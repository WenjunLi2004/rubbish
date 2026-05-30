/*
 * config.h
 *
 * 服务器/客户端共用的配置结构体与加载接口。
 * 所有路径、FIFO 名都从配置文件派生，避免在代码里硬编码。
 */

#ifndef CHAT_CONFIG_H
#define CHAT_CONFIG_H

typedef struct {
    /* 配置文件原始字段 */
    char server_name[64];
    char short_name[16];
    char version[16];
    char data_dir[256];
    char log_dir[256];
    char fifo_prefix[16];
    int  poolsize;

    /* 派生字段，chat_config_load 自动填充 */
    char full_name[96];          /* "chatserver_lwj_1.0.0" */
    char server_fifo_dir[320];   /* <data_dir>/server_fifo */
    char client_fifo_dir[320];   /* <data_dir>/client_fifo */
    char fifo_register[384];
    char fifo_login[384];
    char fifo_message[384];
    char fifo_logout[384];
    char log_dir_server[320];    /* <log_dir>/server */
    char log_dir_users[320];     /* <log_dir>/users */
    char server_log_path[512];   /* <log_dir>/server/server.log */
    char threads_log_path[512];  /* <log_dir>/server/threads.log */
} ChatConfig;

/* 成功返回 0；失败返回 -1 并向 stderr 打印原因。 */
int chat_config_load(const char *path, ChatConfig *out);

#endif
