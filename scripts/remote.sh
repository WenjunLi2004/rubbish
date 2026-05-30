#!/bin/sh
# scripts/remote.sh
#
# Run an arbitrary command on the chat server, in the project directory.
# Usage:
#   scripts/remote.sh "ls -l data/server_fifo/"
#   scripts/remote.sh "sudo ./bin/chatserver_lwj_1.0.0 config/chatserver.conf &"
#   scripts/remote.sh "make smoke"

set -e

REMOTE="${CHAT_HOST:-chatlab}"
REMOTE_DIR="${CHAT_REMOTE_DIR:-/home/liwenjun2023150001/ChatRoom}"

if [ $# -eq 0 ]; then
    echo "Usage: $0 <command>" >&2
    exit 1
fi

exec ssh "$REMOTE" "cd $REMOTE_DIR && $*"
