# Claude Code Project Context

This repository is the system programming course project for option 1,
feature iteration: a single-machine multi-user chat system implemented with
named pipes.

> **2026 spec alignment (current authoritative target).** The course PDF
> (`2026系统编程大作业试题.pdf`) was revised; the old 11-stage plan in
> `docs/plan.md` is **superseded**. The server now matches the new PDF directly:
> binary `chatserver_lwj_1.0`; public FIFOs `~/Server/fifo/lwj_{reg,login,msg,logout}_fifo`;
> logs `~/log/chat-logs/server/{server,threads}.log`; `POOLSIZE=100` real thread
> pool (LIFO idle stack) with main-`select` dispatch; shared-memory user table;
> online-list broadcast; one-to-one send with success count + important-friend `*`;
> offline store-and-push; chatbot manager (`/bot add|del` via `__botmgr__`).
> Obsolete staged constraints (e.g. "no thread pool before stage 9") no longer apply.
> Verify with `make smoke` (= `scripts/smoke/smoke-2026.sh`, needs Linux `/dev/shm`);
> see `scripts/demo/2026-demo.md` and `docs/answer-notes/2026-compliance.md`.

Git history on `main` (stages 01–03 were the old plan; commit 6 is the 2026 rework):

1. `baseline: original IPC chat files`
   - Original experiment files: `chat_common.h`, `chatclient.c`, `chatserver.c`.
2. `stage 01: config file + 4 FIFOs + logout skeleton`
3. `stage 02: daemonize server and write server log`
4. `fix stage 02 daemon log and smoke robustness`
5. `stage 03: move user table to shared memory`
6. `align chat system with 2026 assignment spec`
   - Current implementation (thread pool + online list + offline messages + bots).

Stage 01 completed:

- Configuration file loading via `config/chatserver.conf` and `src/config.[ch]`.
- Four public FIFOs derived from `fifo_prefix=lwj`:
  - `lwjregister`
  - `lwjlogin`
  - `lwjsendmsg`
  - `lwjlogout`
- Client command `/logout`.
- `ChatLogoutRequest`.
- Server-side `handle_logout()` stub: checks username + fifo, sets `online=0`,
  replies `logout ok`.
- Build system via `Makefile`.
- Stage 01 smoke script.

Stage 02 completed:

- Server daemonization: `fork`, `setsid`, signal setup, `chdir("/")`,
  `umask(0)`, and standard streams redirected to `/dev/null`.
- Structured server logging to `/var/log/chat-logs/server/server.log`.
- `server.log` is opened append-only and forced to mode `0600` with
  `fchmod`, including the case where the file already existed with looser
  permissions.
- `SIGINT` / `SIGTERM` handling uses `volatile sig_atomic_t` flags; real log
  output and FIFO cleanup run later in the main loop.

Stage 03 completed:

- User table moved from `chatserver.c` globals into a POSIX shared memory object
  (`/chatroom_lwj_users`, i.e. `/dev/shm/chatroom_lwj_users` on Linux), via new
  files `src/user_store.[ch]`.
- Init path: `shm_open(O_CREAT|O_RDWR,0600)` → `fchmod(0600)` (with an `fstat`
  fallback where shm `fchmod` is unsupported, e.g. macOS) → `ftruncate` →
  `mmap(MAP_SHARED)` → init an in-region `pthread_mutex_t` with
  `PTHREAD_PROCESS_SHARED`; object is reset on each server start.
- The shared mutex protects `user_count` and `users[]`; register/login/logout/
  chat go through `src/user_store.c` locking helpers, with FIFO writes kept
  outside the lock. Reply texts unchanged (`register ok`, `login ok`,
  `logout ok`, `username already exists`, `target user is not online`, ...).
- Normal shutdown unmaps and `shm_unlink`s the object via the Stage 02
  `sig_atomic_t` cleanup path.
- Server links with `-pthread` (and `-lrt` on Linux); the client build is
  unchanged.
- `make smoke` currently runs Stage 03; `smoke-stage01`, `smoke-stage02`, and
  `smoke-stage03` remain available as explicit targets. The real Stage 03 smoke
  needs Linux `/dev/shm` + process-shared mutexes + `sudo` + `/var/log` and runs
  on the euler container; on macOS it cleanly SKIPs (no `/dev/shm`).

2026 rework completed (`align chat system with 2026 assignment spec`):

- Config/paths/binary: `chatserver_lwj_1.0`; `config/chatserver.conf` now uses
  `fifo_dir` / `client_fifo_dir` / `log_dir` / `poolsize=100`. FIFOs are
  `<fifo_dir>/lwj_{reg,login,msg,logout}_fifo`; logs are
  `<log_dir>/server/{server,threads}.log` (both `0600`, root-owned under sudo).
- Thread pool `src/thread_pool.[ch]`: exactly `poolsize` workers, LIFO idle
  stack ("just-recycled worker is dispatched first"), non-blocking dispatch.
  Main thread only does `select` + read + dispatch; workers run business logic.
- Logging is thread-safe (one mutex, whole-line `write`); `threads.log` records
  `dispatch ... state=busy` / `recycle ... state=idle` timestamps.
- Protocol (`chat_common.h`): `ChatPacket` gained `timestamp/send_count/online_count`
  and packet types `ONLINE_LIST/OFFLINE_PUSH/SYSTEM`; `CHAT_TEXT_LEN` is 512.
  `ChatSendRequest` to `__botmgr__` is the bot-manager channel (no 5th FIFO).
- User store (`src/user_store.[ch]`): `is_bot/login_time/logout_time`,
  `send_count[][]`, offline queue, online-list/broadcast snapshots, bot helpers.
- Business: online-list broadcast on login/logout, one-to-one send with success
  count and important-friend `*` (>5), offline store-and-push (original
  timestamp), chatbot manager. Bots keep no offline queue (random names).
- `make smoke` = `scripts/smoke/smoke-2026.sh` (runs with or without sudo since
  logs are in `~/log`); legacy `smoke-stage01/02/03` targets are kept but were
  written for the old config and are superseded.

Important constraints:

- Do not commit `.DS_Store`.
- Do not commit local walkthrough HTML files.
- Do not commit `2026系统编程大作业试题.pdf` unless explicitly asked.
- Keep the protocol based on named pipes, not sockets.
- Keep `select()` as the main-thread multiplexing primitive.
- All shared user-table access goes through the shared-memory mutex; copy data
  for FIFO writes into local snapshots and do FIFO I/O outside the lock.

When implementing a stage:

- Read `docs/plan.md` first.
- Read the corresponding `docs/stages/NN-*.md` brief.
- Keep changes scoped to that stage.
- Run at least `make clean && make`.
- Run the stage smoke test if the local environment allows it.
- Commit only relevant files.

Workflow memory:

- Prompts for Claude Code should be written as local repository files first.
- After writing a prompt file, give the user the exact short instruction to paste
  into Claude Code, for example `执行 docs/stages/<prompt-file>.md`.
- Codex reviews Claude Code's changes after Claude finishes.
