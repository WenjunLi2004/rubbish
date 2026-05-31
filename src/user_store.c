/*
 * user_store.c
 *
 * 阶段 03：POSIX 共享内存用户表 + 进程间互斥锁的实现。
 *
 * 步骤：shm_open(O_CREAT) → fchmod(0600) → ftruncate → mmap →
 * 在共享区内用 PTHREAD_PROCESS_SHARED 属性初始化互斥锁，并写入
 * magic/version/user_count。所有对 user_count 与 users[] 的访问都在
 * 这把锁的保护下完成。
 *
 * 设计取舍：把“查找/插入/改状态”这类临界区操作封装在本文件里、
 * 内部自管加解锁，chatserver.c 的 handler 只拿返回值、在锁外做 FIFO
 * 回包。这样既保证锁范围最小，也避免上层忘记解锁。
 */

#include "user_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* 安全字符串拷贝：保证 '\0' 结尾，超长截断。 */
static void copy_string(char *dst, size_t dst_size, const char *src)
{
    snprintf(dst, dst_size, "%s", src == NULL ? "" : src);
}

static void su_lock(UserStoreHandle *h)   { pthread_mutex_lock(&h->store->mutex); }
static void su_unlock(UserStoreHandle *h) { pthread_mutex_unlock(&h->store->mutex); }

/* 在已持锁的前提下按用户名查找，返回下标或 -1。 */
static int find_locked(const ChatUserStore *s, const char *username)
{
    int i;
    for (i = 0; i < s->user_count; i++)
        if (strcmp(s->users[i].username, username) == 0) return i;
    return -1;
}

int user_store_init(UserStoreHandle *h, const char *short_name)
{
    char                name[64];
    int                 fd = -1;
    ChatUserStore      *s = MAP_FAILED;
    pthread_mutexattr_t attr;
    int                 pshared_ok = 0;
    int                 rc;

    memset(h, 0, sizeof(*h));

    rc = snprintf(name, sizeof(name), "/chatroom_%s_users",
                  (short_name && short_name[0]) ? short_name : "default");
    if (rc < 0 || (size_t)rc >= sizeof(name)) { errno = ENAMETOOLONG; return -1; }

    fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd == -1) return -1;

    /* 已存在的对象可能权限较松，尽量用 fchmod 收紧到 0600（与 server.log 的 fchmod 同理）。
     * 少数平台（如 macOS）对 shm fd 的 fchmod 返回 EINVAL：此时退而用 fstat 确认对象
     * 未对组/其他开放（newly created 时本就是 0600）；只要不超出 0600 即可继续，否则判失败。
     * 目标环境 Linux euler 上 fchmod 正常生效，强制收紧。 */
    if (fchmod(fd, 0600) == -1)
    {
        struct stat st;
        if (fstat(fd, &st) == -1) goto fail;
        if ((st.st_mode & 077) != 0) { errno = EPERM; goto fail; }  /* 太松又改不动：失败 */
    }

    if (ftruncate(fd, (off_t)sizeof(ChatUserStore)) == -1) goto fail;

    s = mmap(NULL, sizeof(ChatUserStore), PROT_READ | PROT_WRITE,
             MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) goto fail;

    close(fd);   /* 映射建立后 fd 不再需要 */
    fd = -1;

    /* 每次启动都重置共享区：与旧版全局数组“重启即清空”的语义一致，
     * 也能覆盖上一个被 kill -9 的守护进程残留下来的陈旧对象。 */
    memset(s, 0, sizeof(*s));

    if (pthread_mutexattr_init(&attr) != 0) goto fail_unmap;
    /* 把锁设为进程间共享是本阶段的核心。失败（如 macOS 不支持）不致命：
     * 退回进程私有锁，由调用方在日志里说明；Linux euler 上应成功。 */
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) == 0)
        pshared_ok = 1;
    rc = pthread_mutex_init(&s->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc != 0) { errno = rc; goto fail_unmap; }

    s->magic = CHAT_USER_STORE_MAGIC;
    s->version = CHAT_USER_STORE_VERSION;
    s->user_count = 0;

    copy_string(h->name, sizeof(h->name), name);
    h->store = s;
    h->size = sizeof(ChatUserStore);
    h->process_shared = pshared_ok;
    return 0;

fail_unmap:
    munmap(s, sizeof(ChatUserStore));
