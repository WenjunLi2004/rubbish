/*
 * chatserver.c
 *
 * 多用户聊天系统服务器（阶段 02）。
 * 在阶段 01 的基础上把服务器守护进程化（daemon），并把启动、退出、
 * 致命错误以及注册/登录/登出/聊天事件写入
 *   /var/log/chat-logs/server/server.log
 * 业务处理函数（注册/登录/聊天/登出）仍保持单线程同步执行；
 * 多线程化、共享内存、线程池等留到后续阶段。
 */

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
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
static int        g_log_fd = -1;   /* server.log 的写句柄，守护化后唯一可靠的输出 */

/*
 * 统一日志：守护化后 stdout/stderr 已重定向到 /dev/null，printf/perror 会被丢弃，
 * 因此所有诊断都改走这里。日志句柄尚未打开时（如创建日志目录失败）退回到 stderr，
 * 此时进程还没重定向标准流，消息仍能在终端看到。
 * 行格式： "YYYY-MM-DD HH:MM:SS [pid=12345] [LEVEL] message"
 */
static void log_message(const char *level, const char *fmt, ...)
{
    char    ts[32];
    char    line[1024];
    time_t  now = time(NULL);
    struct tm tmv;
    va_list ap;
    int     hdr, body;
    size_t  len;
    int     fd = (g_log_fd != -1) ? g_log_fd : STDERR_FILENO;

    localtime_r(&now, &tmv);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    hdr = snprintf(line, sizeof(line), "%s [pid=%d] [%s] ",
                   ts, (int)getpid(), level);
    if (hdr < 0 || (size_t)hdr >= sizeof(line)) return;

    va_start(ap, fmt);
    body = vsnprintf(line + hdr, sizeof(line) - (size_t)hdr, fmt, ap);
    va_end(ap);
    if (body < 0) return;

    len = (size_t)hdr + (size_t)body;
    if (len >= sizeof(line) - 1) len = sizeof(line) - 2;  /* 给换行留位 */
    line[len++] = '\n';

    {
        size_t off = 0;
        while (off < len)
        {
            ssize_t w = write(fd, line + off, len - off);
            if (w <= 0)
            {
                if (w == -1 && errno == EINTR) continue;
                break;
            }
            off += (size_t)w;
        }
    }
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    snprintf(dst, dst_size, "%s", src == NULL ? "" : src);
}

/* 如果目录不存在则按 mode 创建；存在但非目录视为错误。 */
static int ensure_dir(const char *path, mode_t mode)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
            return 0;
        log_message("ERROR", "%s exists but is not a directory", path);
        return -1;
    }
    if (mkdir(path, mode) == -1)
    {
        log_message("ERROR", "mkdir %s failed: %s", path, strerror(errno));
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
        log_message("WARN", "chown %s failed: %s", path, strerror(errno));
}

/* 创建实验要求的目录树（data_dir / server_fifo / client_fifo）。 */
static int ensure_tree(const ChatConfig *cfg)
{
    if (ensure_dir(cfg->data_dir, 0777) == -1) return -1;
    if (ensure_dir(cfg->server_fifo_dir, 0777) == -1) return -1;
    if (ensure_dir(cfg->client_fifo_dir, 0777) == -1) return -1;
    chown_back_to_owner(cfg->data_dir);
    chown_back_to_owner(cfg->server_fifo_dir);
    chown_back_to_owner(cfg->client_fifo_dir);
    return 0;
}

/*
 * 创建日志目录树并打开 server.log。
 * 日志根目录与文件保持 root 所有（sudo 启动时即为 root），不 chown 回普通用户，
 * 符合"日志只允许特权进程写"的要求。日志文件权限固定 0600。
 */
