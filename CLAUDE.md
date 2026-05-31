# Claude Code Project Context

This repository is the system programming course project for option 1,
feature iteration: a single-machine multi-user chat system implemented with
named pipes.

Current Git history on `main` is intentionally simple for review:

1. `baseline: original IPC chat files`
   - Original experiment files: `chat_common.h`, `chatclient.c`, `chatserver.c`.
2. `stage 01: config file + 4 FIFOs + logout skeleton`
3. `stage 02: daemonize server and write server log`
4. `fix stage 02 daemon log and smoke robustness`
5. `stage 03: move user table to shared memory`
   - Current implementation.

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

Important constraints:

- Do not introduce multi-threading, thread pools, online-list broadcast,
  one-to-many send, offline message queues, or user logs before their planned
  stages.
- Do not commit `.DS_Store`.
- Do not commit local walkthrough HTML files.
- Keep the protocol based on named pipes, not sockets.
- Keep `select()` as the multiplexing primitive for now.
- Existing Stage 01 behavior must keep working after each new stage.

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
