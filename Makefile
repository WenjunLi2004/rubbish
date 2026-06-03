SHORT_NAME := $(shell awk -F= '/^[[:space:]]*short_name/{gsub(/[ \t]/,""); print $$2}' config/chatserver.conf)
VERSION    := $(shell awk -F= '/^[[:space:]]*version/{gsub(/[ \t]/,""); print $$2}' config/chatserver.conf)
SERVER_BIN := bin/chatserver_$(SHORT_NAME)_$(VERSION)
CLIENT_BIN := bin/chatclient

UNAME_S := $(shell uname -s)

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=gnu11 -Isrc -I.
LDFLAGS :=

# 服务器使用线程池 + 共享内存 + 进程间互斥锁，需要 pthread。
# Linux 上 shm_open 在较老 glibc 里位于 librt，故链接 -lrt（新 glibc 无害）。
SERVER_LDLIBS := -pthread
ifeq ($(UNAME_S),Linux)
SERVER_LDLIBS += -lrt
endif

SERVER_SRC := chatserver.c src/config.c src/user_store.c src/thread_pool.c

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(SERVER_LDLIBS)

$(CLIENT_BIN): chatclient.c src/config.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf bin

# `make smoke` 跑 2026 试题对齐版冒烟。旧的分阶段冒烟保留为 legacy 目标。
smoke: smoke-2026

smoke-2026: all
	scripts/smoke/smoke-2026.sh

smoke-stage01: all
	scripts/smoke/smoke-stage01.sh

smoke-stage02: all
	scripts/smoke/smoke-stage02.sh

smoke-stage03: all
	scripts/smoke/smoke-stage03.sh

.PHONY: all clean smoke smoke-2026 smoke-stage01 smoke-stage02 smoke-stage03
