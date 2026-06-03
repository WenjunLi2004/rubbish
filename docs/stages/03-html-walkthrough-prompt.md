# Claude Code Prompt: Generate Stage 03 HTML Walkthrough

You are Claude Code running in:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

Your task is documentation only. Do not change C source, scripts, Makefile, Git
history, or existing commits.

Create a local HTML walkthrough file:

```text
03-walkthrough.html
```

This HTML is for the student to understand Stage 03. It is intentionally local
and ignored by Git via `*.html`; do not force-add it and do not commit it.

## Read First

Read these files before writing the HTML:

- `01-walkthrough.html` if it exists locally, as a visual/style reference.
- `02-walkthrough.html` if it exists locally, as the direct previous-stage
  reference.
- `CLAUDE.md`
- `docs/plan.md`
- `docs/protocol.md`
- `docs/stages/03-shared-memory-user-table.md`
- `docs/stages/03-claude-code-prompt.md`
- `docs/answer-notes/03.md`
- `chatserver.c`
- `src/user_store.h`
- `src/user_store.c`
- `Makefile`
- `scripts/smoke/smoke-stage03.sh`

Also inspect the Stage 03 commit:

```bash
git log --oneline --decorate -5
git show --stat --oneline ddf9d4b
git diff 32db261..ddf9d4b -- chatserver.c src/user_store.h src/user_store.c Makefile scripts/smoke/smoke-stage03.sh docs/answer-notes/03.md CLAUDE.md
```

If later review-fix commits exist after `ddf9d4b`, explain the final current
state after those fixes, not only the first Stage 03 commit.

## Style Rules

- Use Chinese.
- Use a left-side navigation/sidebar table of contents so the student can jump
  between major sections.
- Keep the HTML self-contained with inline CSS.
- Do not use "AI 辅助", "基线", "注意", or badge labels.
- Do not include an "AI 辅助代码索引".
- Do not talk about "答辩"; this project is for final report + code
  submission. Use wording such as "写报告时要能讲清楚".
- Use headings, short explanations, tables, and code snippets.
- Prefer precise systems-programming explanations over motivational wording.
- Be honest about verification limits. Do not claim a smoke test proves
  something unless the script actually checks it.

## Required Content

The HTML must help the student answer:

1. Stage 03 增加了什么功能？
2. 为什么要把用户表从 `chatserver.c` 的普通全局数组迁到 POSIX shared memory？
3. 共享内存对象名是什么？Linux 上对应 `/dev/shm` 下的哪个文件？
4. `ChatUserRecord`、`ChatUserStore`、`UserStoreHandle` 各自负责什么？
5. `shm_open -> fchmod -> ftruncate -> mmap -> memset/reset -> pthread_mutex_init` 的流程各自做什么？
6. 为什么互斥锁要放在共享内存里？`PTHREAD_PROCESS_SHARED` 解决了什么问题？
7. 为什么本阶段仍然不能证明真正的并发正确性？
8. `handle_register`、`handle_login`、`handle_logout`、`handle_chat` 如何改成通过 `user_store` helper 访问用户表？
9. 为什么 FIFO 写入要尽量放在锁外？
10. 投递失败时为什么用 `user_store_mark_offline_if_fifo()`，而不是直接把目标用户置离线？
11. `cleanup()` 如何释放共享内存？为什么 `SIGTERM` 仍然走 Stage 02 的 `sig_atomic_t` 延迟清理路径？
12. Stage 03 smoke 脚本验证了什么？还有什么没有验证？

## Suggested Structure

Use this outline unless you see a better equivalent:

```text
1. 本阶段目标总览
2. 从 Stage 02 到 Stage 03 的行为变化
3. 共享内存用户表的数据结构
   3.1 ChatUserRecord
   3.2 ChatUserStore
   3.3 UserStoreHandle
4. src/user_store.c 初始化流程详解
   4.1 共享内存命名
   4.2 shm_open 与权限 0600
   4.3 fchmod 为什么仍然需要
   4.4 ftruncate 定长
   4.5 mmap(MAP_SHARED)
   4.6 每次启动 reset 的含义
   4.7 pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)
5. 加锁访问用户表的业务流程
   5.1 register
   5.2 login
   5.3 logout
   5.4 chat
   5.5 mark_offline_if_fifo 防误伤
6. chatserver.c 集成点
   6.1 移除旧全局 users/user_count
   6.2 启动时初始化 user store
   6.3 cleanup 时 shm_unlink
   6.4 server.log 增加哪些共享内存日志
7. Makefile 与 smoke-stage03.sh
   7.1 -pthread / Linux -lrt
   7.2 make smoke 指向 Stage 03
   7.3 /dev/shm 与 sudo 前置条件
   7.4 shm 文件权限检查
   7.5 register / duplicate / login / logout 验证
   7.6 SIGTERM 后 FIFO 与 shm 清理检查
8. 本阶段能证明什么，不能证明什么
9. 写报告时要能讲清楚的问题
10. 常用验证命令
11. 下一阶段衔接
```

