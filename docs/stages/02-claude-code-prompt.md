# Claude Code Prompt For Stage 02

You are Claude Code running in this repository:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

Read these files first:

- `CLAUDE.md`
- `docs/plan.md`
- `docs/stages/02-daemon-and-server-log.md`
- `chatserver.c`
- `chatclient.c`
- `chat_common.h`
- `src/config.h`
- `src/config.c`
- `Makefile`

Then implement Stage 02 exactly as described in
`docs/stages/02-daemon-and-server-log.md`.

Keep the implementation focused:

- daemonize the server
- write `/var/log/chat-logs/server/server.log`
- add Stage 02 smoke test
- add Stage 02 report notes
- preserve all Stage 01 behavior

Do not implement later-stage functionality.

Run:

```bash
make clean && make
```

Run `make smoke` if possible. If it cannot run because `sudo` or `/var/log`
access is unavailable, say so clearly.

Create one commit:

```text
stage 02: daemonize server and write server log
```

Do not commit `.DS_Store`.
Do not commit local HTML walkthrough files.

At the end, print:

- changed files
- key design choices
- commands run and results
- commit hash