static int open_server_log(const ChatConfig *cfg)
{
    if (ensure_dir(cfg->log_dir, 0700) == -1) return -1;
    if (ensure_dir(cfg->log_dir_server, 0700) == -1) return -1;

    g_log_fd = open(cfg->server_log_path,
                    O_CREAT | O_APPEND | O_WRONLY, 0600);
    if (g_log_fd == -1)
    {
        log_message("ERROR", "open %s failed: %s",
                    cfg->server_log_path, strerror(errno));
        return -1;
    }
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
        log_message("ERROR", "%s exists but is not a FIFO", path);
        return -1;
    }
    if (mkfifo(path, 0666) == -1)
    {
        log_message("ERROR", "mkfifo %s failed: %s", path, strerror(errno));
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
    if (read_fds[index] == -1)
    {
        log_message("ERROR", "open(rd) %s failed: %s", path, strerror(errno));
        return -1;
    }

    hold_fds[index] = open(path, O_WRONLY | O_NONBLOCK);
    if (hold_fds[index] == -1)
    {
        log_message("ERROR", "open(wr) %s failed: %s", path, strerror(errno));
        return -1;
    }
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
    log_message("INFO", "received signal %d, shutting down", sig);
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
        log_message("WARN", "failed to reply through %s", fifo);
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

    log_message("INFO", "registered user: %s", req->username);
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
    log_message("INFO", "login user: %s, fifo: %s", req->username, req->fifo);
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
    log_message("INFO", "logout user: %s", req->username);
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
        log_message("WARN", "target fifo unavailable for %s, marked offline", req->to);
        reply_to_fifo(users[from_index].fifo, 0, "target fifo is not available");
        return;
    }
    snprintf(message, sizeof(message), "message sent to %s", req->to);
    reply_to_fifo(users[from_index].fifo, 1, message);
    log_message("INFO", "%s -> %s: %s", req->from, req->to, req->text);
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
            log_message("ERROR", "read auth fifo failed: %s", strerror(errno));
            return;
        }
        if (nread == 0) return;
        if (nread != (ssize_t)sizeof(req))
        {
            log_message("WARN", "ignoring incomplete auth request: %zd bytes", nread);
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
            log_message("ERROR", "read chat fifo failed: %s", strerror(errno));
            return;
        }
        if (nread == 0) return;
        if (nread != (ssize_t)sizeof(req))
        {
            log_message("WARN", "ignoring incomplete chat request: %zd bytes", nread);
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
            log_message("ERROR", "read logout fifo failed: %s", strerror(errno));
            return;
        }
        if (nread == 0) return;
        if (nread != (ssize_t)sizeof(req))
        {
            log_message("WARN", "ignoring incomplete logout request: %zd bytes", nread);
            continue;
        }
        handle_logout(&req);
    }
}

/* 把启动横幅（生效配置、FIFO 路径、协议结构体尺寸）写进 server.log。 */
static void log_startup_info(const ChatConfig *cfg)
{
    log_message("INFO", "config loaded: %s", cfg->full_name);
    log_message("INFO", "data_dir        = %s", cfg->data_dir);
    log_message("INFO", "log_dir         = %s", cfg->log_dir);
    log_message("INFO", "server_fifo_dir = %s", cfg->server_fifo_dir);
    log_message("INFO", "client_fifo_dir = %s", cfg->client_fifo_dir);
    log_message("INFO", "sizeof(ChatPacket)        = %zu", sizeof(ChatPacket));
    log_message("INFO", "sizeof(ChatAuthRequest)   = %zu", sizeof(ChatAuthRequest));
    log_message("INFO", "sizeof(ChatSendRequest)   = %zu", sizeof(ChatSendRequest));
    log_message("INFO", "sizeof(ChatLogoutRequest) = %zu", sizeof(ChatLogoutRequest));
    log_message("INFO", "REG_FIFO    -> %s", cfg->fifo_register);
    log_message("INFO", "LOGIN_FIFO  -> %s", cfg->fifo_login);
    log_message("INFO", "MSG_FIFO    -> %s", cfg->fifo_message);
    log_message("INFO", "LOGOUT_FIFO -> %s", cfg->fifo_logout);
}

