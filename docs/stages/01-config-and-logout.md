# Stage 01：配置文件 + 4 个公共 FIFO + 路径修正 + logout 骨架

## 目标（一句话）

把硬编码的路径与 FIFO 名替换成读配置文件，新增 LOGOUT_FIFO 打通端到端最小链路。**本阶段不引入线程、不动业务逻辑、不 daemon 化、不写日志文件。**

## 涉及文件

### 新增
- `config/chatserver.conf` — 服务器配置
- `src/config.h` — 配置结构体声明
- `src/config.c` — 配置文件解析与派生字段计算
- `Makefile` — 单一构建脚本，自动从 conf 抽 short_name/version 命名服务器二进制
- `scripts/smoke/smoke-stage01.sh` — 端到端冒烟测试
- `docs/answer-notes/01.md` — 答题纸素材（200~300 字）

### 修改
- `chat_common.h` — 删除所有路径与 FIFO 名宏；保留 `CHAT_NAME_LEN/CHAT_PASSWORD_LEN/CHAT_FIFO_PATH_LEN/CHAT_TEXT_LEN` 等长度宏；新增 `ChatLogoutRequest`
- `chatserver.c` — `main` 接受 `argv[1]` 作为 conf 路径；启动时 `chat_config_load`；监听 4 个 FIFO；新增 `drain_logout_fifo` 与 `handle_logout`（stub）
- `chatclient.c` — `main` 签名变更为 `chatclient <conf> <username> <password>`；启动时 `chat_config_load`；新增 `/logout` 命令；所有 FIFO 路径改读 `ChatConfig`

### 严格不动
- `ChatAuthRequest` / `ChatSendRequest` / `ChatPacket` 的字段顺序与数量（本阶段都不动；`ChatPacket` 的 `timestamp/send_count/reserved` 留到后续阶段加）
- 用户表结构（保持 `static User users[]`，stage 3 才换共享内存）
- 单线程 select() 主循环（stage 4 才多线程化）

## 详细约束

### 1. `config/chatserver.conf` 内容

```ini
# === 服务器身份 ===
server_name = chatserver
short_name  = lwj
version     = 1.0.0

# === 路径 ===
data_dir    = /home/liwenjun2023150001/ChatRoom/data
log_dir     = /var/log/chat-logs

# === FIFO 命名前缀 ===
fifo_prefix = lwj

# === 线程池（阶段 9 起生效，本阶段读取但不使用） ===
poolsize    = 10
```

### 2. `src/config.h` 接口

```c
#ifndef CHAT_CONFIG_H
#define CHAT_CONFIG_H

typedef struct {
    /* 配置文件原始字段 */
    char server_name[64];
    char short_name[16];
    char version[16];
    char data_dir[256];
    char log_dir[256];
    char fifo_prefix[16];
    int  poolsize;

    /* 派生字段，chat_config_load 自动填充 */
    char full_name[96];          /* "chatserver_lwj_1.0.0" */
    char server_fifo_dir[320];   /* <data_dir>/server_fifo */
    char client_fifo_dir[320];   /* <data_dir>/client_fifo */
    char fifo_register[384];
    char fifo_login[384];
    char fifo_message[384];
    char fifo_logout[384];
    char log_dir_server[320];    /* <log_dir>/server */
    char log_dir_users[320];     /* <log_dir>/users */
    char server_log_path[512];   /* <log_dir>/server/server.log */
    char threads_log_path[512];  /* <log_dir>/server/threads.log */
} ChatConfig;

/* 成功返回 0；失败返回 -1 并向 stderr 打印原因。 */
int chat_config_load(const char *path, ChatConfig *out);

#endif
```

### 3. 解析规则

- 每行 `key = value`，等号两侧空白随意
- 行首 `#` 整行注释；空行忽略
- 未知 key → stderr 警告，继续解析
- 7 个字段（除 poolsize 可缺省 10 外）必填，缺一报错退出
- 解析完后做派生字段拼装；任何拼装结果超长立即报错退出
- `poolsize` 必须为正整数，越界报错

### 4. `Makefile` 关键片段

```make
SHORT_NAME := $(shell awk -F= '/^short_name/{gsub(/[ \t]/,""); print $$2}' config/chatserver.conf)
VERSION    := $(shell awk -F= '/^version/{gsub(/[ \t]/,""); print $$2}' config/chatserver.conf)
SERVER_BIN := bin/chatserver_$(SHORT_NAME)_$(VERSION)
CLIENT_BIN := bin/chatclient

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=gnu11 -Isrc
LDFLAGS :=

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): chatserver.c src/config.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): chatclient.c src/config.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf bin

smoke: all
	scripts/smoke/smoke-stage01.sh

.PHONY: all clean smoke
```

### 5. 服务器启动 stdout（验收对比基准）

```
[chatserver_lwj_1.0.0] config loaded from config/chatserver.conf
  data_dir        = /home/liwenjun2023150001/ChatRoom/data
  log_dir         = /var/log/chat-logs   (本阶段尚未启用)
  server_fifo_dir = /home/liwenjun2023150001/ChatRoom/data/server_fifo
  client_fifo_dir = /home/liwenjun2023150001/ChatRoom/data/client_fifo
[chatserver_lwj_1.0.0] sizeof(ChatPacket)        = NNN
[chatserver_lwj_1.0.0] sizeof(ChatAuthRequest)   = NNN
[chatserver_lwj_1.0.0] sizeof(ChatSendRequest)   = NNN
[chatserver_lwj_1.0.0] sizeof(ChatLogoutRequest) = NNN
[chatserver_lwj_1.0.0] listening on:
  REG_FIFO    -> .../data/server_fifo/lwjregister
  LOGIN_FIFO  -> .../data/server_fifo/lwjlogin
  MSG_FIFO    -> .../data/server_fifo/lwjsendmsg
  LOGOUT_FIFO -> .../data/server_fifo/lwjlogout
[chatserver_lwj_1.0.0] ready (single-threaded select)
```

