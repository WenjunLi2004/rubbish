# 通信协议（2026 试题对齐版）

> **2026 对齐说明**：公共 FIFO 改名为 `~/Server/fifo/lwj_{reg,login,msg,logout}_fifo`；
> 二进制 `chatserver_lwj_1.0`；`CHAT_TEXT_LEN` 提升到 512（在线名单更长，仍远小于
> PIPE_BUF=4096，单包 write 在 FIFO 上保持原子）；`ChatPacket` 尾部新增
> `timestamp/send_count/online_count`，新增包类型 `ONLINE_LIST/OFFLINE_PUSH/SYSTEM`；
> 机器人管理复用 MSG_FIFO（`to=__botmgr__`），不新增第 5 个公共 FIFO。

> 一切请求与回复都是**定长二进制结构体**，通过命名管道传输。
> 服务器和客户端都包含 `chat_common.h`，**严禁两端结构体定义不一致**。
> 任何破坏二进制兼容性的修改必须同步更新本文档并在 commit message 里显式标注。

## 一、FIFO 列表

### 公共 FIFO（多写者→单读者，服务器端）

| 名 | 路径 | 客户端写入 | mkfifo 模式 |
|---|---|---|---|
| REG_FIFO | `~/Server/fifo/lwj_reg_fifo` | `ChatAuthRequest` | 0666 |
| LOGIN_FIFO | `~/Server/fifo/lwj_login_fifo` | `ChatAuthRequest` | 0666 |
| MSG_FIFO | `~/Server/fifo/lwj_msg_fifo` | `ChatSendRequest`（机器人管理 `to=__botmgr__` 复用此 FIFO） | 0666 |
| LOGOUT_FIFO | `~/Server/fifo/lwj_logout_fifo` | `ChatLogoutRequest` | 0666 |

服务器创建 FIFO 前必须 `umask(0)`，否则 0666 会被默认 umask 022 砍成 0644，客户端就写不进去。

### 私有 FIFO（单写者→单读者，每个客户端一个）

- 路径：`<data>/client_fifo/<username>`
- 模式：`0600`（客户端进程自己拥有，其他用户读不到）
- 读取的是统一的 `ChatPacket`

## 二、请求结构体

### `ChatAuthRequest` —— 注册 & 登录复用

```c
typedef struct {
    char username[CHAT_NAME_LEN];     // 32, '\0' 结尾
    char password[CHAT_PASSWORD_LEN]; // 32, 明文（作业级，不做加密）
    char fifo[CHAT_FIFO_PATH_LEN];    // 256, 客户端的私有 FIFO 全路径
} ChatAuthRequest;
```

注册与登录用同一结构体，区分依赖**写入的 FIFO** 不同（REG_FIFO vs LOGIN_FIFO），不在结构体内携带 op 字段。

### `ChatSendRequest` —— 聊天消息

```c
typedef struct {
    char from[CHAT_NAME_LEN];
    char to[CHAT_NAME_LEN];           // 单一接收人
    char text[CHAT_TEXT_LEN];         // 256
} ChatSendRequest;
```

**一对多发送**由客户端循环 N 次写入 MSG_FIFO，每次只发一个 to，保持协议简单。

### `ChatLogoutRequest`（v1.0.0 新增）

```c
typedef struct {
    char username[CHAT_NAME_LEN];
    char fifo[CHAT_FIFO_PATH_LEN];    // 用于和用户表里的当前 fifo 比对，做弱身份校验
} ChatLogoutRequest;
```

退出请求只需身份标识，不带密码。服务器收到后：
1. 在用户表里找该 username
2. 比对请求里的 fifo 与表中记录是否一致（一致才接受退出，否则丢弃）
3. 置 `online=0`，更新 `logout_time`，对所有其他在线用户广播 ONLINE_LIST

## 三、回复结构体（统一 `ChatPacket`）

```c
typedef enum {
    CHAT_PACKET_REPLY        = 1, // 服务器对某个请求的应答
    CHAT_PACKET_MESSAGE      = 2, // 别人发来的实时聊天消息（含机器人回复）
    CHAT_PACKET_ONLINE_LIST  = 3, // 在线人数 + 名单广播
    CHAT_PACKET_OFFLINE_PUSH = 4, // 重新登录后的离线消息回推（带原始时间）
    CHAT_PACKET_SYSTEM       = 5, // 系统事件：登入/登出/机器人增减
} ChatPacketType;

typedef struct {
    int  type;                      // ChatPacketType
    int  ok;                        // REPLY: 1=成功 0=失败；其他类型固定 1
    char from[CHAT_NAME_LEN];       // 消息来源；server 端回包用 "server"
    char message[CHAT_TEXT_LEN];    // 文本载荷 / 在线名单 "u1,u2*,u3"
    long timestamp;                 // 服务器侧 time(NULL)；OFFLINE_PUSH 为原始发送时间
    int  send_count;                // from→to 的累计成功次数（含本次）
    int  online_count;              // 在线总数（ONLINE_LIST / 登录回执）
} ChatPacket;
```

