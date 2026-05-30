/*
 * chatserver.c
 *
 * 多用户聊天系统服务器（阶段 01）。
 * 配置文件读入 ChatConfig，创建 4 个公共 FIFO，select() 单线程主循环。
 * 业务处理函数（注册/登录/聊天/登出）保持单线程同步执行；
 * 多线程化、共享内存、守护进程化等留到后续阶段。
 */

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "chat_common.h"
#include "src/config.h"

typedef struct
{
    char username[CHAT_NAME_LEN];
    char password[CHAT_PASSWORD_LEN];
    char fifo[CHAT_FIFO_PATH_LEN];
    int online;
} User;

/* 4 路公共 FIFO 的统一索引。 */
enum { FIFO_REGISTER = 0, FIFO_LOGIN, FIFO_MESSAGE, FIFO_LOGOUT, FIFO_COUNT };

static ChatConfig g_cfg;
static User       users[CHAT_MAX_USERS];
static int        user_count = 0;
static int        read_fds[FIFO_COUNT] = {-1, -1, -1, -1};
static int        hold_fds[FIFO_COUNT] = {-1, -1, -1, -1};
static const char *fifo_paths[FIFO_COUNT];

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    snprintf(dst, dst_size, "%s", src == NULL ? "" : src);
}

/* 如果目录不存在则创建；存在但非目录视为错误。 */
static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
            return 0;
        fprintf(stderr, "%s exists but is not a directory\n", path);
        return -1;
    }
    if (mkdir(path, 0777) == -1)
    {
        perror(path);
        return -1;
    }
    return 0;
}

/* 服务器以 sudo 启动时把目录所有权交还给 liwenjun，避免普通客户端读写受阻。 */
static void chown_back_to_owner(const char *path)
{
    struct passwd *pw = getpwnam("liwenjun2023150001");
    if (!pw) return;  /* 容器里可能没这账号，沉默跳过。 */
    if (chown(path, pw->pw_uid, pw->pw_gid) == -1 && errno != EPERM)
        perror(path);
}

/* 创建实验要求的目录树（data_dir / server_fifo / client_fifo）。 */
static int ensure_tree(const ChatConfig *cfg)
{
    if (ensure_dir(cfg->data_dir) == -1) return -1;
    if (ensure_dir(cfg->server_fifo_dir) == -1) return -1;
    if (ensure_dir(cfg->client_fifo_dir) == -1) return -1;
    chown_back_to_owner(cfg->data_dir);
    chown_back_to_owner(cfg->server_fifo_dir);
    chown_back_to_owner(cfg->client_fifo_dir);
    return 0;
}

/* 确保 path 是一个 FIFO（不存在则创建）。 */
static int ensure_fifo(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        if (S_ISFIFO(st.st_mode))
            return 0;
        fprintf(stderr, "%s exists but is not a FIFO\n", path);
        return -1;
    }
    if (mkfifo(path, 0666) == -1)
    {
        perror(path);
        return -1;
    }
    return 0;
}

/* 打开一个服务器公共 FIFO；读端非阻塞，hold 写端用来防止 EOF。 */
static int open_server_fifo(const char *path, int index)
{
    if (ensure_fifo(path) == -1) return -1;
    chown_back_to_owner(path);

    read_fds[index] = open(path, O_RDONLY | O_NONBLOCK);
    if (read_fds[index] == -1) { perror(path); return -1; }

    hold_fds[index] = open(path, O_WRONLY | O_NONBLOCK);
    if (hold_fds[index] == -1) { perror(path); return -1; }
    return 0;
}

/* 关闭所有 FIFO，并删除服务器创建的 4 个公共 FIFO。 */
static void cleanup(int status)
{
    int i;
    for (i = 0; i < FIFO_COUNT; i++)
    {
        if (read_fds[i] != -1) close(read_fds[i]);
        if (hold_fds[i] != -1) close(hold_fds[i]);
        if (fifo_paths[i]) unlink(fifo_paths[i]);
    }
    exit(status);
}

static void handle_signal(int sig)
{
    (void)sig;
    cleanup(EXIT_SUCCESS);
}

static int find_user(const char *username)
{
    int i;
    for (i = 0; i < user_count; i++)
        if (strcmp(users[i].username, username) == 0) return i;
    return -1;
}

static int send_packet(const char *fifo, const ChatPacket *packet)
{
    int fd = open(fifo, O_WRONLY | O_NONBLOCK);
    ssize_t written;
    if (fd == -1) return -1;
    written = write(fd, packet, sizeof(*packet));
    close(fd);
    return written == (ssize_t)sizeof(*packet) ? 0 : -1;
}

