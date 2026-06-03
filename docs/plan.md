# 大作业实施计划（选题一·功能迭代）

> ⚠️ **本 11 阶段计划已被 2026 新版试题取代（SUPERSEDED）。**
> 课程 PDF（`2026系统编程大作业试题.pdf`）改版后，服务器已一次性对齐新要求：
> 二进制 `chatserver_lwj_1.0`、公共 FIFO `~/Server/fifo/lwj_*_fifo`、日志 `~/log/chat-logs`、
> `POOLSIZE=100` 真实线程池（LIFO 空闲栈）、在线名单广播、离线消息暂存回推、
> 重要朋友 `*`、机器人管理。原先“线程池留到阶段 9 / 离线消息留到阶段 8”等分阶段约束已作废。
> 实现说明见 `docs/answer-notes/2026-compliance.md`，演示见 `scripts/demo/2026-demo.md`，
> 验证用 `make smoke`（`scripts/smoke/smoke-2026.sh`）。下文保留原计划仅作历史参考。

## 项目元信息

| 项 | 值 |
|---|---|
| 学生 | 李文俊 (liwenjun2023150001) |
| 拼音首字母缩写 | `lwj` |
| 服务器命名规则 | `chatserver_lwj_<x>.<y>.<z>` |
| 基础版本 | `1.0.0`（阶段 1～8） |
| 线程池版本 | `1.2.0`（阶段 9；y=2 偶数表示稳定版，符合 Linux 内核版本号约定） |
| 远端主机 | `liwenjun2023150001@172.31.234.194` (`euler-container-30001`) |
| 服务器工作目录 | `/home/liwenjun2023150001/ChatRoom` |
| 日志根目录 | `/var/log/chat-logs/`（mode 0600, owner root；服务器以 sudo 启动写入） |
| GitHub 仓库 | `https://github.com/WenjunLi2004/rubbish`（public，作为备份/版本控制） |

## 工作流

Claude Code 通过终端直接在服务器上执行命令和编辑代码。**单一源，单一副本，在服务器上**。

```
Claude Code（在服务器 /home/liwenjun2023150001/ChatRoom）
   │
   │ 编辑 → make → 跑 → 看 /var/log/chat-logs/
   │
   ▼
完成一个阶段 → git add/commit → git push 到 GitHub 当 backup
   │
   ▼
你 git diff HEAD~1 贴回这个对话 → review → 进入下一阶段
```

**一次性准备**：

1. 服务器上 `cd /home/liwenjun2023150001 && git clone https://github.com/WenjunLi2004/rubbish ChatRoom`
2. `echo "export PS1='[\u@\h \W]\$ '" >> ~/.bashrc && source ~/.bashrc`，保证后续截屏提示符符合试题要求
3. 把 `docs/`、`scripts/` 这几个目录提交到仓库（先在本地或服务器上都行，commit + push）
4. Claude Code 启动，工作目录 `/home/liwenjun2023150001/ChatRoom`

辅助脚本（`scripts/remote.sh`、`scripts/logs.sh`）现在主要是**你自己**用的便利工具——比如在另一个 ssh 窗口看日志、远程跑命令。Claude Code 自己直接在工作目录里就行，不必经它们。

## 公共 FIFO 命名（试题 1.1.2）

由配置 `fifo_prefix=lwj` 派生：

| 用途 | 配置项 | 文件名 | 完整路径 |
|---|---|---|---|
| 注册 | REG_FIFO | `lwjregister` | `<data>/server_fifo/lwjregister` |
| 登录 | LOGIN_FIFO | `lwjlogin` | `<data>/server_fifo/lwjlogin` |
| 发消息 | MSG_FIFO | `lwjsendmsg` | `<data>/server_fifo/lwjsendmsg` |
| 退出 | LOGOUT_FIFO | `lwjlogout` | `<data>/server_fifo/lwjlogout` |

## 日志（试题 1.1.3 / 1.2.2）

| 文件 | 路径 | 权限 | 写入内容 |
|---|---|---|---|
| 服务器日志 | `/var/log/chat-logs/server/server.log` | `0600 root` | 启动、退出、致命错误 |
| 线程池日志 | `/var/log/chat-logs/server/threads.log` | `0600 root` | 阶段 9 起：线程分派/回收时间戳 |
| 用户日志 | `/var/log/chat-logs/users/<u>.log` | `0600 root` | 注册/登录/退出/在线时长/与各好友发送次数/未送达消息 |

## 11 个里程碑

每阶段对应 `docs/stages/NN-*.md` 一份 brief。