`CHAT_TEXT_LEN=512`。计算 `sizeof(ChatPacket) = 4+4+32+512+8+4+4 = 568`（8 字节对齐）。
实测值由服务器启动时打印进 `server.log`（`sizeof(ChatPacket)=...`）。

### 为什么不为每种 type 单独定义结构体

- 客户端只用 `read(fifo_fd, &packet, sizeof(ChatPacket))` 一句话就能处理所有事件
- 新增 type 不需要客户端 read 路径分支
- 代价是 message 字段在 ONLINE_LIST 这种场合被复用为"逗号分隔列表"，文档里说清楚即可

### 各 type 的字段约定

| type | from | message | timestamp | send_count |
|---|---|---|---|---|
| `REPLY` | `"server"` | 人类可读的成功/错误文案 | 0 | 0 |
| `MESSAGE` | 发送方用户名（>5 次时尾部带 `*`，例如 `lwj_2*`） | 正文 | 服务器收到的时间 | from→to 累计成功次数 |
| `ONLINE_LIST` | `"server"` | 形如 `online=3:lwj_1,lwj_2*,lwj_3`，逗号分隔；`*` 标记表示与"我"是重要朋友（>5 次往来） | 广播时间 | 在线总数 |
| `OFFLINE_PUSH` | 原始发送方 | 原始正文 | 原始发送时间 | 累计成功次数（含本次） |

## 四、"重要朋友"规则（试题 2.4）

- 服务器为每对有序键 `(from, to)` 维护一个 `send_count`，每次成功送达 +1
- 当 `send_count > 5`，在向 `to` 发的 MESSAGE 包里把 `from` 字段写成 `<name>*`
- ONLINE_LIST 广播也用 `*` 标记发起人（"我"）的重要朋友
- `*` 是展示用，不参与字符串匹配；客户端解析时按需剥离

## 五、登出与离线消息推送时序

```
B 离线状态下：
  A 发送 → 服务器收到 MSG → 发现 B 离线
        → 写入 offline_queue[B]（保存 from, text, timestamp）
        → 向 A 回 REPLY(ok=0, "target user is not online")
        → 用户日志 users/A.log 记一条 "未送达 (A,B,time,false)"

B 重新登录：
  B 写 LOGIN_FIFO → 服务器登录线程
        → 置 online=1
        → 对所有 online 用户广播 ONLINE_LIST
        → 取 offline_queue[B] 逐条转成 OFFLINE_PUSH 写入 B 的私有 FIFO
        → 每推送一条，把 (A,B) 的 send_count++，A 的用户日志改成"成功"
        → 清空 offline_queue[B]
```

## 六、配置文件 `config/chatserver.conf`

格式：`key = value`，`=` 两侧空格可有可无；`#` 起始为整行注释。

```ini
# === 服务器身份 ===
server_name = chatserver
short_name  = lwj
version     = 1.0

# === 路径（2026）===
fifo_dir        = /home/liwenjun2023150001/Server/fifo
client_fifo_dir = /home/liwenjun2023150001/Client/fifo
log_dir         = /home/liwenjun2023150001/log/chat-logs

# === FIFO 命名前缀 ===
fifo_prefix = lwj

# === 线程池 ===
poolsize    = 100
```

服务器启动时：
- 解析失败 → 立即 exit(1)，并向 stderr 打印错误（此时 server.log 尚未打开）
- 解析成功 → 守护化、打开日志，把生效配置写入 `server.log`

派生字段（解析完成后由 `chat_config_load` 自动填充）：
- `full_name = "chatserver_lwj_1.0"`
- `fifo_register = <fifo_dir>/lwj_reg_fifo`，其余三个同理（`_login_/_msg_/_logout_`）
- `server_log_path  = <log_dir>/server/server.log`
- `threads_log_path = <log_dir>/server/threads.log`

## 七、版本演进与兼容性

- **新字段只能追加在结构体尾部**，不得改字段顺序、不得改字段大小
- **字段长度宏（CHAT_NAME_LEN 等）改动算重大版本变更**，需要全量重新编译并 bump version
- 客户端遇到未识别的 `type` 值：打印一行警告，**不要 abort**
- `reserved` 字段未来可能被定义新含义；本版本读到非 0 也不报错

## 八、当前协议字段总览（速查）

阶段 01 实测（编译器 gcc 11.x，x86_64 SysV ABI，4 字节对齐）：

```
sizeof(ChatAuthRequest)   = 32 + 32 + 256                  = 320
sizeof(ChatSendRequest)   = 32 + 32 + 512                  = 576
sizeof(ChatLogoutRequest) = 32 + 256                       = 288
sizeof(ChatPacket)        = 4 + 4 + 32 + 512 + 8 + 4 + 4   = 568
```

注：`CHAT_TEXT_LEN` 由 256 提升到 512（在线名单更长）。所有定长结构体仍 < PIPE_BUF(4096)，
单次 `write` 在 FIFO 上保持原子，并发工作线程写同一客户端 FIFO 不会交错。实测以
`server.log` 启动横幅为准。
