#!/bin/sh
# scripts/logs.sh
#
# Fetch the latest server-side logs to ./tmp/logs/ for local review.
# Uses sudo on the remote (which is configured NOPASSWD).

set -e

REMOTE="${CHAT_HOST:-chatlab}"
OUT_DIR="${CHAT_LOGS_OUT:-tmp/logs}"

mkdir -p "$OUT_DIR/server" "$OUT_DIR/users"

echo "[logs] fetching server.log..."
ssh "$REMOTE" "sudo cat /var/log/chat-logs/server/server.log 2>/dev/null || true" \
    > "$OUT_DIR/server/server.log"

echo "[logs] fetching threads.log (stage 9+)..."
ssh "$REMOTE" "sudo cat /var/log/chat-logs/server/threads.log 2>/dev/null || true" \
    > "$OUT_DIR/server/threads.log"

echo "[logs] fetching per-user logs..."
USERS=$(ssh "$REMOTE" "sudo ls /var/log/chat-logs/users/ 2>/dev/null || true")
for u in $USERS; do
    ssh "$REMOTE" "sudo cat /var/log/chat-logs/users/$u" > "$OUT_DIR/users/$u" 2>/dev/null || true
    echo "  - $u"
done

echo "[logs] saved to $OUT_DIR/"
