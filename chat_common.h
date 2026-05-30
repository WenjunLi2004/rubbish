/*
 * chat_common.h
 *
 * 多用户聊天系统的公共协议头文件。
 * 服务器和客户端都包含这个文件，保证双方使用相同的字段长度和
 * 通信结构体格式。所有路径与 FIFO 名都不再硬编码，统一由
 * `src/config.h` 的 ChatConfig 在运行时提供。
 */

#ifndef CHAT_COMMON_H
#define CHAT_COMMON_H

/* 用户数量和各字段长度限制，避免固定数组越界。 */
#define CHAT_MAX_USERS 64
#define CHAT_NAME_LEN 32
#define CHAT_PASSWORD_LEN 32
#define CHAT_FIFO_PATH_LEN 256
#define CHAT_TEXT_LEN 256

/*
 * 注册和登录请求结构体。
 * 注册写入 REG_FIFO，登录写入 LOGIN_FIFO，结构体复用——
 * op 信息隐式地由“写入的 FIFO 是哪一个”表达。
 */
typedef struct
{
    char username[CHAT_NAME_LEN];
    char password[CHAT_PASSWORD_LEN];
    char fifo[CHAT_FIFO_PATH_LEN];
} ChatAuthRequest;

/* 客户端发送聊天消息时使用的请求结构体。 */
typedef struct
{
    char from[CHAT_NAME_LEN];
    char to[CHAT_NAME_LEN];
    char text[CHAT_TEXT_LEN];
} ChatSendRequest;

/*
 * 登出请求。仅需身份标识 + 客户端 FIFO，用 FIFO 做弱身份校验：
 * 只有当请求中的 fifo 与用户表中记录的一致时，服务器才接受登出。
 */
typedef struct
{
    char username[CHAT_NAME_LEN];
    char fifo[CHAT_FIFO_PATH_LEN];
} ChatLogoutRequest;

typedef enum
{
    CHAT_PACKET_REPLY = 1,
    CHAT_PACKET_MESSAGE = 2
} ChatPacketType;

/*
 * 服务器向客户端发送的统一数据包。
 * timestamp/send_count/reserved 等扩展字段留到后续阶段再追加，
 * 当前阶段保持 1.0.0 协议字段不变。
 */
typedef struct
{
    int type;
    int ok;
    char from[CHAT_NAME_LEN];
    char message[CHAT_TEXT_LEN];
} ChatPacket;

#endif
