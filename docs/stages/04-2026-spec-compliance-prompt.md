# Claude Code Prompt: Align Current Chat System With 2026 Assignment Spec

You are Claude Code running in this repository:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

The course assignment PDF has changed. The current code is at Stage 03
(`ddf9d4b stage 03: move user table to shared memory`), but the old staged plan
is no longer aligned with the new PDF.

Read these files first:

- `2026系统编程大作业试题.pdf`
- `CLAUDE.md`
- `docs/plan.md`
- `docs/protocol.md`
- `config/chatserver.conf`
- `chat_common.h`
- `chatserver.c`
- `chatclient.c`
- `src/config.h`
- `src/config.c`
- `src/user_store.h`
- `src/user_store.c`
- `Makefile`
- `scripts/smoke/smoke-stage03.sh`

If PDF text extraction is needed, use Python or another local tool. Do not
commit the PDF unless explicitly asked.

## New Spec Summary: Option 1

We still choose 选题(一) “功能迭代”: single-machine multi-user chat.

The new PDF requires the final system to demonstrate:

1. Config-driven multi-threaded daemon server.
2. Well-known FIFOs under `FIFOFILES=~/Server/fifo/`.
3. Public FIFO names:
   - `lwj_reg_fifo`
   - `lwj_login_fifo`
   - `lwj_msg_fifo`
   - `lwj_logout_fifo`
4. Server name format:
   - `chatserver_学生姓名拼音首字母缩写_x.y`
   - for this project: `chatserver_lwj_1.0`
   - `x` odd means development version; `y` is bug-fix count.
5. Logs under `LOGFILES=~/log/chat-logs/`:
   - server log: `~/log/chat-logs/server/server.log`
   - thread-pool log: `~/log/chat-logs/server/threads.log`
   - mode `0600`, root-owned when server runs with `sudo`
6. `POOLSIZE=100`; server creates 100 worker threads at startup.
7. Startup logs include startup time and thread-pool construction success/failure.
8. Main thread uses multiplexing (`select`, `poll`, or `epoll`) to listen for:
   register, login, chat, logout.
9. When a request arrives:
   - log request arrival
   - obtain an idle thread from the pool
   - mark it busy
   - worker handles request
   - worker returns result to user
   - worker logs completion and becomes idle again
10. Thread pool management must show:
    - data structure design
    - idle/busy state
    - dispatch and recycle
    - “刚回收的线程倾向于下一次被分派”, so use an idle stack / LIFO
    - dispatch/recycle timestamps in `~/log/chat-logs/server/threads.log`
11. Register:
    - unique username/password
    - duplicate username rejected
    - required demo usernames: `liwenjun_1`, `liwenjun_2`, duplicate `liwenjun_2`
12. Login:
    - required demo usernames: `liwenjun_1`, `liwenjun_2`, `liwenjun_3`
    - successful login gets online total and online username list
    - failed login gets failure reply
13. Message sending:
    - `liwenjun_1` sends `How are you, liwenjun_2` to `liwenjun_2` five times
    - successful sends are logged as `(sender, receiver, time, sent)`
    - once successful sends exceed 5, append `*` to the target username when
      displaying that important friend relationship
14. Logout/offline:
    - when `liwenjun_2` logs out, remaining online users see logout info and
      remaining online list
    - while `liwenjun_2` is offline, `liwenjun_1` sends
      `Hi, let's play badminton?`
    - server stores it as pending, logs `(sender, receiver, time, pending)`
    - when `liwenjun_2` logs in again, server pushes the stored message with
      original send time, then logs it as sent
15. Server log final screenshot should include all register/login/send/logout/
    offline-push/thread-management activity.
