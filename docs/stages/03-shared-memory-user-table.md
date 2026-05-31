# Stage 03: POSIX Shared Memory User Table + Process-Shared Mutex

## Goal

Move the server's in-process user table from plain globals in `chatserver.c` to
a POSIX shared memory object protected by a `pthread_mutex_t` configured with
`PTHREAD_PROCESS_SHARED`.

This stage is mainly for report item 2.1: explain how shared memory is created,
mapped, protected by a mutex, and verified. It should prepare the user table for
later multi-threaded and multi-process extensions without changing the client
protocol yet.

## Scope

Implement only:

- POSIX shared memory object for the user table.
- Process-shared pthread mutex stored inside the shared memory region.
- Server-side register/login/logout/chat logic updated to access the shared
  table through locking helpers.
- Stage 03 smoke test and report notes.

Do not implement:

- multi-threaded request handling
- thread pool
- online-list broadcast
- one-to-many send
- important-friend `*` marker
- user logs
- offline message queue
- protocol field changes
- `/who` or other new client commands

Stage 01 and Stage 02 behavior must keep working.

## Suggested Design

Add new files:

```text
src/user_store.h
src/user_store.c
```

Keep protocol structs in `chat_common.h` unchanged. Do not move shared memory
implementation details into `chat_common.h`; that header is the wire protocol
shared by server and client.

Suggested shared-memory name:

```text
/chatroom_lwj_users
```

It should correspond to the Linux file:

```text
/dev/shm/chatroom_lwj_users
```

Using `cfg->short_name` to derive the name is fine, but keep it deterministic
and document it in the logs and smoke script.

Suggested data model:

```c
#define CHAT_USER_STORE_MAGIC 0x43485553u
#define CHAT_USER_STORE_VERSION 1u

typedef struct {
    char username[CHAT_NAME_LEN];
    char password[CHAT_PASSWORD_LEN];
    char fifo[CHAT_FIFO_PATH_LEN];
    int  online;
} ChatUserRecord;

typedef struct {
    pthread_mutex_t mutex;
    unsigned int magic;
    unsigned int version;
    int user_count;
    ChatUserRecord users[CHAT_MAX_USERS];
} ChatUserStore;
```

You may adjust names, but keep the responsibilities clear.

## Initialization Requirements

In `src/user_store.c`, create helpers that perform roughly:

1. `shm_open(name, O_CREAT | O_RDWR, 0600)`
2. `fchmod(fd, 0600)` so an existing shm object is not left too permissive
3. `ftruncate(fd, sizeof(ChatUserStore))`
4. `mmap(..., PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)`
5. initialize the shared region for the current server run:
   - `memset`
   - `pthread_mutexattr_init`
   - `pthread_mutexattr_setpshared(..., PTHREAD_PROCESS_SHARED)`
   - `pthread_mutex_init(&store->mutex, &attr)`
   - set `magic`, `version`, `user_count = 0`

Resetting the table on server startup is acceptable and intentional in this
stage: the old global array also lost all registered users when the server
restarted. If a previous daemon was killed with `kill -9`, a stale shm object
may remain; the next normal startup should reinitialize it instead of failing
only because the object already exists.

On normal server shutdown, unmap and close the object, then `shm_unlink(name)`.
If the server receives `SIGTERM`, cleanup still happens through the Stage 02
`sig_atomic_t` flag path, not inside the signal handler.

If `pthread_mutexattr_setpshared` is unsupported on the local OS, report that
clearly. The target environment is the Linux euler container.

## Locking Requirements

All access to `user_count` and `users[]` must be protected by the shared mutex.

Keep lock scope small:

- Lock while searching/updating the table.
- Copy any needed FIFO path or reply status into local variables.
- Unlock before doing slow or blocking FIFO writes when practical.

For chat delivery:

- Lock and verify sender exists and is online.
- Lock and verify target exists and is online.
- Copy sender/target FIFO paths and target username into locals.
- Unlock before writing to the target FIFO.
- If delivery fails, lock again and mark the target offline only if the record
  still refers to the same target FIFO.

