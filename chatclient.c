/*
 * chatclient.c
 *
 * 多用户聊天系统客户端（阶段 01）。
 * 启动签名：chatclient <conf> <username> <password>
 * 所有 FIFO 路径来自配置文件，不再硬编码。
 * 新增 /logout：会话级登出，进程不退出；/quit 才退进程。
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "chat_common.h"
#include "src/config.h"

static ChatConfig cfg;
static char       username[CHAT_NAME_LEN];
static char       password[CHAT_PASSWORD_LEN];
static char       myfifo[CHAT_FIFO_PATH_LEN];
static int        fifo_fd = -1;
static int        hold_fd = -1;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    snprintf(dst, dst_size, "%s", src == NULL ? "" : src);
}

static void cleanup(int status)
{
    if (fifo_fd != -1) close(fifo_fd);
    if (hold_fd != -1) close(hold_fd);
    if (myfifo[0] != '\0') unlink(myfifo);
    exit(status);
}

static void handle_signal(int sig)
{
    (void)sig;
    cleanup(EXIT_SUCCESS);
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "%s exists but is not a directory\n", path);
        return -1;
    }
    if (mkdir(path, 0777) == -1) { perror(path); return -1; }
    return 0;
}

static int ensure_tree(void)
{
    if (ensure_dir(cfg.data_dir) == -1) return -1;
    if (ensure_dir(cfg.client_fifo_dir) == -1) return -1;
    return 0;
}

/* 用户名既是身份标识又是 FIFO 文件名，因此不允许出现 '/'、空格等字符。 */
static int valid_username(const char *name)
{
    size_t i;
    if (name[0] == '\0') return 0;
    for (i = 0; name[i] != '\0'; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-')
            return 0;
    return 1;
}

static int prepare_client_fifo(void)
{
    struct stat st;
    snprintf(myfifo, sizeof(myfifo), "%s/%s", cfg.client_fifo_dir, username);

    if (stat(myfifo, &st) == 0)
    {
        if (!S_ISFIFO(st.st_mode))
        {
            fprintf(stderr, "%s exists but is not a FIFO\n", myfifo);
            return -1;
        }
    }
    else if (mkfifo(myfifo, 0600) == -1) { perror(myfifo); return -1; }

    fifo_fd = open(myfifo, O_RDONLY | O_NONBLOCK);
    if (fifo_fd == -1) { perror(myfifo); return -1; }
    hold_fd = open(myfifo, O_WRONLY | O_NONBLOCK);
    if (hold_fd == -1) { perror(myfifo); return -1; }
    return 0;
}

static int write_request(const char *server_fifo, const void *request, size_t size)
{
    int fd = open(server_fifo, O_WRONLY | O_NONBLOCK);
    ssize_t written;
    if (fd == -1) { perror(server_fifo); return -1; }
    written = write(fd, request, size);
    close(fd);
    if (written != (ssize_t)size) { perror("write"); return -1; }
    return 0;
}

static int send_auth_request(const char *server_fifo)
{
    ChatAuthRequest req;
    memset(&req, 0, sizeof(req));
    copy_string(req.username, sizeof(req.username), username);
    copy_string(req.password, sizeof(req.password), password);
    copy_string(req.fifo, sizeof(req.fifo), myfifo);
    return write_request(server_fifo, &req, sizeof(req));
}

static int send_logout_request(void)
{
    ChatLogoutRequest req;
    memset(&req, 0, sizeof(req));
    copy_string(req.username, sizeof(req.username), username);
    copy_string(req.fifo, sizeof(req.fifo), myfifo);
    return write_request(cfg.fifo_logout, &req, sizeof(req));
}

static int send_chat_request(const char *to, const char *text)
{
    ChatSendRequest req;
    memset(&req, 0, sizeof(req));
    copy_string(req.from, sizeof(req.from), username);
    copy_string(req.to, sizeof(req.to), to);
    copy_string(req.text, sizeof(req.text), text);
    return write_request(cfg.fifo_message, &req, sizeof(req));
}

static void print_help(void)
{
    printf("commands:\n");
    printf("  /register             register current username and password\n");
    printf("  /login                login current username and password\n");
    printf("  /logout               logout current session (process keeps running)\n");
    printf("  /send <user> <text>   send text to another online user\n");
    printf("  /quit                 exit client\n");
}

static void print_prompt(void)
{
    printf("chat> ");
    fflush(stdout);
}

static void print_packet(const ChatPacket *packet)
{
    if (packet->type == CHAT_PACKET_MESSAGE)
    {
        printf("\n[%s] %s\n", packet->from, packet->message);
        return;
    }
    if (packet->type == CHAT_PACKET_REPLY)
    {
        printf("\n[server] %s: %s\n", packet->ok ? "OK" : "ERROR", packet->message);
        return;
    }
    printf("\n[server] unknown packet type %d\n", packet->type);
}

static void drain_packets(void)
{
    ChatPacket packet;
    ssize_t nread;
    while (1)
    {
        nread = read(fifo_fd, &packet, sizeof(packet));
        if (nread == (ssize_t)sizeof(packet)) { print_packet(&packet); continue; }
        if (nread == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            perror("read"); cleanup(EXIT_FAILURE);
        }
        if (nread == 0) return;
        fprintf(stderr, "\nIgnoring incomplete packet: %zd bytes\n", nread);
    }
}

static int handle_line(char *line)
{
    char *p, *to, *text;
    line[strcspn(line, "\n")] = '\0';
    if (line[0] == '\0') return 0;

    if (strcmp(line, "/help") == 0) { print_help(); return 0; }
    if (strcmp(line, "/register") == 0) { send_auth_request(cfg.fifo_register); return 0; }
    if (strcmp(line, "/login") == 0)    { send_auth_request(cfg.fifo_login);    return 0; }
    if (strcmp(line, "/logout") == 0)   { send_logout_request();                return 0; }
    if (strcmp(line, "/quit") == 0) return 1;

    if (strncmp(line, "/send ", 6) == 0)
    {
        p = line + 6;
        while (isspace((unsigned char)*p)) p++;
        to = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) p++;
        if (*p == '\0') { printf("Usage: /send <user> <text>\n"); return 0; }
        *p++ = '\0';
        while (isspace((unsigned char)*p)) p++;
        text = p;
        if (to[0] == '\0' || text[0] == '\0')
        {
            printf("Usage: /send <user> <text>\n");
            return 0;
        }
        send_chat_request(to, text);
        return 0;
    }

    printf("Unknown command. Type /help for command list.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    fd_set rfds;
    char   line[512];
    int    maxfd;

    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <config-file> <username> <password>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (chat_config_load(argv[1], &cfg) != 0)
        return EXIT_FAILURE;

    if (!valid_username(argv[2]))
    {
        fprintf(stderr, "Username may contain only letters, digits, '_' and '-'\n");
        return EXIT_FAILURE;
    }

    copy_string(username, sizeof(username), argv[2]);
    copy_string(password, sizeof(password), argv[3]);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    if (ensure_tree() == -1 || prepare_client_fifo() == -1)
        cleanup(EXIT_FAILURE);

    printf("client fifo: %s\n", myfifo);
    print_help();
    print_prompt();

    while (1)
    {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(fifo_fd, &rfds);
        maxfd = fifo_fd > STDIN_FILENO ? fifo_fd : STDIN_FILENO;

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) == -1)
        {
            if (errno == EINTR) continue;
            perror("select"); cleanup(EXIT_FAILURE);
        }

        if (FD_ISSET(fifo_fd, &rfds)) { drain_packets(); print_prompt(); }
        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            if (fgets(line, sizeof(line), stdin) == NULL) break;
            if (handle_line(line)) break;
            print_prompt();
        }
    }
    cleanup(EXIT_SUCCESS);
}
