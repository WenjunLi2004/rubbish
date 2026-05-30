# Claude Code Prompt: Fix Stage 02 Review Issues

You are Claude Code running in:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

Read first:

- `CLAUDE.md`
- `docs/stages/02-daemon-and-server-log.md`
- `chatserver.c`
- `scripts/smoke/smoke-stage02.sh`
- `Makefile`

Then fix the Stage 02 review issues below. Keep the fix focused. Do not add
later-stage features.

## Issues To Fix

### 1. Ensure `server.log` mode is always `0600`

Current implementation opens `cfg->server_log_path` with mode `0600`, but that
mode only applies when the file is newly created. If `server.log` already exists
with a looser mode such as `0644`, it stays loose.

Required change:

- After `open()` succeeds in `open_server_log()`, call `fchmod(g_log_fd, 0600)`.
- If `fchmod` fails, log the error and fail startup.
- Keep the file root-owned when server is launched with `sudo`.
- Update `scripts/smoke/smoke-stage02.sh` so log mode not equal to `600` is a
  failure, not just a warning.

### 2. Make signal handling safer

Current `handle_signal()` directly calls `log_message()` and `cleanup()`, which
are not async-signal-safe enough for a strict systems-programming explanation.

Required change:

- Add a global `volatile sig_atomic_t g_shutdown_requested = 0;`
- Make `handle_signal(int sig)` only record the signal number and set the
  shutdown flag.
- In the main loop, when `select()` returns `-1` with `errno == EINTR`, check the
  shutdown flag. If shutdown was requested, log the signal, clean up FIFOs, and
  exit normally.
- Also check the shutdown flag after successful select iterations, so shutdown
  is handled promptly.
- Preserve behavior: SIGTERM / SIGINT should still clean up all 4 public FIFOs.
- Keep SIGHUP and SIGPIPE ignored.

### 3. Make Stage 02 smoke process matching narrower

Current smoke uses:

```bash
pgrep -f "bin/chatserver_${SHORT}_${VER}"
```

This can match too broadly if multiple copies of the same project are running.

Required change:

- Match daemon processes by both the absolute server path `$SERVER` and the
  absolute config path `$CONF`.
- Prefer a helper function in the smoke script, e.g. `find_server_pids()`, based
  on `ps -axo pid,args` and `awk`/shell filtering.
- Use that helper both for stale-process cleanup and for locating the newly
  daemonized server.
- Avoid killing unrelated chatserver processes.

### 4. Reduce smoke race around client replies

Current smoke pipes commands with short fixed sleeps such as `sleep 0.4`. On a
busy machine, the client can run `/quit` and unlink its private FIFO before the
server replies.

Required change:

- Make the sleeps more conservative, at least `sleep 1`, or implement a more
  robust helper that waits for expected output.
- The simple conservative sleep is acceptable for this stage.

### 5. Clean documentation whitespace warnings

`git diff --check` / `git show --check` reported extra blank lines at EOF in:

- `docs/stages/02-claude-code-prompt.md`
- `docs/stages/02-daemon-and-server-log.md`

Required change:

- Remove the extra blank lines at EOF.
- Ensure `git diff --check HEAD~1..HEAD` or at least `git diff --check` is clean
  after your new changes.

## Verification

Run:

```bash
make clean && make
bash -n scripts/smoke/smoke-stage01.sh scripts/smoke/smoke-stage02.sh
git diff --check
```

If possible locally, also run a `/tmp`-based daemon sanity check without sudo.
If not, explain why.

Do not run destructive commands. Do not commit `.DS_Store`.
Do not commit local HTML walkthrough files.

## Commit

Create one commit on top of the current Stage 02 commit. Include:

- the Stage 02 code/smoke/doc fixes
- `CLAUDE.md` if it is still untracked or modified
- this prompt file, `docs/stages/02-review-fix-prompt.md`

Exclude:

- `.DS_Store`
- local walkthrough HTML files

Commit message:

```text
fix stage 02 daemon log and smoke robustness
```

After the commit succeeds, push to `main`:

```bash
git push origin main
```

At the end, report:

- changed files
- summary of fixes
- commands run and results
- commit hash
- push result
