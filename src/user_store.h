/*
 * user_store.h
 *
 * 阶段 03：把服务器的用户表从 chatserver.c 的普通全局数组迁到
 * POSIX 共享内存对象，并用放在共享内存区内、属性为
 * PTHREAD_PROCESS_SHARED 的 pthread 互斥锁保护。
 *
 * 本头文件只描述“服务器内部的用户表存储”，不属于 client/server 之间的
 * 线协议；线协议仍由 chat_common.h 定义，两者不要混在一起。
 *
 * 阶段 03 服务器仍是单进程单线程；这里的加锁是为阶段 04 的工作线程
 * 预留正确性，当前不证明并发。
 */

#ifndef CHAT_USER_STORE_H
#define CHAT_USER_STORE_H

#include <pthread.h>
#include <stddef.h>

#include "chat_common.h"

/* 魔数 "CHUS"，仅用于排查共享内存内容是否是本程序写的。 */
#define CHAT_USER_STORE_MAGIC   0x43485553u
#define CHAT_USER_STORE_VERSION 1u

/* 共享内存里的单条用户记录，字段与阶段 01/02 的 User 一致。 */
typedef struct
{
    char username[CHAT_NAME_LEN];
    char password[CHAT_PASSWORD_LEN];
    char fifo[CHAT_FIFO_PATH_LEN];
    int  online;
} ChatUserRecord;

/*
 * 共享内存对象的整体布局：互斥锁放最前面，其后是元信息与记录数组。
 * mutex 保护 magic 之后（含 user_count 与 users[]）的所有可变字段。
 */
typedef struct
{
    pthread_mutex_t mutex;
    unsigned int    magic;
    unsigned int    version;
    int             user_count;
    ChatUserRecord  users[CHAT_MAX_USERS];
} ChatUserStore;

/* 进程内持有的句柄。store 指向 mmap 区，NULL 表示未初始化。 */
typedef struct
{
    char           name[64];       /* 形如 "/chatroom_lwj_users" */
    ChatUserStore *store;          /* mmap 映射地址 */
    size_t         size;           /* 映射字节数 = sizeof(ChatUserStore) */
    int            process_shared; /* 1=互斥锁成功设为 PROCESS_SHARED */
} UserStoreHandle;

/* 注册/登录/登出操作的返回状态。 */
typedef enum
{
    USER_STORE_OK = 0,
    USER_STORE_ERR_EXISTS,        /* 注册：用户名已存在 */
    USER_STORE_ERR_FULL,          /* 注册：用户表已满 */
    USER_STORE_ERR_NOT_FOUND,     /* 登录/登出：用户不存在 */
    USER_STORE_ERR_BAD_PASSWORD,  /* 登录：密码错误 */
    USER_STORE_ERR_FIFO_MISMATCH  /* 登出：fifo 与记录不一致（弱身份校验失败） */
} UserStoreStatus;

/* 聊天投递前对用户表取一致快照的结果。 */
typedef enum
{
    CHAT_PREP_OK = 0,
    CHAT_PREP_SENDER_INVALID,  /* 发送方不存在或不在线：静默丢弃 */
    CHAT_PREP_TARGET_MISSING,  /* 目标不存在 */
    CHAT_PREP_TARGET_OFFLINE   /* 目标离线 */
} ChatPrepStatus;

/*
 * 创建/映射/初始化共享内存用户表。short_name 用于派生对象名
 * （"/chatroom_<short_name>_users"）。每次启动都重置共享区。
 * 成功返回 0；失败返回 -1（errno 保留失败原因）。
 */
int  user_store_init(UserStoreHandle *h, const char *short_name);

/* 解除映射并 shm_unlink。对零值/未初始化句柄是安全的 no-op。 */
void user_store_destroy(UserStoreHandle *h);

/*
 * 以下操作各自在内部完成加锁/解锁，锁的临界区只覆盖对用户表的查改；
 * 调用方应在锁外做 FIFO 回包这类慢/阻塞操作。
 */
UserStoreStatus user_store_register(UserStoreHandle *h, const char *username,
                                    const char *password, const char *fifo);
UserStoreStatus user_store_login(UserStoreHandle *h, const char *username,
                                 const char *password, const char *fifo);
UserStoreStatus user_store_logout(UserStoreHandle *h, const char *username,
                                  const char *fifo);

/*
 * 给聊天投递取一致快照：在一次加锁内校验发送方在线、目标在线，
 * 并把（只要发送方有效）发送方 fifo、（OK 时）目标 fifo 复制出来。
 * 之后的实际 FIFO 写在锁外进行。
 */
ChatPrepStatus  user_store_prepare_chat(UserStoreHandle *h,
                                        const char *from, const char *to,
                                        char *from_fifo, size_t from_fifo_sz,
                                        char *to_fifo, size_t to_fifo_sz);

/*
 * 投递失败后调用：仅当目标记录的 fifo 仍等于 fifo 时才把它置为离线，
 * 避免误伤在投递间隙用同名重新登录、换了新 fifo 的用户。
 * 返回 1 表示已置离线，0 表示记录已变化或不存在。
 */
int user_store_mark_offline_if_fifo(UserStoreHandle *h,
                                    const char *username, const char *fifo);

#endif