static void make_packet(ChatPacket *packet, int type, int ok,
                        const char *from, const char *message)
{
    memset(packet, 0, sizeof(*packet));
    packet->type = type;
    packet->ok = ok;
    copy_string(packet->from, sizeof(packet->from), from);
    copy_string(packet->message, sizeof(packet->message), message);
}

static void reply_to_fifo(const char *fifo, int ok, const char *message)
{
    ChatPacket packet;
    make_packet(&packet, CHAT_PACKET_REPLY, ok, "server", message);
    if (send_packet(fifo, &packet) == -1)
        fprintf(stderr, "Failed to reply through %s\n", fifo);
}

static void handle_register(const ChatAuthRequest *req)
{
    if (req->username[0] == '\0' || req->password[0] == '\0')
    {
        reply_to_fifo(req->fifo, 0, "username and password must not be empty");
        return;
    }
    if (find_user(req->username) != -1)
    {
        reply_to_fifo(req->fifo, 0, "username already exists");
        return;
    }
    if (user_count >= CHAT_MAX_USERS)
    {
        reply_to_fifo(req->fifo, 0, "server user table is full");
        return;
    }

    copy_string(users[user_count].username, sizeof(users[user_count].username), req->username);
    copy_string(users[user_count].password, sizeof(users[user_count].password), req->password);
    copy_string(users[user_count].fifo, sizeof(users[user_count].fifo), req->fifo);
    users[user_count].online = 0;
    user_count++;

    printf("registered user: %s\n", req->username);
    reply_to_fifo(req->fifo, 1, "register ok");
}

static void handle_login(const ChatAuthRequest *req)
{
    int index = find_user(req->username);
    if (index == -1)
    {
        reply_to_fifo(req->fifo, 0, "username does not exist");
        return;
    }
    if (strcmp(users[index].password, req->password) != 0)
    {
        reply_to_fifo(req->fifo, 0, "password is incorrect");
        return;
    }
    copy_string(users[index].fifo, sizeof(users[index].fifo), req->fifo);
    users[index].online = 1;
    printf("login user: %s, fifo: %s\n", req->username, req->fifo);
    reply_to_fifo(req->fifo, 1, "login ok");
}

/* 阶段 01 的登出 stub：核心是身份校验 + 置 online=0；不广播 ONLINE_LIST。 */
static void handle_logout(const ChatLogoutRequest *req)
{
    int idx = find_user(req->username);
    if (idx == -1) return;                                /* 不存在的用户，沉默丢弃 */
    if (strcmp(users[idx].fifo, req->fifo) != 0) return;  /* fifo 不匹配，弱身份校验失败 */
    users[idx].online = 0;
    reply_to_fifo(req->fifo, 1, "logout ok");
    printf("logout user: %s\n", req->username);
}

static void handle_chat(const ChatSendRequest *req)
{
    int from_index = find_user(req->from);
    int to_index = find_user(req->to);
    ChatPacket packet;
    char message[CHAT_TEXT_LEN];

    if (from_index == -1 || !users[from_index].online) return;
    if (to_index == -1)
    {
        reply_to_fifo(users[from_index].fifo, 0, "target user does not exist");
        return;
    }
    if (!users[to_index].online)
    {
        reply_to_fifo(users[from_index].fifo, 0, "target user is not online");
        return;
    }

    make_packet(&packet, CHAT_PACKET_MESSAGE, 1, req->from, req->text);
    if (send_packet(users[to_index].fifo, &packet) == -1)
    {
        users[to_index].online = 0;
        reply_to_fifo(users[from_index].fifo, 0, "target fifo is not available");
        return;
    }
    snprintf(message, sizeof(message), "message sent to %s", req->to);
    reply_to_fifo(users[from_index].fifo, 1, message);
    printf("%s -> %s: %s\n", req->from, req->to, req->text);
}

static void drain_auth_fifo(int fd, int is_register)
{
    ChatAuthRequest req;
    ssize_t nread;
    while (1)
    {
        nread = read(fd, &req, sizeof(req));
        if (nread == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            perror("read"); return;
        }
        if (nread == 0) return;
        if (nread != (ssize_t)sizeof(req))
        {
            fprintf(stderr, "Ignoring incomplete auth request: %zd bytes\n", nread);
            continue;
        }
        if (is_register) handle_register(&req); else handle_login(&req);
    }
}