/* 守护化的最后一步：把 stdin/stdout/stderr 接到 /dev/null。 */
static void redirect_std_to_devnull(void)
{
    int fd = open("/dev/null", O_RDWR);
    if (fd == -1) return;
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);
}

/*
 * 经典守护进程化（5 步法）：
 *   1) fork，父进程退出 —— 子进程脱离调用者的进程组，不再是组长，setsid 才能成功；
 *   2) setsid，自立新会话与新进程组，脱离控制终端；
 *   3) 设置信号处置（在 main 里完成）；
 *   4) chdir("/")，不再占用启动目录所在文件系统；
 *   5) 重定向标准流到 /dev/null（在打开日志之后执行）。
 * umask(0) 同时是 mkfifo 0666 所必需，放在 main 里 fork 之后。
 */
static int daemonize_step1_fork(void)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        log_message("ERROR", "fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0)
        _exit(EXIT_SUCCESS);   /* 父进程立即退出，shell 立刻拿回提示符 */

    if (setsid() == -1)
    {
        log_message("ERROR", "setsid failed: %s", strerror(errno));
        return -1;
    }
    return 0;
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

    /* 先于守护化加载配置：相对路径（如 config/chatserver.conf）只有在
     * chdir("/") 之前、还在调用者工作目录里时才解析得到。 */
    if (chat_config_load(argv[1], &g_cfg) != 0)
        return EXIT_FAILURE;

    /* 守护化第 1、2 步：fork + setsid。 */
    if (daemonize_step1_fork() != 0)
        return EXIT_FAILURE;

    /* 守护化第 3 步：信号处置。
     * SIGINT/SIGTERM 记录并清理后退出；SIGHUP 忽略（无控制终端，仅防意外）；
     * SIGPIPE 忽略，避免向已关闭的客户端 FIFO 写时进程被杀。 */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /* 守护化第 4 步：切到根目录。 */
    if (chdir("/") == -1)
        return EXIT_FAILURE;

    /* mkfifo / mkdir 之前必须 umask(0)，否则 0666/0700 会被默认 umask 砍掉。 */
    umask(0);

    /* 打开日志（绝对路径，不受 chdir("/") 影响）。此前 g_log_fd==-1，
     * log_message 会退回 stderr——但此刻标准流尚未重定向，仍可见。 */
    if (open_server_log(&g_cfg) != 0)
        return EXIT_FAILURE;

    /* 守护化第 5 步：日志就绪后再把标准流接到 /dev/null。 */
    redirect_std_to_devnull();

    log_message("INFO", "server starting");
    log_startup_info(&g_cfg);

    if (ensure_tree(&g_cfg) == -1) cleanup(EXIT_FAILURE);

    fifo_paths[FIFO_REGISTER] = g_cfg.fifo_register;
    fifo_paths[FIFO_LOGIN]    = g_cfg.fifo_login;
    fifo_paths[FIFO_MESSAGE]  = g_cfg.fifo_message;
    fifo_paths[FIFO_LOGOUT]   = g_cfg.fifo_logout;

    for (i = 0; i < FIFO_COUNT; i++)
        if (open_server_fifo(fifo_paths[i], i) == -1) cleanup(EXIT_FAILURE);

    log_message("INFO", "%s ready (single-threaded select)", g_cfg.full_name);

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
            log_message("ERROR", "select failed: %s", strerror(errno));
            cleanup(EXIT_FAILURE);
        }

        if (FD_ISSET(read_fds[FIFO_REGISTER], &rfds)) drain_auth_fifo(read_fds[FIFO_REGISTER], 1);
        if (FD_ISSET(read_fds[FIFO_LOGIN], &rfds))    drain_auth_fifo(read_fds[FIFO_LOGIN], 0);
        if (FD_ISSET(read_fds[FIFO_MESSAGE], &rfds))  drain_chat_fifo(read_fds[FIFO_MESSAGE]);
        if (FD_ISSET(read_fds[FIFO_LOGOUT], &rfds))   drain_logout_fifo(read_fds[FIFO_LOGOUT]);
    }
}
