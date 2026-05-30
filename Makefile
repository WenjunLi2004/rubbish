SHORT_NAME := $(shell awk -F= '/^[[:space:]]*short_name/{gsub(/[ \t]/,""); print $$2}' config/chatserver.conf)
VERSION    := $(shell awk -F= '/^[[:space:]]*version/{gsub(/[ \t]/,""); print $$2}' config/chatserver.conf)
SERVER_BIN := bin/chatserver_$(SHORT_NAME)_$(VERSION)
CLIENT_BIN := bin/chatclient

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=gnu11 -Isrc -I.
LDFLAGS :=

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): chatserver.c src/config.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): chatclient.c src/config.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf bin

# `make smoke` 默认跑当前阶段（02）的冒烟。保留 smoke-stage01 便于回归对比。
smoke: smoke-stage02

smoke-stage01: all
	scripts/smoke/smoke-stage01.sh

smoke-stage02: all
	scripts/smoke/smoke-stage02.sh

.PHONY: all clean smoke smoke-stage01 smoke-stage02