This still runs in one server process and one main thread in Stage 03, but the
code should already be safe for Stage 04 worker threads.

## Server Integration

In `chatserver.c`:

- Remove the static global `User users[CHAT_MAX_USERS]` and `user_count`.
- Add a global user-store handle.
- Initialize the store after config load / daemon setup / log opening, before
  request handling begins.
- Include shared-memory information in startup logs:
  - shm name
  - shm size
  - `sizeof(ChatUserStore)`
  - mutex initialized as process-shared
- Update `handle_register`, `handle_login`, `handle_logout`, and `handle_chat`
  to use the store helpers and mutex.
- Keep existing reply texts stable where possible:
  - `register ok`
  - `login ok`
  - `logout ok`
  - `username already exists`
  - `target user does not exist`
  - `target user is not online`
- Keep Stage 02 daemon/log behavior unchanged.

## Build System

Update `Makefile`:

- compile `src/user_store.c` into the server binary
- link the server with pthread support, for example `-pthread`
- keep the client build unchanged unless a shared helper truly requires it
- make `make smoke` run Stage 03 by default
- keep explicit `smoke-stage01`, `smoke-stage02`, and `smoke-stage03` targets

## Smoke Test

Add:

```text
scripts/smoke/smoke-stage03.sh
```

Base it on `smoke-stage02.sh`, but add shared-memory checks.

The test should:

1. Build binaries if needed.
2. Require Linux `/dev/shm`; if unavailable, print a clear skip message.
3. Require `sudo` for the daemon and `/var/log`.
4. Kill stale matching server processes using the Stage 02 server+conf matching
   strategy.
5. Remove stale public FIFOs and stale client FIFOs for the smoke users.
6. Remove stale `/dev/shm/chatroom_lwj_users` if no matching server is running.
7. Start the server with `sudo`.
8. Find the daemon pid by matching both absolute `$SERVER` and `$CONF`.
9. Verify the shm object exists in `/dev/shm` and has mode `600`.
10. Verify 4 public FIFOs still exist.
11. Register a user and verify `register ok`.
12. Register the same user again and verify `username already exists`.
13. Login and logout the user and verify `login ok` / `logout ok`.
14. Check `server.log` contains shared-memory initialization plus register,
    duplicate-register, login, and logout entries.
15. Send `SIGTERM` to the daemon.
16. Confirm public FIFOs are cleaned up.
17. Confirm the shm object is removed after normal shutdown.
18. Print:

```text
STAGE 03 PASS
```

The smoke test does not need to prove concurrent correctness yet. It proves the
server is using a shm-backed table and that the existing user lifecycle still
works.

## Documentation

Add:

```text
docs/answer-notes/03.md
```

Write 200-400 Chinese characters or words for the final report, covering:

- why the user table is put into shared memory
- `shm_open` / `ftruncate` / `mmap`
- why `pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)` is needed
- what data is protected by the mutex
- how `/dev/shm`, `server.log`, duplicate registration, and login/logout smoke
  checks prove this stage works
- what this stage still does not prove: real multi-threaded concurrency comes
  in Stage 04

Update `CLAUDE.md` after successful implementation so it records Stage 03 as
completed and `make smoke` as the current Stage 03 smoke target.

## Verification

At minimum run:

```bash
make clean && make
bash -n scripts/smoke/smoke-stage01.sh scripts/smoke/smoke-stage02.sh scripts/smoke/smoke-stage03.sh
git diff --check
```

If on the Linux euler container with sudo available, run:

```bash
sudo -v
make smoke
```

If running on macOS and `/dev/shm` or process-shared pthread mutexes are not
available, explain that the real Stage 03 smoke must run on euler. Do not fake a
successful smoke result.

## Commit And Push

Create one commit:

```text
stage 03: move user table to shared memory
```

Then push to GitHub:

```bash
git push origin main
```

Do not commit `.DS_Store`.
Do not commit local HTML walkthrough files.
