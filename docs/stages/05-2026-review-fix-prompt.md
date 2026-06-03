# Claude Code Prompt: Fix 2026 Spec Alignment Review Issues

You are Claude Code running in:

```text
/Users/wenjun/Documents/GitHub/rubbish
```

The 2026 spec alignment was implemented and pushed in:

- `ab5c612 align chat system with 2026 assignment spec`
- `9cbea73 docs: align plan/protocol/notes with 2026 assignment spec`

Codex review found a few issues to fix. Keep this as a focused review-fix
commit. Do not rewrite the whole architecture.

## Read First

- `CLAUDE.md`
- `docs/plan.md`
- `docs/protocol.md`
- `chatserver.c`
- `chatclient.c`
- `chat_common.h`
- `src/user_store.h`
- `src/user_store.c`
- `src/thread_pool.h`
- `src/thread_pool.c`
- `scripts/smoke/smoke-2026.sh`

## Issues To Fix

### 1. Bot manager requests must require an online real user

Current `do_bot_manager()` only calls `user_store_lookup_fifo()`. A user that
registered but did not log in can still have a stored FIFO path and issue
`/bot add` or `/bot del`. That is not aligned with the assignment flow: robot
management is an action by an online client user.

Fix:

- Add a helper in `src/user_store.[ch]`, for example:

  ```c
  int user_store_lookup_online_client_fifo(UserStoreHandle *h,
                                           const char *username,
                                           char *buf,
                                           size_t bufsz);
  ```

- It should return true only if the user exists, is online, is not a bot, and
  has a non-empty FIFO.
- Update `do_bot_manager()` to use this helper.
- If the requester is not online, reply through the best available FIFO if
  possible, or silently drop if no FIFO is known. Prefer a clear reply when the
  requester's FIFO can be found:

  ```text
  bot manager requires login
  ```

- Add a smoke check that a registered-but-not-logged-in user cannot add bots.

### 2. Smoke must prove the process-shared mutex on Linux/euler

The code now aborts on Linux if `PTHREAD_PROCESS_SHARED` is unavailable, which
is good. But `scripts/smoke/smoke-2026.sh` currently does not explicitly grep
the successful log line.

Fix smoke to require:

```text
user store mutex initialized as process-shared
```

in `server.log` on Linux/euler.

### 3. Smoke should verify the important-friend `*` threshold

The assignment says successful sends exceeding 5 should mark the important
friend with `*`. Current smoke sends exactly 5 messages and does not verify this
feature.

Fix:

- Send a sixth message from `liwenjun_1` to `liwenjun_2`.
- Verify `liwenjun_1` receives an ack containing:

  ```text
  message sent to liwenjun_2*
  ```

- Keep the test robust to async output ordering.

### 4. Preserve offline messages if push delivery fails

Current `push_offline_messages()` pops messages from the store before trying to
write them to the logging-in user's FIFO. If the FIFO write fails, the pending
message is lost.

Fix one of these ways:

- Prefer: add a user-store helper that snapshots pending messages without
  clearing them, then clears each message only after successful `send_packet()`.
- Or: add a helper to reinsert failed messages.

Keep FIFO I/O outside the shared-memory mutex.

Log failed offline push attempts clearly.

### 5. Avoid target re-login race after failed live send

In `do_chat_to_human()`, when target was online but `send_packet(info->to_fifo)`
fails, the code calls `user_store_mark_offline_if_fifo()`, then always stores an
offline message. If `mark_offline_if_fifo()` returns 0 because the target has
already re-logged in with a new FIFO, storing a pending message is misleading.

Fix:

- If `mark_offline_if_fifo()` returns 0, re-check the target or reply with a
  retry/error message instead of blindly storing pending.
- Keep the logic simple and document the choice in a comment.

## Verification

Run locally:

```bash
make clean && make
bash -n scripts/smoke/*.sh
git diff --check
```

On euler Linux, run if available:

```bash
sudo -v
make smoke
```

If this local machine is macOS and `/dev/shm` is unavailable, `make smoke` may
skip locally; do not claim it passed locally. Use prior euler access if
available, otherwise state that full smoke still needs euler.

## Commit And Push

Create one focused commit:

```text
fix 2026 bot manager and smoke coverage
```

Push to GitHub:

```bash
git push origin main
```

Do not commit:

- `.DS_Store`
- ignored local walkthrough HTML files
- `2026系统编程大作业试题.pdf`

At the end, report:

- changed files
- fixes made
- commands run and results
- whether full euler `make smoke` passed
- commit hash
- push result