把实测出的 sizeof 写回 `docs/protocol.md` 第八节"当前协议字段总览"。

### 6. `handle_logout`（stub 版）

```c
static void handle_logout(const ChatLogoutRequest *req) {
    int idx = find_user(req->username);
    if (idx == -1) return;                                /* 不存在的用户，沉默丢弃 */
    if (strcmp(users[idx].fifo, req->fifo) != 0) return;  /* fifo 不匹配，弱身份校验失败 */
    users[idx].online = 0;
    reply_to_fifo(req->fifo, 1, "logout ok");
    printf("logout user: %s\n", req->username);
    /* 不广播 ONLINE_LIST，那是 stage 6 */
}
```

### 7. 客户端 `/logout` 命令

```c
if (strcmp(line, "/logout") == 0) {
    ChatLogoutRequest req;
    memset(&req, 0, sizeof(req));
    copy_string(req.username, sizeof(req.username), username);
    copy_string(req.fifo, sizeof(req.fifo), myfifo);
    write_request(cfg.fifo_logout, &req, sizeof(req));
    return 0;   /* 进程不退出 */
}
```

`/logout` 是登出，进程继续；`/quit` 才退进程。

### 8. mkfifo 与 umask

```c
umask(0);                /* 必须在 mkfifo 之前。否则默认 022 会把 0666 砍成 0644 */
mkfifo(path, 0666);
```

### 9. sudo 启动后的目录所有权

服务器以 sudo 运行，第一次创建 `data/server_fifo/` 与 `data/client_fifo/` 时 owner 会是 root。需要在创建后立即把目录与文件 `chown` 回 liwenjun2023150001:

```c
struct passwd *pw = getpwnam("liwenjun2023150001");
if (pw && chown(cfg.server_fifo_dir, pw->pw_uid, pw->pw_gid) == -1)
    perror("chown server_fifo_dir");
```

不要写死 1001 这种 uid。

## 验收流程（在服务器上执行）

```bash
cd /home/liwenjun2023150001/ChatRoom

# 1. 编译过
make clean && make
# 期望产出 bin/chatserver_lwj_1.0.0 和 bin/chatclient

# 2. 配置文件可读
cat config/chatserver.conf

# 3. 启动服务器
sudo ./bin/chatserver_lwj_1.0.0 config/chatserver.conf &
SERVER_PID=$!
sleep 1

# 4. 4 个 FIFO 存在，文件类型字段首字符为 p
ls -l data/server_fifo/
# 期望 4 行：
#   prw-rw-rw- ... lwjlogin
#   prw-rw-rw- ... lwjlogout
#   prw-rw-rw- ... lwjregister
#   prw-rw-rw- ... lwjsendmsg

# 5. 端到端注册
./bin/chatclient config/chatserver.conf lwj_test 1234 <<'EOF'
/register
/quit
EOF
# 期望 stdout 包含：[server] OK: register ok

# 6. 端到端登录 + 登出
./bin/chatclient config/chatserver.conf lwj_test 1234 <<'EOF'
/login
/logout
/quit
EOF
# 期望两条：[server] OK: login ok 然后 [server] OK: logout ok

# 7. 清理
sudo kill $SERVER_PID
ls data/server_fifo/
# 期望为空（信号处理 unlink 了 4 个 FIFO）
```

`scripts/smoke/smoke-stage01.sh` 把上述步骤打包成无人工干预脚本，结尾打印 `STAGE 01 PASS` 或 `STAGE 01 FAIL`。

## 答题纸素材

`docs/answer-notes/01.md` 写一段 200~300 字，覆盖：

1. 服务器命名 `chatserver_lwj_1.0.0` 借鉴 Linux 内核版本号 x.y.z 命名法的含义（主版本 / 偶数稳定版 / 补丁号）
2. 4 个公共 FIFO 名 `lwj{register,login,sendmsg,logout}` 由配置项 `fifo_prefix` 派生，体现配置文件解耦
3. `ls -l` 输出中文件类型字段首字符 `p` 是 FIFO 的标志（区分于 `-` 普通文件、`d` 目录、`l` 符号链接、`c` 字符设备、`b` 块设备、`s` socket）
4. 服务器创建公共 FIFO 时为何要 `umask(0)`

这一段直接进入答卷的 1.2/a 小问（4 分）。

## Git commit message

```
stage 01: config file + 4 FIFOs + logout skeleton

- add config/chatserver.conf, src/config.[ch]
- rename FIFOs to lwj{register,login,sendmsg,logout} per spec 1.1.2
- add ChatLogoutRequest struct and /logout client command
- drop hardcoded paths from chat_common.h; chat_common.h now contains
  only length macros and wire structures
- Makefile derives binary name from short_name and version
- record sizeof of each wire struct into docs/protocol.md §8

server name: chatserver_lwj_1.0.0
not yet covered (later stages): daemon, server.log, threads, shm, broadcast
```

完成后 `git push origin main` 上 GitHub 当 backup。

## 下一阶段预告

Stage 02：把服务器变成守护进程（5 步法）、忽略 SIGCHLD/SIGTERM、写 `/var/log/chat-logs/server/server.log`。
