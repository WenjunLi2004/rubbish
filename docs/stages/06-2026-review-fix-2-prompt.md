# Stage 2026 Review Fix 2 Prompt

工作目录：`/Users/wenjun/Documents/GitHub/rubbish`

你需要在当前 `main` 基础上做一次很小的 review 修复。上一轮 commit `405c427` 已经修了大部分问题，但 Codex review 后还发现两个并发一致性细节需要补。

## 背景

当前项目是系统编程大作业选题（一）“单机多用户聊天”，已经按 2026 新试题要求做了：

- `chatserver_lwj_1.0`
- `~/Server/fifo/` 四个公共 FIFO
- `~/log/chat-logs/server/server.log` 与 `threads.log`
- `POOLSIZE=100` 的线程池
- POSIX 共享内存用户表
- 登录在线名单、logout 广播、离线消息、重要朋友 `*`、机器人管理等

请不要扩大功能范围，不要重写协议，不要重构无关代码。

## 必须修复的问题

### 1. 离线消息 clear 失败时不能继续计数/记录 sent

当前 `chatserver.c` 的 `push_offline_messages()` 中：

```c
user_store_clear_offline(&g_store, slots[i], &msgs[i]);
user_store_increment_send(&g_store, msgs[i].from, user);
log_message("INFO", "(%s, %s, %s, sent) [offline push]", ...);
pushed++;
```

问题：`user_store_clear_offline()` 已经返回 `0/1`，但调用方忽略返回值。极端情况下，如果同一用户并发登录导致另一条 worker 已经清掉该离线槽，本 worker 再投递成功后不应继续：

- 递增 `send_count`
- 记录 `sent`
- 增加 `pushed`

要求：

- 检查 `user_store_clear_offline()` 返回值。
- 只有 clear 成功时才 `user_store_increment_send()`、记录 sent、`pushed++`。
- clear 失败时记录一条 `WARN`，说明 offline slot was already cleared/changed；不要再计数。
- FIFO 写仍然保持在共享内存锁外。

同时请把 `user_store_clear_offline()` 的匹配条件补得更完整：除了 `used/from/to/timestamp`，也比较 `text`，避免同一秒同一 from/to 的不同消息在并发场景下被误判为同一条。

### 2. 目标重新登录不能只靠 FIFO 路径判断

上一轮在 `do_chat_to_human()` 里用：

```c
user_store_mark_offline_if_fifo(&g_store, req->to, info->to_fifo)
```

意图是：在线发送失败后，只有当目标仍是同一会话时才置离线；如果目标已经重新登录，就不要把它误置离线。

但当前客户端私有 FIFO 基本按用户名派生，同一用户重新登录后 FIFO 路径通常仍然相同。因此“比较 FIFO 字符串”不足以区分旧登录和新登录。

要求：

- 在 `ChatSendInfo` 中增加目标登录时间快照，例如 `long target_login_time;`
- 在 `user_store_prepare_send()` 中填充该字段。
- 把 `user_store_mark_offline_if_fifo()` 改为更准确的函数，例如：

```c
int user_store_mark_offline_if_session(UserStoreHandle *h,
                                       const char *username,
                                       const char *fifo,
                                       long login_time);
```

- 只有当目标当前记录同时满足：
  - username 匹配
  - fifo 仍等于快照 fifo
  - login_time 仍等于快照 login_time
  - 当前仍 online

  才置离线并返回 1。

- `do_chat_to_human()` 调用新函数，并更新注释，不要再写“FIFO 变化才代表重新登录”这种不准确表述。
- 如果 mark 返回 0，继续使用现在的逻辑：给发送者回复 `target just reconnected; please resend`，不要暂存离线消息。

## 验证要求

请运行：

```bash
make clean && make
bash -n scripts/smoke/*.sh
git diff --check
```

如果当前环境是 euler/Linux 且可 sudo，请额外运行：

```bash
sudo -v && make smoke
```

如果本机是 macOS、`make smoke` 因 `/dev/shm` 缺失跳过，请如实说明，不要把它说成完整通过。

## 提交要求

- 提交 commit，建议信息：

```text
fix offline replay and reconnect race
```

- push 到 GitHub `main`。
- 不要提交：
  - `.DS_Store`
  - 被 `.gitignore` 忽略的 HTML 文件
  - `2026系统编程大作业试题.pdf`

## 完成后报告

请报告：

- 修改了哪些文件
- 两个问题分别怎么修
- 跑了哪些命令，结果如何
- 如果没有在 euler 上跑 `make smoke`，明确说明
- commit hash 与 push 结果