| # | 主题 | 试题分数对应 |
|---|---|---|
| 01 | 配置文件 + 4 个 FIFO + 路径修正 + logout 骨架 | 1.2/a 4 分 |
| 02 | 守护进程化（5 步法）+ 信号忽略 + server.log | 1.1) 6 分 + 1.2/b 5 分 |
| 03 | 共享内存用户表 + 互斥锁（pthread pshared） | 2.1) 5 分 |
| 04 | 多线程化：主线程 select 分派，4 类请求各起一个工作线程 | 1.2.1 隐含 |
| 05 | 注册线程函数 + 重名拒绝 + 4 终端注册演示 | 2.2) 5 分 |
| 06 | 登录/退出线程 + 在线人数与名单广播 (ONLINE_LIST 包) | 2.3) 10 分 |
| 07 | 一对一/一对多发送 + 重要朋友标记 (`name*`) | 2.4) 20 分 |
| 08 | 用户日志 + 离线消息暂存与回推 | 1.2.2 + 难点 5 分 |
| 09 | 升版本 1.2.0 + 线程池 (LIFO 回收) + threads.log | 3) 20 分 |
| 10 | 多路复用 fd 集合变化代码注释 + 性能/安全讨论素材 | 1.2/c 10 分 |
| 11 | tmux 演示脚本 + 答题纸截屏 + 报告整理 | 提交 |

## 每阶段交付物

1. 通过验收命令的代码变更（git commit）
2. `make smoke` 跑过
3. **答题纸素材**：agent 主动产出 200~400 字解释写到 `docs/answer-notes/NN.md`，回贴对话给我 review

## 设计决策（已定，后续不要反复改）

- 协议层只用命名管道，不引入 socket
- 多路复用用 `select()`，不切换 epoll/poll；阶段 10 在讨论里比较优劣
- 共享内存用 POSIX `shm_open` + `mmap`；互斥锁 `pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)` 放在 shm 区内
- 线程池数据结构是栈（LIFO），匹配"刚回收的线程倾向于下一次被分派"
- 二进制协议字段只能加在结构体尾部，不重排已有字段
- 客户端 UI 保持现状，只加 `/logout`、必要时加 `/who`
- 客户端进程 ≠ 登录态：`/login` 和 `/logout` 是会话管理，`/quit` 才退进程

## 答题纸 ↔ 阶段对应表

| 题号 | 分值 | 由哪个阶段产出素材 |
|---|---|---|
| 1.1) ps 看 daemon | 6 | 阶段 2 |
| 1.2/a 4 FIFO + ls -l | 4 | 阶段 1 |
| 1.2/b 日志代码与 cat | 20 | 阶段 2 + 阶段 8 |
| 1.2/c 多路复用 fd 集合 | 10 | 阶段 10 |
| 2.1) 共享内存线程安全 | 5 | 阶段 3 |
| 2.2) 注册线程函数 + 重名 | 5 | 阶段 5 |
| 2.3) 登录线程 + 退出广播 | 10 | 阶段 6 |
| 2.4) 一对一/一对多/重要朋友 | 20 | 阶段 7 |
| 2.4) 未送达消息管理 | 5 | 阶段 8 |
| 3.a/b/c 线程池 | 20 | 阶段 9 |

## 风险点（提前知会）

- **`/dev/shm` 在容器里**：阶段 3 开头 agent 要先 `ls /dev/shm` 验证 POSIX shm 可用
- **daemon 的 stdout/stderr**：daemon 化时这些 fd 要重定向到 `/dev/null` 或日志文件，否则后续 `printf` 直接丢；阶段 2 必须处理
- **FIFO 权限与 umask**：mkfifo 前必须 `umask(0)`，否则 0666 被砍成 0644，客户端写不进去
- **sudo 启动后的目录 owner**：服务器以 root 创建 `data/server_fifo/` 后要 `chown` 回 liwenjun，否则用户态客户端读写受阻
- **截屏提示符**：必须以 `liwenjun2023150001` 登录、`PS1='[\u@\h \W]\$ '`。一次性准备里已写

## 使用本 plan

每开新阶段：
1. 你说"开做阶段 N"
2. Claude Code 接到 `docs/stages/NN-*.md` 这份 brief，开始实现
3. agent 自己跑 `make` + `make smoke`，跑过后写 commit
4. 你 `git diff HEAD~1` 贴对话给我 review
5. 答题纸素材入档 `docs/answer-notes/NN.md`
6. push 到 GitHub backup，进入下一阶段