static void drain_chat_fifo(int fd)
{
    ChatSendRequest req;
    ssize_t nread;
    while (1)
    {
        nread = read(fd, &req, sizeof(req));
        if (nread == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            perror("read"); return;
        }
        if (nread == 0) return;
        if (nread != (ssize_t)sizeof(req))
        {
            fprintf(stderr, "Ignoring incomplete chat request: %zd bytes\n", nread);
            continue;
        }
        handle_chat(&req);
    }
}

static void drain_logout_fifo(int fd)
{
    ChatLogoutRequest req;
    ssize_t nread;
    while (1)
    {
        nread = read(fd, &req, sizeof(req));
        if (nread == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            perror("read"); return;
        }
        if (nread == 0) return;
        if (nread != (ssize_t)sizeof(req))
        {
            fprintf(stderr, "Ignoring incomplete logout request: %zd bytes\n", nread);
            continue;
        }
        handle_logout(&req);
    }
}

static void print_startup_banner(const ChatConfig *cfg)
{
    printf("[%s] config loaded from %s\n", cfg->full_name, "config/chatserver.conf");
    printf("  data_dir        = %s\n", cfg->data_dir);
    printf("  log_dir         = %s   (本阶段尚未启用)\n", cfg->log_dir);
    printf("  server_fifo_dir = %s\n", cfg->server_fifo_dir);
    printf("  client_fifo_dir = %s\n", cfg->client_fifo_dir);
    printf("[%s] sizeof(ChatPacket)        = %zu\n", cfg->full_name, sizeof(ChatPacket));
    printf("[%s] sizeof(ChatAuthRequest)   = %zu\n", cfg->full_name, sizeof(ChatAuthRequest));
    printf("[%s] sizeof(ChatSendRequest)   = %zu\n", cfg->full_name, sizeof(ChatSendRequest));
    printf("[%s] sizeof(ChatLogoutRequest) = %zu\n", cfg->full_name, sizeof(ChatLogoutRequest));
    printf("[%s] listening on:\n", cfg->full_name);
    printf("  REG_FIFO    -> %s\n", cfg->fifo_register);
    printf("  LOGIN_FIFO  -> %s\n", cfg->fifo_login);
    printf("  MSG_FIFO    -> %s\n", cfg->fifo_message);
    printf("  LOGOUT_FIFO -> %s\n", cfg->fifo_logout);
    printf("[%s] ready (single-threaded select)\n", cfg->full_name);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    fd_set rfds;
    int maxfd, i;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <config-file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (chat_config_load(argv[1], &g_cfg) != 0)
        return EXIT_FAILURE;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    /* mkfifo 之前必须 umask(0)，否则 0666 会被默认 umask 022 砍成 0644。 */
    umask(0);

    if (ensure_tree(&g_cfg) == -1) exit(EXIT_FAILURE);

    fifo_paths[FIFO_REGISTER] = g_cfg.fifo_register;
    fifo_paths[FIFO_LOGIN]    = g_cfg.fifo_login;
    fifo_paths[FIFO_MESSAGE]  = g_cfg.fifo_message;
    fifo_paths[FIFO_LOGOUT]   = g_cfg.fifo_logout;

    for (i = 0; i < FIFO_COUNT; i++)
        if (open_server_fifo(fifo_paths[i], i) == -1) cleanup(EXIT_FAILURE);

    print_startup_banner(&g_cfg);

    while (1)
    {
        FD_ZERO(&rfds);
        maxfd = -1;
        for (i = 0; i < FIFO_COUNT; i++)
        {
            FD_SET(read_fds[i], &rfds);
            if (read_fds[i] > maxfd) maxfd = read_fds[i];
        }

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) == -1)
        {
            if (errno == EINTR) continue;
            perror("select");
            cleanup(EXIT_FAILURE);
        }

        if (FD_ISSET(read_fds[FIFO_REGISTER], &rfds)) drain_auth_fifo(read_fds[FIFO_REGISTER], 1);
        if (FD_ISSET(read_fds[FIFO_LOGIN], &rfds))    drain_auth_fifo(read_fds[FIFO_LOGIN], 0);
        if (FD_ISSET(read_fds[FIFO_MESSAGE], &rfds))  drain_chat_fifo(read_fds[FIFO_MESSAGE]);
        if (FD_ISSET(read_fds[FIFO_LOGOUT], &rfds))   drain_logout_fifo(read_fds[FIFO_LOGOUT]);
    }
}
