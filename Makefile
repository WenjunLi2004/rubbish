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

smoke: all
	scripts/smoke/smoke-stage01.sh

.PHONY: all clean smoke
