/*
 * config.c
 *
 * 解析 `key = value` 风格的配置文件，并填充 ChatConfig 的派生字段。
 * 调用者保证 out 非空；本函数失败时不保证 out 中字段状态可用。
 */

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 去掉字符串两端的空白字符，原地修改并返回起始指针。 */
static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* 拷贝字符串到固定长度字段；超长返回 -1，避免静默截断。 */
static int copy_field(char *dst, size_t dst_size, const char *src, const char *key) {
    size_t len = strlen(src);
    if (len + 1 > dst_size) {
        fprintf(stderr, "config: value for '%s' too long (max %zu)\n", key, dst_size - 1);
        return -1;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

/* 按 snprintf 拼接派生字段，截断即报错退出。 */
static int join_path(char *dst, size_t dst_size, const char *name,
                     const char *fmt, const char *a, const char *b) {
    int n = snprintf(dst, dst_size, fmt, a, b);
    if (n < 0 || (size_t)n >= dst_size) {
        fprintf(stderr, "config: derived path '%s' too long\n", name);
        return -1;
    }
    return 0;
}

int chat_config_load(const char *path, ChatConfig *out) {
    FILE *fp;
    char  line[1024];
    int   lineno = 0;
    int   have_server = 0, have_short = 0, have_version = 0;
    int   have_data = 0, have_log = 0, have_prefix = 0;
    int   have_pool = 0;

    memset(out, 0, sizeof(*out));
    out->poolsize = 10;  /* 缺省值；后续校验仍要求 >0 */

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "config: cannot open '%s': ", path);
        perror("");
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p, *eq, *key, *val;
        lineno++;

        p = trim(line);
        if (*p == '\0' || *p == '#') continue;

        eq = strchr(p, '=');
        if (!eq) {
            fprintf(stderr, "config: line %d missing '='\n", lineno);
            fclose(fp);
            return -1;
        }

        *eq = '\0';
        key = trim(p);
        val = trim(eq + 1);

        if (strcmp(key, "server_name") == 0) {
            if (copy_field(out->server_name, sizeof(out->server_name), val, key) < 0) goto fail;
            have_server = 1;
        } else if (strcmp(key, "short_name") == 0) {
            if (copy_field(out->short_name, sizeof(out->short_name), val, key) < 0) goto fail;
            have_short = 1;
        } else if (strcmp(key, "version") == 0) {
            if (copy_field(out->version, sizeof(out->version), val, key) < 0) goto fail;
            have_version = 1;
        } else if (strcmp(key, "data_dir") == 0) {
            if (copy_field(out->data_dir, sizeof(out->data_dir), val, key) < 0) goto fail;
            have_data = 1;
        } else if (strcmp(key, "log_dir") == 0) {
            if (copy_field(out->log_dir, sizeof(out->log_dir), val, key) < 0) goto fail;
            have_log = 1;
        } else if (strcmp(key, "fifo_prefix") == 0) {
            if (copy_field(out->fifo_prefix, sizeof(out->fifo_prefix), val, key) < 0) goto fail;
            have_prefix = 1;
        } else if (strcmp(key, "poolsize") == 0) {
            char *endp;
            long v = strtol(val, &endp, 10);
            if (*endp != '\0' || v <= 0 || v > 100000) {
                fprintf(stderr, "config: poolsize must be a positive integer (got '%s')\n", val);
                goto fail;
            }
            out->poolsize = (int)v;
            have_pool = 1;
        } else {
            fprintf(stderr, "config: warning: unknown key '%s' at line %d\n", key, lineno);
        }
    }

    fclose(fp);
    fp = NULL;

    if (!have_server || !have_short || !have_version ||
        !have_data || !have_log || !have_prefix) {
        fprintf(stderr, "config: missing required keys (server_name/short_name/version/"
                        "data_dir/log_dir/fifo_prefix)\n");
        return -1;
    }
    (void)have_pool;  /* poolsize 缺省 10，可选 */

    /* 派生字段拼装。 */
    if (join_path(out->full_name, sizeof(out->full_name), "full_name",
                  "%s_%s", out->server_name, out->short_name) < 0) return -1;
    {
        /* full_name 还要再追加 _version；前一步只拼了 server_short，再补一次。 */
        char tmp[96];
        int  n = snprintf(tmp, sizeof(tmp), "%s_%s", out->full_name, out->version);
        if (n < 0 || (size_t)n >= sizeof(tmp)) {
            fprintf(stderr, "config: derived 'full_name' too long\n");
            return -1;
        }
        memcpy(out->full_name, tmp, (size_t)n + 1);
    }

    if (join_path(out->server_fifo_dir, sizeof(out->server_fifo_dir), "server_fifo_dir",
                  "%s/%s", out->data_dir, "server_fifo") < 0) return -1;
    if (join_path(out->client_fifo_dir, sizeof(out->client_fifo_dir), "client_fifo_dir",
                  "%s/%s", out->data_dir, "client_fifo") < 0) return -1;

    {
        char fname[64];
        int  n;

        n = snprintf(fname, sizeof(fname), "%sregister", out->fifo_prefix);
        if (n < 0 || (size_t)n >= sizeof(fname)) goto fname_too_long;
        if (join_path(out->fifo_register, sizeof(out->fifo_register), "fifo_register",
                      "%s/%s", out->server_fifo_dir, fname) < 0) return -1;

        n = snprintf(fname, sizeof(fname), "%slogin", out->fifo_prefix);
        if (n < 0 || (size_t)n >= sizeof(fname)) goto fname_too_long;
        if (join_path(out->fifo_login, sizeof(out->fifo_login), "fifo_login",
                      "%s/%s", out->server_fifo_dir, fname) < 0) return -1;

        n = snprintf(fname, sizeof(fname), "%ssendmsg", out->fifo_prefix);
        if (n < 0 || (size_t)n >= sizeof(fname)) goto fname_too_long;
        if (join_path(out->fifo_message, sizeof(out->fifo_message), "fifo_message",
                      "%s/%s", out->server_fifo_dir, fname) < 0) return -1;

        n = snprintf(fname, sizeof(fname), "%slogout", out->fifo_prefix);
        if (n < 0 || (size_t)n >= sizeof(fname)) goto fname_too_long;
        if (join_path(out->fifo_logout, sizeof(out->fifo_logout), "fifo_logout",
                      "%s/%s", out->server_fifo_dir, fname) < 0) return -1;

        if (0) {
fname_too_long:
            fprintf(stderr, "config: fifo_prefix too long\n");
            return -1;
        }
    }

    if (join_path(out->log_dir_server, sizeof(out->log_dir_server), "log_dir_server",
                  "%s/%s", out->log_dir, "server") < 0) return -1;
    if (join_path(out->log_dir_users, sizeof(out->log_dir_users), "log_dir_users",
                  "%s/%s", out->log_dir, "users") < 0) return -1;
    if (join_path(out->server_log_path, sizeof(out->server_log_path), "server_log_path",
                  "%s/%s", out->log_dir_server, "server.log") < 0) return -1;
    if (join_path(out->threads_log_path, sizeof(out->threads_log_path), "threads_log_path",
                  "%s/%s", out->log_dir_server, "threads.log") < 0) return -1;

    return 0;

fail:
    if (fp) fclose(fp);
    return -1;
}