fail:
    {
        int saved = errno;
        if (fd != -1) close(fd);
        shm_unlink(name);   /* 本次创建/打开失败，尽量不残留对象 */
        errno = saved;
    }
    return -1;
}

void user_store_destroy(UserStoreHandle *h)
{
    if (h->store != NULL)
    {
        pthread_mutex_destroy(&h->store->mutex);
        munmap(h->store, h->size);
        h->store = NULL;
    }
    if (h->name[0] != '\0')
    {
        shm_unlink(h->name);
        h->name[0] = '\0';
    }
}

UserStoreStatus user_store_register(UserStoreHandle *h, const char *username,
                                    const char *password, const char *fifo)
{
    ChatUserStore  *s = h->store;
    UserStoreStatus ret;

    su_lock(h);
    if (find_locked(s, username) != -1)
        ret = USER_STORE_ERR_EXISTS;
    else if (s->user_count >= CHAT_MAX_USERS)
        ret = USER_STORE_ERR_FULL;
    else
    {
        ChatUserRecord *r = &s->users[s->user_count];
        copy_string(r->username, sizeof(r->username), username);
        copy_string(r->password, sizeof(r->password), password);
        copy_string(r->fifo, sizeof(r->fifo), fifo);
        r->online = 0;
        s->user_count++;
        ret = USER_STORE_OK;
    }
    su_unlock(h);
    return ret;
}

UserStoreStatus user_store_login(UserStoreHandle *h, const char *username,
                                 const char *password, const char *fifo)
{
    ChatUserStore  *s = h->store;
    UserStoreStatus ret;
    int             idx;

    su_lock(h);
    idx = find_locked(s, username);
    if (idx < 0)
        ret = USER_STORE_ERR_NOT_FOUND;
    else if (strcmp(s->users[idx].password, password) != 0)
        ret = USER_STORE_ERR_BAD_PASSWORD;
    else
    {
        copy_string(s->users[idx].fifo, sizeof(s->users[idx].fifo), fifo);
        s->users[idx].online = 1;
        ret = USER_STORE_OK;
    }
    su_unlock(h);
    return ret;
}

UserStoreStatus user_store_logout(UserStoreHandle *h, const char *username,
                                  const char *fifo)
{
    ChatUserStore  *s = h->store;
    UserStoreStatus ret;
    int             idx;

    su_lock(h);
    idx = find_locked(s, username);
    if (idx < 0)
        ret = USER_STORE_ERR_NOT_FOUND;
    else if (strcmp(s->users[idx].fifo, fifo) != 0)
        ret = USER_STORE_ERR_FIFO_MISMATCH;
    else
    {
        s->users[idx].online = 0;
        ret = USER_STORE_OK;
    }
    su_unlock(h);
    return ret;
}

ChatPrepStatus user_store_prepare_chat(UserStoreHandle *h,
                                       const char *from, const char *to,
                                       char *from_fifo, size_t from_fifo_sz,
                                       char *to_fifo, size_t to_fifo_sz)
{
    ChatUserStore *s = h->store;
    ChatPrepStatus ret;
    int            fi, ti;

    su_lock(h);
    fi = find_locked(s, from);
    if (fi < 0 || !s->users[fi].online)
    {
        ret = CHAT_PREP_SENDER_INVALID;
        goto out;
    }
    copy_string(from_fifo, from_fifo_sz, s->users[fi].fifo);

    ti = find_locked(s, to);
    if (ti < 0)
        ret = CHAT_PREP_TARGET_MISSING;
    else if (!s->users[ti].online)
        ret = CHAT_PREP_TARGET_OFFLINE;
    else
    {
        copy_string(to_fifo, to_fifo_sz, s->users[ti].fifo);
        ret = CHAT_PREP_OK;
    }
out:
    su_unlock(h);
    return ret;
}

int user_store_mark_offline_if_fifo(UserStoreHandle *h,
                                    const char *username, const char *fifo)
{
    ChatUserStore *s = h->store;
    int            marked = 0;
    int            idx;

    su_lock(h);
    idx = find_locked(s, username);
    if (idx >= 0 && strcmp(s->users[idx].fifo, fifo) == 0)
    {
        s->users[idx].online = 0;
        marked = 1;
    }
    su_unlock(h);
    return marked;
}