16. Chatbot manager:
    - dynamically add `x` robot users
    - each robot registers and logs in like a normal user with random username
      and password
    - `liwenjun_1` can see online robot users
    - when `liwenjun_1` sends a message to a robot, the robot replies:
      `幸会，liwenjun_1，很高兴认识您`
    - dynamically reduce `x` robot users by randomly selecting online robots
      and sending logout requests
    - online users can see the robot logout info
    - robot register/login/message/logout events are logged
    - because robot names are random and may never return, do not keep offline
      messages for offline robot users; document and log discarded robot-pending
      cases clearly

The PDF has one path inconsistency: it defines `LOGFILES=~/log/chat-logs/`, but
one shell command example uses `cat /var/log/chat-logs/server/server.log`.
Prefer the explicit `LOGFILES=~/log/chat-logs/` requirement. If useful, add a
small optional setup script or documentation note explaining how to make a
root-owned symlink from `/var/log/chat-logs` to `~/log/chat-logs`, but do not
silently depend on that symlink for correctness.

## Current Code Gaps

The current code already has:

- config loading
- four public FIFOs
- daemonization
- `server.log`
- POSIX shared-memory user table
- process-shared mutex attempt
- register/login/logout/send basics

But it is not yet compliant with the new PDF because:

- binary name is `chatserver_lwj_1.0.0`, not `chatserver_lwj_1.0`
- FIFO dir and names do not match `~/Server/fifo/lwj_*_fifo`
- log dir currently defaults to `/var/log/chat-logs`, not `~/log/chat-logs`
- `poolsize` is 10 and no real thread pool exists
- no `threads.log`
- request handling is still in the main thread
- no online-list packet/broadcast
- no offline message queue/push
- no successful-send counter or important-friend `*`
- no chatbot manager
- smoke scripts still target the old staged requirements
- docs still describe the old 11-stage plan

## Implementation Strategy

This is a large spec alignment. Keep the work coherent, but it is acceptable to
make more than one commit if needed. Prefer this order:

1. Align config, paths, binary name, and smoke/demo scripts with the new PDF.
2. Add thread-safe logging for both `server.log` and `threads.log`.
3. Add a real fixed-size thread pool using `POOLSIZE=100` and LIFO idle stack.
4. Dispatch register/login/chat/logout requests from main `select()` loop to
   worker threads.
5. Extend shared-memory user store to support online lists, send counts, offline
   messages, and robot flags.
6. Extend the wire protocol and client display for online lists, offline pushes,
   timestamps, and send counts.
7. Implement logout broadcast and offline message push.
8. Implement chatbot manager.
9. Update docs, answer notes, and smoke tests for the new PDF.

Do not preserve obsolete staged constraints such as “do not implement
thread-pool until Stage 09”. The new PDF requires the thread pool now.

## Required Code Changes

### 1. Config, Paths, And Binary Name

Update `config/chatserver.conf`:

```ini
server_name = chatserver
short_name  = lwj
version     = 1.0

fifo_dir        = /home/liwenjun2023150001/Server/fifo
client_fifo_dir = /home/liwenjun2023150001/Client/fifo
log_dir         = /home/liwenjun2023150001/log/chat-logs
fifo_prefix     = lwj
poolsize        = 100
```

`client_fifo_dir` is not explicitly named in the PDF, but clients still need a
directory for their private FIFOs. Keep it config-driven and document the choice.

Update `src/config.[ch]`:

- parse `fifo_dir` and `client_fifo_dir` directly
- keep backward compatibility only if easy; new config is authoritative
- derive:
  - `full_name = chatserver_lwj_1.0`
  - `fifo_register = <fifo_dir>/lwj_reg_fifo`
  - `fifo_login    = <fifo_dir>/lwj_login_fifo`
  - `fifo_message  = <fifo_dir>/lwj_msg_fifo`
  - `fifo_logout   = <fifo_dir>/lwj_logout_fifo`
  - `log_dir_server = <log_dir>/server`
  - `server_log_path = <log_dir>/server/server.log`
  - `threads_log_path = <log_dir>/server/threads.log`

Update `Makefile` so the server binary becomes:

```text
bin/chatserver_lwj_1.0
```

Do not rename the client unless needed.

### 2. Logs And Permissions

Keep server daemonized and started with `sudo`.

Open:

```text
~/log/chat-logs/server/server.log
~/log/chat-logs/server/threads.log
```

Both must be:

- created with `O_CREAT | O_APPEND | O_WRONLY`
- forced to mode `0600` with `fchmod`
- root-owned when launched with `sudo`

Because Stage 04 introduces worker threads, make logging thread-safe:

- either protect `log_message()` and `thread_log_message()` with a pthread mutex
- or guarantee each line is written with one atomic `write` under a lock
- avoid interleaved log lines

Server startup log must include:

- startup time
- effective config
- FIFO paths
- `POOLSIZE`
- thread pool creation success, e.g. `thread pool created: 100 threads`
- shared-memory initialization and process-shared mutex result

If `pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)` fails on Linux/euler,
startup should fail. It may be a documented skip/fallback on macOS only, but the
target environment must not silently run without a process-shared mutex.

### 3. Thread Pool

Add new files, for example:

```text
src/thread_pool.h
src/thread_pool.c
```

Required behavior:

- create exactly `cfg->poolsize` worker threads at startup (`100` from config)
- each worker has:
  - index/id
  - pthread id
  - state: idle or busy
  - one assigned job slot
  - condition variable or equivalent wakeup
- maintain idle workers in a stack so the last recycled worker is dispatched
  first (LIFO)
- main thread dispatches jobs to idle workers
- when assigned:
  - worker state becomes busy
  - write `threads.log`: timestamp, worker index, pthread id, request type,
    requester username if available, state `busy`
- when finished:
  - worker state becomes idle
  - push it back onto idle stack
  - write `threads.log`: timestamp, worker index, request type, state `idle`
- on shutdown:
  - notify all workers
  - join them
  - log clean shutdown

Keep the main thread responsible for `select()` and FIFO draining. Once a full
request struct is read, package it as a job and dispatch it. Do not do business
logic in the main thread except basic read/dispatch/error handling.

If no idle thread is available:

- log the event
- reply `server busy` when a reply FIFO is known
- do not block the main event loop indefinitely

### 4. Protocol Extension

Extend `chat_common.h` carefully. Existing fields may stay in place; append new
fields where possible.

Suggested packet types:

```c
typedef enum {
    CHAT_PACKET_REPLY        = 1,
    CHAT_PACKET_MESSAGE      = 2,
    CHAT_PACKET_ONLINE_LIST  = 3,
    CHAT_PACKET_OFFLINE_PUSH = 4,
    CHAT_PACKET_SYSTEM       = 5
} ChatPacketType;
```

Suggested `ChatPacket` final fields:

```c
typedef struct {
    int  type;
    int  ok;
    char from[CHAT_NAME_LEN];
    char message[CHAT_TEXT_LEN];
    long timestamp;
    int  send_count;
    int  online_count;
} ChatPacket;
```

Full rebuild of server and client is fine.

Update `chatclient.c`:

- print `REPLY`
- print `MESSAGE`
- print `ONLINE_LIST` as online total and names
- print `OFFLINE_PUSH` with original timestamp
- print `SYSTEM` for logout broadcasts / robot events if used
- keep `/register`, `/login`, `/logout`, `/send`, `/quit`
- add:
  - `/bot add <x>`
  - `/bot del <x>`

For chatbot manager, prefer using the existing message FIFO with a reserved
target such as `__botmgr__` instead of adding a fifth public FIFO. The PDF
enumerates exactly four public FIFOs; avoid adding a fifth unless you document
why.

### 5. Shared User Store Extension

Extend `src/user_store.[ch]` while keeping the shared-memory mutex.

Suggested additions:

```c
#define CHAT_MAX_OFFLINE_MESSAGES 256
#define CHAT_MAX_BOTS 64

typedef struct {
    int used;
    char from[CHAT_NAME_LEN];
    char to[CHAT_NAME_LEN];
    char text[CHAT_TEXT_LEN];
    long timestamp;
} ChatOfflineMessage;
```

Extend `ChatUserRecord` with fields such as:

- `int is_bot`
- `long login_time`
- `long logout_time`

Extend `ChatUserStore` with:

- `int send_count[CHAT_MAX_USERS][CHAT_MAX_USERS]`
- `ChatOfflineMessage offline_messages[CHAT_MAX_OFFLINE_MESSAGES]`
- bot counter/random seed metadata if useful

Add helpers:

- register user / duplicate rejection
- login user and return online list snapshot
- logout user and return online list snapshot
- build online list string for a viewer
- broadcast helper can live in `chatserver.c`, but store should provide safe
  snapshots of online user FIFOs
- prepare send:
  - sender online?
  - target exists?
  - target online?
  - target bot?
  - copy FIFOs and current send count
- increment send count on successful sent delivery
- store pending offline message for non-bot offline users
- retrieve and clear pending offline messages for a user on login
- mark important friend after successful sends exceed 5
- create random bot users and mark them online
- choose random online bot users and log them out

All shared state access must be under the shared mutex. Copy data needed for
FIFO writes into local arrays/snapshots and do FIFO I/O outside the store lock.

### 6. Business Behavior

#### Register

- reject empty username/password
- reject duplicate username
- on success log `(username, register, time)`
- reply `register ok`

#### Login

- verify username/password
- set online and update fifo
- reply with success plus online total and names
- broadcast updated online list to all online users
- push pending offline messages to this user, preserving original timestamp
- after each successful offline push, log `(sender, receiver, time, sent)`

#### Logout

- verify username and fifo
- set offline and record logout time
- reply `logout ok`
- broadcast logout info and remaining online list to online users
- log `(username, logout, time)`

#### Send To Human User

- if target online:
  - deliver message
  - increment sender→target send count
  - log `(sender, receiver, time, sent)`
  - reply success to sender
  - when successful sends exceed 5, display target as `<target>*` where useful
    for the sender, such as ack text and online list
- if target offline:
  - store pending offline message
  - log `(sender, receiver, time, pending)`
  - reply that message is pending
- if target missing:
  - reply target does not exist

#### Send To Bot

- if target is an online bot:
  - log sender→bot sent
  - immediately send a bot reply to sender:
    `幸会，<sender>，很高兴认识您`
  - log bot→sender sent
- if bot is offline or missing, do not store offline messages for it; reply and
  log as discarded/unavailable.

### 7. Chatbot Manager

Implement `/bot add <x>` and `/bot del <x>` in the client.

Suggested protocol:

- client sends a `ChatSendRequest` to `cfg.fifo_message`
- `from = current username`
- `to = "__botmgr__"`
- `text = "add <x>"` or `"del <x>"`

Server recognizes `to == "__botmgr__"` and handles it as a bot-manager request.

Add bots:

- generate random unique username, e.g. `lwj_bot_<pid>_<seq>` or similar
- generate random password
- insert into user store as `is_bot=1`, `online=1`
- log register and login events
- broadcast online list
- reply with created bot usernames

Delete bots:

- randomly choose up to `x` online bots
- mark them offline or remove them from online list
- log logout events
- broadcast robot logout info and updated online list
- reply with deleted bot usernames

Document the design choice: robots are server-managed users; no persistent
offline queue is kept for robot users because random robot names may never come
back.

### 8. Smoke And Demo Scripts

Add or replace current default smoke with:

```text
scripts/smoke/smoke-2026.sh
```

Update `Makefile`:

```make
smoke: smoke-2026
smoke-2026: all
        scripts/smoke/smoke-2026.sh
```

Keep old smoke targets only if they still work or clearly mark them as legacy.

