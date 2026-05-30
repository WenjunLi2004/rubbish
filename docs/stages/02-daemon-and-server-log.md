# Stage 02: Daemon Server + `server.log`

## Goal

Turn the Stage 01 foreground chat server into a background daemon and write
server lifecycle logs to:

```text
/var/log/chat-logs/server/server.log
```

This stage should not add shared memory, multi-threading, thread pools,
online-list broadcast, offline messages, one-to-many send, or user logs.

## Existing Stage 01 Behavior To Preserve

- `chatserver_lwj_1.0.0 <config-file>` loads `config/chatserver.conf`.
- The server creates and listens on four public FIFOs:
  - `lwjregister`
  - `lwjlogin`
  - `lwjsendmsg`
  - `lwjlogout`
- Client commands continue to work:
  - `/register`
  - `/login`
  - `/send <user> <text>`
  - `/logout`
  - `/quit`
- `SIGTERM` / `SIGINT` should still clean up public FIFOs.

## Implementation Requirements

### `chatserver.c`

1. Keep the server CLI signature unchanged:

   ```bash
   ./bin/chatserver_lwj_1.0.0 <config-file>
   ```

2. Load the config before daemonizing.

   A relative config path such as `config/chatserver.conf` only works before
   daemonization because the daemon should later `chdir("/")`.

3. Create log directories from `ChatConfig`:

   - `cfg->log_dir`
   - `cfg->log_dir_server`

4. Open `cfg->server_log_path`:

   - flags: `O_CREAT | O_APPEND | O_WRONLY`
   - mode: `0600`
   - owner should remain root when launched with `sudo`.

5. Add a small logging helper, for example:

   ```c
   static void log_message(const char *level, const char *fmt, ...);
   ```

   Log line format should include at least:

   - timestamp
   - pid
   - level
   - message

   Example:

   ```text
   2026-05-30 14:20:31 [pid=12345] [INFO] server starting
   ```

6. Daemonize using the classic steps:

   - `fork()`, parent exits
   - `setsid()`
   - reasonable signal setup
   - `chdir("/")`
   - `umask(0)` before creating FIFOs
   - redirect `stdin`, `stdout`, `stderr` to `/dev/null` or log-safe fds

7. Signal handling:

   - `SIGTERM` / `SIGINT`: log shutdown, cleanup public FIFOs, exit.
   - `SIGHUP`: ignore or log and ignore.
   - `SIGPIPE`: ignore.

8. Replace important foreground `printf` / `perror` behavior with log output:

   - startup
   - effective config
   - FIFO paths
   - `sizeof` protocol structs
   - register/login/logout events
   - chat delivery
   - failed reply
   - target FIFO unavailable
   - fatal `select`, `open`, `mkdir`, `mkfifo`, `read` errors

9. Preserve Stage 01 permission handling:

   - `umask(0)` before `mkfifo`
   - `ensure_tree()` and `ensure_fifo()` remain idempotent
   - `chown_back_to_owner()` continues to make data/FIFO paths usable by the
     normal client user after `sudo` server startup

## Smoke Test

Add:

```text
scripts/smoke/smoke-stage02.sh
```

Update the `Makefile` so:

- `make smoke` runs the current stage smoke test.
- It is fine to keep separate `smoke-stage01` and `smoke-stage02` targets.

The Stage 02 smoke test should:

1. Build binaries if needed.
2. Fail clearly if `sudo` is required but unavailable.
3. Clean stale public FIFOs and stale matching server process if safe.
4. Start the server with `sudo`.
5. Because the server daemonizes, find the daemon pid with `pgrep` / `ps`, not
   `$!`.
6. Check that four public FIFOs exist and are FIFO files.
7. Run a client registration.
8. Run client login + logout.
9. Check `/var/log/chat-logs/server/server.log`:
   - exists
   - mode is approximately `0600`
   - contains startup / ready / register / login / logout entries
10. Send `SIGTERM` to the daemon pid.
11. Confirm public FIFOs are cleaned up.
12. Print:

   ```text
   STAGE 02 PASS
   ```

## Documentation

Add:

```text
docs/answer-notes/02.md
```

It should be 200-400 Chinese characters or words suitable for the final report,
covering:

- why the server is daemonized
- daemonization steps
- why stdout/stderr are unreliable after daemonization
- `server.log` path and permissions
- how `ps` and `cat server.log` prove this stage works

You may update `docs/plan.md` lightly if useful, but do not rewrite the whole
plan.

## Verification

At minimum run:

```bash
make clean && make
```

If the environment supports `sudo` and `/var/log`, run:

```bash
sudo -v
make smoke
```

If full smoke cannot run locally, explain why.

## Commit

Create one commit:

```text
stage 02: daemonize server and write server log
```

Do not commit `.DS_Store`.
Do not commit local HTML walkthrough files.

