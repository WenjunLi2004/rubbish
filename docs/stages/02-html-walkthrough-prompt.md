# Claude Code Prompt: Generate Stage 02 HTML Walkthrough

You are Claude Code running in:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

Your task is documentation only. Do not change C source, scripts, Makefile, or
Git history.

Create a local HTML walkthrough file:

```text
02-walkthrough.html
```

This HTML is for the student to understand Stage 02. It is intentionally local
and ignored by Git via `*.html`; do not force-add it and do not commit it.

## Read First

Read these files before writing the HTML:

- `01-walkthrough.html` if it exists locally, as a visual/style reference.
- `CLAUDE.md`
- `docs/plan.md`
- `docs/stages/02-daemon-and-server-log.md`
- `docs/stages/02-review-fix-prompt.md`
- `docs/answer-notes/02.md`
- `chatserver.c`
- `scripts/smoke/smoke-stage02.sh`
- `Makefile`

Also inspect the Stage 02 commits:

```bash
git log --oneline --decorate -5
git diff ab7abb8..32db261 --stat
git diff ab7abb8..32db261 -- chatserver.c Makefile scripts/smoke/smoke-stage02.sh docs/answer-notes/02.md
```

Stage 02 consists of:

- `6c25647 stage 02: daemonize server and write server log`
- `32db261 fix stage 02 daemon log and smoke robustness`

The final walkthrough should explain the combined final state after both
commits, not only the first commit.

## Style Rules

- Use Chinese.
- Do not use "AI 辅助", "基线", "注意", or badge labels.
- Do not include an "AI 辅助代码索引".
- Do not talk about "答辩"; this project is for final report + code
  submission. Use wording such as "写报告时要能讲清楚".
- Keep the HTML self-contained with inline CSS.
- Follow the clean, readable style of `01-walkthrough.html`, but simplify where
  useful.
- Use headings, short explanations, tables, and code snippets.
- Keep explanations accurate and honest: if something is only a smoke test
  guarantee, say so.

## Required Content

The HTML must help the student answer:

1. Stage 02 增加了什么功能？
2. 为什么要把 server 做成 daemon？
3. daemon 化之后为什么不能再依赖 `printf` / `perror`？
4. `server.log` 写到哪里，权限为什么是 `0600`？
5. 为什么配置要在 fork/chdir 之前加载？
6. `fork -> setsid -> signal setup -> chdir("/") -> redirect stdio` 的流程各自做什么？
7. 为什么 `umask(0)` 仍然要保留，而且必须在 `mkfifo` 前？
8. 为什么 `open(..., 0600)` 不足以保证已有日志文件权限？为什么还需要 `fchmod`？
9. 为什么 signal handler 不能直接写日志和 cleanup？`sig_atomic_t` 标志位方案怎么工作？
10. Stage 02 smoke 脚本验证了什么？还有什么没验证？

## Suggested Structure

Use this outline unless you see a better equivalent:

```text
1. 本阶段目标总览
2. 从 Stage 01 到 Stage 02 的行为变化
3. chatserver.c 修改详解
   3.1 日志句柄与 log_message()
   3.2 open_server_log()：目录、文件、0600、fchmod
   3.3 daemon 化流程
   3.4 标准流重定向到 /dev/null
   3.5 signal handler 与 shutdown_if_requested()
   3.6 启动顺序为什么这样排
   3.7 原有注册/登录/聊天/logout 如何改为写日志
4. Makefile 与 smoke-stage02.sh
   4.1 make smoke 指向 Stage 02
   4.2 sudo 与 /var/log
   4.3 如何定位 daemon pid
   4.4 register/login/logout 自动验证
   4.5 server.log 内容和权限检查
   4.6 SIGTERM 后 FIFO 清理检查
5. 本阶段能证明什么，不能证明什么
6. 写报告时要能讲清楚的问题
7. 常用验证命令
8. 下一阶段衔接
```

## Important Technical Points To Explain

### Daemon Startup Order

Explain why the final order is:

```text
load config
fork + setsid
install signal handlers
chdir("/")
umask(0)
open server.log
redirect stdin/stdout/stderr
create data/FIFO tree
open 4 public FIFOs
select loop
```

Especially explain:

- relative config path must be read before `chdir("/")`
- log path is absolute, so it still works after `chdir("/")`
- stdio is redirected only after log is ready
- `umask(0)` keeps FIFO mode `0666` from becoming `0644`

### Logging

Explain:

- `g_log_fd`
- `log_message(level, fmt, ...)`
- timestamp + pid + level format
- why log lines are written with `write()`
- what events are logged:
  - startup
  - effective config
  - FIFO paths
  - struct sizes
  - register/login/logout/chat
  - errors
  - signal shutdown

### `fchmod`

Explain this subtle point clearly:

```c
open(path, O_CREAT | O_APPEND | O_WRONLY, 0600)
```

The `0600` mode only applies when the file is newly created. If the file already
exists with `0644`, `open()` does not change it. Therefore Stage 02 fix commit
adds:

```c
fchmod(g_log_fd, 0600)
```

### Safer Signal Handling

Explain why the first Stage 02 implementation was improved:

- direct `log_message()` / `cleanup()` in a signal handler is not a clean systems
  programming style
- the final implementation uses:

```c
static volatile sig_atomic_t g_shutdown_requested;
static volatile sig_atomic_t g_shutdown_signal;
```

- `handle_signal()` only sets flags
- main loop checks flags after `select()` is interrupted by `EINTR` and after
  normal `select()` return

### Smoke Script

Explain that `make smoke` now runs Stage 02:

```make
smoke: smoke-stage02
```

Explain why the script:

- requires `sudo`
- starts server with `sudo`
- cannot use `$!` because the parent exits during daemonization
- finds pid by matching both absolute `$SERVER` and `$CONF`
- checks FIFO file type
- runs clients as normal user
- checks `server.log` mode and content
- sends `SIGTERM` and checks FIFO cleanup

Also mention remaining limits:

- smoke does not prove multi-client concurrent correctness
- smoke does not prove long-message truncation behavior
- smoke does not cover later features like shared memory, thread pool, online
  list broadcast, user logs, offline messages
- real `make smoke` should be run on the euler container with:

```bash
sudo -v
make smoke
```

## Verification

After generating the HTML, run:

```bash
xmllint --html --noout 02-walkthrough.html
```

If `xmllint` reports only harmless HTML5 warnings, fix them if easy; otherwise
explain. The goal is a clean parse.

Also run:

```bash
git status --short
```

Confirm `02-walkthrough.html` is ignored and not staged.

## Output

At the end, report:

- created file path
- verification command result
- whether any tracked files changed

Do not create a commit for the HTML walkthrough unless the user explicitly asks.
