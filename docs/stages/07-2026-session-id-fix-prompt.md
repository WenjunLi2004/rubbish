# Stage 2026 Review Fix 3 Prompt

工作目录：`/Users/wenjun/Documents/GitHub/rubbish`

请在当前 `main` 基础上做一次很小的并发一致性修复。上一轮 `727550d` 已经把重连判定从“只看 FIFO 路径”改成“FIFO + login_time”，但 `login_time` 来自 `time(NULL)`，只有秒级精度；如果同一用户在同一秒内 logout/login，仍可能误判为同一会话。

## 必须修复

把“会话判定”从秒级 `login_time` 改为单调递增的 `session_id`。

建议实现：

1. 在 `ChatUserRecord` 中新增：

```c
unsigned long session_id;
```

2. 在 `ChatUserStore` 中新增：

```c
unsigned long session_seq;
```

3. `user_store_init()` 初始化时把 `session_seq = 0`。

4. `user_store_login()` 成功登录时：

```c
s->users[idx].session_id = ++s->session_seq;
```

`login_time` 继续保留，用于日志/展示，不再作为会话判定依据。

5. `user_store_add_bot()` 创建在线机器人时也给机器人分配 `session_id = ++s->session_seq;`，保持用户记录语义一致。

6. `ChatSendInfo` 中把上一轮新增的 `target_login_time` 改成：

```c
unsigned long target_session_id;
```

`user_store_prepare_send()` 填充它。

7. 把当前 `user_store_mark_offline_if_session(..., long login_time)` 改成使用 `unsigned long session_id`：

```c
int user_store_mark_offline_if_session(UserStoreHandle *h,
                                       const char *username,
                                       const char *fifo,
                                       unsigned long session_id);
```

只有当前记录满足：

- 当前仍 online
- fifo 等于发送前快照
- session_id 等于发送前快照

才置离线并返回 1。

8. 更新 `chatserver.c` 的调用与注释：不要再说用 `login_time` 区分新旧会话，改成说明 `session_id` 是每次成功登录递增的会话号，避免同一秒重新登录误判。

9. 由于共享内存结构变了，把 `CHAT_USER_STORE_VERSION` 从 `2u` 改为 `3u`，注释写清楚 2026 版增加 session_id/session_seq。

## 不要做

- 不要改线协议。
- 不要改 smoke 脚本。
- 不要重构 thread pool。
- 不要处理更大的身份认证问题，这一轮只修 session 判定。
- 不要提交 `.DS_Store`、忽略的 HTML、`2026系统编程大作业试题.pdf`。

## 验证

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

如果本机是 macOS、`make smoke` 因 `/dev/shm` 缺失跳过，请如实说明。

## 提交与推送

提交 commit，建议 message：

```text
use session id for reconnect detection
```

push 到 GitHub `main`。

完成后报告：修改文件、修复方式、验证命令结果、commit hash、push 结果。