## Important Technical Points To Explain

### Shared Memory Is Not The Wire Protocol

Explain clearly:

- `chat_common.h` is still the client/server wire protocol.
- `src/user_store.[ch]` is server-internal storage.
- Stage 03 does not change `ChatAuthRequest`, `ChatSendRequest`,
  `ChatLogoutRequest`, or `ChatPacket`.
- The client build and commands stay the same.

### Why Shared Memory Here

Be precise:

- Threads in the same process already share ordinary memory, so POSIX shared
  memory is not strictly required for Stage 04 threads alone.
- This course item asks for shared memory, and this stage makes the user table a
  kernel-managed shared object that can also be inspected through `/dev/shm` and
  extended to multi-process scenarios later.
- The mutex is still useful now because it forces all user-table access through
  a thread-safe interface before Stage 04 introduces worker threads.

### Initialization Flow

Explain this sequence:

```text
shm_open("/chatroom_lwj_users", O_CREAT | O_RDWR, 0600)
fchmod(fd, 0600)
ftruncate(fd, sizeof(ChatUserStore))
mmap(..., MAP_SHARED, fd, 0)
close(fd)
memset(shared_region, 0, sizeof(ChatUserStore))
pthread_mutexattr_init
pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)
pthread_mutex_init(&store->mutex, &attr)
magic/version/user_count initialization
```

Also explain:

- `fchmod` matters for already-existing shm objects, just like Stage 02
  `server.log`.
- Resetting the table on startup matches the old global-array behavior: server
  restart loses registered users.
- Resetting also clears a stale shm object after `kill -9`, but it does not by
  itself prove duplicate-daemon safety.

### Locking Scope

Explain that `src/user_store.c` owns locking:

- register: check duplicate + insert under lock
- login: check username/password + update online/fifo under lock
- logout: check username/fifo + set offline under lock
- chat: copy sender/target FIFO snapshot under lock, then write FIFO outside
  the lock

Explain why FIFO writes should stay outside the mutex:

- opening/writing a FIFO may block or fail
- holding the mutex during I/O would make later worker threads unnecessarily
  wait
- copying the needed state into local variables gives a small critical section

### Process-Shared Mutex

Explain accurately:

- `pthread_mutex_t` must live inside the shared memory region if another process
  is ever expected to use the same lock.
- `pthread_mutexattr_setpshared(..., PTHREAD_PROCESS_SHARED)` changes the mutex
  from process-private to process-shared.
- On the Linux euler target this should succeed and server.log should contain
  the process-shared initialization message.
- If the final code still has a fallback where process-shared failure only logs
  a warning, do not claim the mutex is definitely process-shared unless the
  smoke script or log output actually proves it.

### Smoke Script Limits

Explain that `scripts/smoke/smoke-stage03.sh` checks:

- Linux `/dev/shm` exists
- sudo is available
- daemon starts
- `/dev/shm/chatroom_lwj_users` exists with mode `600`
- four public FIFOs still exist
- first registration succeeds
- duplicate registration is rejected
- login + logout still work
- `server.log` contains shm init and user lifecycle entries
- `SIGTERM` cleans FIFO files and shm object

Also mention remaining limits:

- It does not prove real multi-client concurrent correctness.
- It does not prove worker-thread behavior because Stage 04 has not introduced
  worker threads yet.
- It does not prove online-list broadcast, one-to-many send, offline messages,
  user logs, or thread pool behavior.
- On macOS, `make smoke` cleanly skips because `/dev/shm` is absent; the real
  test should run on euler:

```bash
sudo -v
make smoke
```

## Verification

After generating the HTML, run:

```bash
xmllint --html --noout 03-walkthrough.html
```

Then run:

```bash
git status --short --ignored
git check-ignore -v 03-walkthrough.html
```

Confirm `03-walkthrough.html` is ignored and not staged.

## Output

At the end, report:

- created file path
- verification command result
- whether `03-walkthrough.html` is ignored
- whether any tracked files changed

Do not create a commit for the HTML walkthrough unless the user explicitly asks.
