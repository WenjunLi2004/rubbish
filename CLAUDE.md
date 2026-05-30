# Claude Code Project Context

This repository is the system programming course project for option 1,
feature iteration: a single-machine multi-user chat system implemented with
named pipes.

Current Git history on `main` is intentionally simple for review:

1. `baseline: original IPC chat files`
   - Original experiment files: `chat_common.h`, `chatclient.c`, `chatserver.c`.
2. `stage 01: config file + 4 FIFOs + logout skeleton`
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

Important constraints:

- Do not introduce shared memory, multi-threading, thread pools, online-list
  broadcast, one-to-many send, offline message queues, or user logs until later
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
