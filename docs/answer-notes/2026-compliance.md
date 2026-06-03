# 答题纸素材：2026 试题对齐（线程池 / 在线广播 / 离线消息 / 机器人）

> 说明：原“11 阶段计划”已被 2026 新版 PDF 取代。新版要求在**本阶段**就交付配置驱动的
> 多线程守护服务器、100 线程的线程池、在线名单广播、离线消息暂存回推、重要朋友标记与
> 机器人管理。本仓库已据此重写服务器，旧的“线程池留到阶段 9”等约束作废。

## 路径与命名（试题 1）

二进制 `chatserver_lwj_1.0`（`x.y`：x 奇=开发版，y=bug 修复计数），由配置 `short_name`
与 `version` 派生。公共 FIFO 在 `FIFOFILES=~/Server/fifo`：`lwj_reg_fifo`、`lwj_login_fifo`、
`lwj_msg_fifo`、`lwj_logout_fifo`。日志在 `LOGFILES=~/log/chat-logs`：`server/server.log` 与
`server/threads.log`，均 `O_CREAT|O_APPEND|O_WRONLY` 打开并 `fchmod(0600)`；以 sudo 启动时归 root。

## 线程池（试题 3）—— 为什么用“栈”管理空闲线程

启动时 `pthread_create` 恰好 `POOLSIZE=100` 个工作线程，全部置空闲。空闲线程下标存在一个
**栈**里（LIFO）。主线程 `select` 监听 4 个公共 FIFO，读到一条完整请求后打包成 job，
从栈顶弹出一个空闲线程派发；工作线程处理完业务后把自己**压回栈顶**。这样“刚回收的线程
在栈顶、下一次优先被分派”，符合试题“刚回收的线程倾向于下一次被分派”的要求——也对缓存
亲和友好（刚跑过的线程其栈/TLS 更可能仍在 CPU 缓存里）。每个工作线程带：下标、pthread id、
忙/闲状态、一个 job 槽、一个条件变量。派发与回收的时间戳写入 `threads.log`
（`dispatch ... state=busy` / `recycle ... state=idle`）。无空闲线程时主循环不阻塞：派发返回忙，
（在已知回复 FIFO 时）回 `server busy`。

## 共享内存线程安全（试题 2.1）

用户表仍在 POSIX 共享内存（`/dev/shm/chatroom_lwj_users`，0600），锁是放在共享区内、
属性 `PTHREAD_PROCESS_SHARED` 的 `pthread_mutex_t`；Linux/euler 上初始化为进程共享成功
（`server.log` 记 `process-shared`），失败则启动中止（仅 macOS 开发机允许退回）。100 个
工作线程并发访问 `user_count/users[]/send_count[][]/offline_messages[]` 全部走这把锁；
需要写 FIFO 的数据先在锁内复制成本地快照，FIFO I/O 在锁外做，临界区保持最小。

## 业务（试题 2.2–2.4、机器人）

- **注册**：唯一用户名/密码，重名拒绝；记 `(user, register, time)`。
- **登录**：校验后置在线、回执含在线人数与名单、向其他在线用户广播上线与刷新名单、
  回推暂存的离线消息（保留原始发送时间）。
- **发送**：目标在线则投递并 `send_count[from][to]++`、记 `(from,to,time,sent)`；成功次数
  **超过 5** 时发送方一侧把目标显示为 `name*`（重要朋友）。目标离线则暂存离线消息、记
  `(from,to,time,pending)`，重登后回推并改记 sent。
- **登出**：校验后置离线，向其余在线用户广播登出与剩余名单。
- **机器人**：`/bot add x` 由客户端发往保留目标 `__botmgr__`（复用 MSG_FIFO，不新增第 5 个
  FIFO）。服务器创建 x 个随机用户名/密码的在线机器人（`is_bot=1`），像普通用户一样记注册/
  登录并广播名单；`liwenjun_1` 向在线机器人发消息，机器人立即回 `幸会，liwenjun_1，很高兴认识您`。
  `/bot del x` 随机选 x 个在线机器人置离线并广播。**机器人名随机、可能永不再来，故不为离线
  机器人保留离线消息**，丢弃情形在日志中明确记录。

## 验证

`make smoke`（`scripts/smoke/smoke-2026.sh`，需 Linux `/dev/shm`）核验：二进制名、守护化、
4 个 FIFO、两份日志存在且 0600、`thread pool created: 100 threads`、注册/重名/登录/失败登录、
在线 5 次发送、登出广播、离线暂存与重登回推、机器人增/发/删、`threads.log` 派发/回收、
SIGTERM 后 FIFO 与 shm 清理及线程池干净关闭。该脚本在“有 sudo”（试题演示，日志 root）与
“无 sudo”（日志归当前用户）两种模式下都能跑。多终端手工演示见 `scripts/demo/2026-demo.md`。

**尚不证明**：极端并发压力下的吞吐/公平性；机器人大量增删后用户槽（`CHAT_MAX_USERS=64`）
被占满的回收（离线机器人槽暂不回收，演示规模足够，已在代码注释说明）。

## AIGC prompt 留痕

按报告“AIGC 代码片段必须附上 prompts”的要求，驱动本次实现的 prompt 原文保存在
`docs/stages/04-2026-spec-compliance-prompt.md`；各阶段 prompt 一并保留在 `docs/stages/`。