The new smoke should run on euler Linux with sudo and verify:

1. build succeeds
2. server binary name is `bin/chatserver_lwj_1.0`
3. server starts as daemon
4. `ps` shows daemon status suitable for screenshots
5. public FIFOs exist:
   - `~/Server/fifo/lwj_reg_fifo`
   - `~/Server/fifo/lwj_login_fifo`
   - `~/Server/fifo/lwj_msg_fifo`
   - `~/Server/fifo/lwj_logout_fifo`
6. `server.log` exists, mode `600`
7. `threads.log` exists, mode `600`
8. server.log contains startup time and thread pool created with 100 threads
9. threads.log contains dispatch and recycle lines
10. register `liwenjun_1`, `liwenjun_2`, duplicate `liwenjun_2`
11. login `liwenjun_1`, `liwenjun_2`, failed login `liwenjun_3`
12. login reply contains online count and online names
13. `liwenjun_1` sends `How are you, liwenjun_2` to `liwenjun_2` five times
14. logout `liwenjun_2`, verify `liwenjun_1` sees logout/online-list update
15. send offline message `Hi, let's play badminton?` from `liwenjun_1` to
    `liwenjun_2`
16. login `liwenjun_2` again and verify the offline message is pushed with
    original timestamp
17. `/bot add 2`, verify online list includes bots
18. send a message from `liwenjun_1` to one bot and verify reply:
    `幸会，liwenjun_1，很高兴认识您`
19. `/bot del 1`, verify robot logout info is visible
20. `SIGTERM` server; verify FIFO cleanup and clean thread-pool shutdown log

If a full automated smoke is too fragile for interactive terminal behavior,
also add a `scripts/demo/2026-demo.md` or shell helper describing exact terminal
commands for screenshots. But `make smoke` should still verify the core path.

### 9. Documentation Updates

Update:

- `docs/plan.md`
- `docs/protocol.md`
- `CLAUDE.md`
- `docs/answer-notes/04.md` or `docs/answer-notes/2026-compliance.md`

The docs must state that the old staged plan is superseded by the new PDF.

Document the AIGC prompt trail for the report:

- keep this prompt file in `docs/stages/`
- do not label walkthrough HTML with “AI 辅助”, but preserve prompts for the
  report requirement “AIGC 代码片段必须附上 prompts”

Do not commit local HTML walkthrough files.
Do not commit `.DS_Store`.
Do not commit `2026系统编程大作业试题.pdf` unless the user explicitly asks.

## Verification

At minimum run locally:

```bash
make clean && make
bash -n scripts/smoke/*.sh
git diff --check
```

On euler Linux, run:

```bash
sudo -v
make smoke
```

Also collect concise output for the final report:

```bash
ps -ef | grep chatserver_lwj_1.0
ls -l /home/liwenjun2023150001/Server/fifo
ls -l /home/liwenjun2023150001/log/chat-logs/server/server.log
ls -l /home/liwenjun2023150001/log/chat-logs/server/threads.log
sudo cat /home/liwenjun2023150001/log/chat-logs/server/server.log | tail -n 80
sudo cat /home/liwenjun2023150001/log/chat-logs/server/threads.log | tail -n 80
```

If you choose to support the PDF's inconsistent `/var/log/chat-logs` example via
a symlink, document exactly how and do not hide it in code.

## Commit And Push

Because this is a large spec alignment, you may create multiple commits if that
makes review easier, for example:

```text
align config paths with 2026 assignment spec
add thread pool request dispatch
add online list offline messages and bot manager
update 2026 smoke and report notes
```

Or create one commit if the final diff is still reviewable:

```text
align chat system with 2026 assignment spec
```

Push to GitHub:

```bash
git push origin main
```

At the end, print:

- changed files
- key design decisions
- known deviations or ambiguous PDF points
- commands run and results
- whether full `make smoke` ran on euler
- commit hash(es)
- push result
