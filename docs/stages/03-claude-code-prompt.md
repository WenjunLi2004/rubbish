# Claude Code Prompt For Stage 03

You are Claude Code running in this repository:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

Read these files first:

- `CLAUDE.md`
- `docs/plan.md`
- `docs/protocol.md`
- `docs/stages/03-shared-memory-user-table.md`
- `chat_common.h`
- `chatserver.c`
- `chatclient.c`
- `src/config.h`
- `src/config.c`
- `Makefile`
- `scripts/smoke/smoke-stage02.sh`

Then implement Stage 03 exactly as described in:

```text
docs/stages/03-shared-memory-user-table.md
```

Keep the implementation focused:

- move the server user table into POSIX shared memory
- protect it with a `PTHREAD_PROCESS_SHARED` pthread mutex inside the shared
  memory region
- keep Stage 01 and Stage 02 behavior working
- add `scripts/smoke/smoke-stage03.sh`
- update `Makefile` so `make smoke` runs Stage 03
- add `docs/answer-notes/03.md`
- update `CLAUDE.md` to mark Stage 03 completed after the implementation works

Do not implement later-stage functionality:

- no multi-threaded request dispatch
- no thread pool
- no online-list broadcast
- no one-to-many send
- no important-friend marker
- no user logs
- no offline messages
- no protocol field changes
- no new client commands

Run:

```bash
make clean && make
bash -n scripts/smoke/smoke-stage01.sh scripts/smoke/smoke-stage02.sh scripts/smoke/smoke-stage03.sh
git diff --check
```

Run the real smoke test if the environment supports Linux `/dev/shm`,
process-shared pthread mutexes, `sudo`, and `/var/log`:

```bash
sudo -v
make smoke
```

If you are on macOS or another environment where `/dev/shm` or
`PTHREAD_PROCESS_SHARED` is unavailable, say so clearly and do not claim the
real Stage 03 smoke passed. The target environment is the euler Linux container.

Create one commit:

```text
stage 03: move user table to shared memory
```

Push it:

```bash
git push origin main
```

Do not commit `.DS_Store`.
Do not commit local HTML walkthrough files.

At the end, print:

- changed files
- key design choices
- commands run and results
- whether `make smoke` ran locally or must run on euler
- commit hash
- push result
