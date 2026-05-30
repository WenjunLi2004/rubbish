#!/usr/bin/env bash
# 阶段 01 端到端冒烟。本阶段不写 /var/log，因此不用 sudo。
set -u
set -o pipefail

cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
CONF="$ROOT/config/chatserver.conf"
SHORT=$(awk -F= '/^[[:space:]]*short_name/{gsub(/[ \t]/,""); print $2}' "$CONF")
VER=$(awk -F= '/^[[:space:]]*version/{gsub(/[ \t]/,""); print $2}' "$CONF")
SERVER="$ROOT/bin/chatserver_${SHORT}_${VER}"
CLIENT="$ROOT/bin/chatclient"
DATA_DIR=$(awk -F= '/^[[:space:]]*data_dir/{gsub(/[ \t]/,""); print $2}' "$CONF")
FIFO_DIR="$DATA_DIR/server_fifo"

LOG="$ROOT/scripts/smoke/.smoke.log"
SERVER_LOG="$ROOT/scripts/smoke/.server.log"
: > "$LOG"
: > "$SERVER_LOG"

fail() { echo "STAGE 01 FAIL: $*"; tail -n 50 "$SERVER_LOG" >&2 || true; cleanup; exit 1; }

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

[[ -x "$SERVER" ]] || fail "server binary not found: $SERVER"
[[ -x "$CLIENT" ]] || fail "client binary not found: $CLIENT"

# 1. 清场：删除可能残留的 FIFO 与用户 FIFO。
rm -f "$FIFO_DIR"/* "$DATA_DIR/client_fifo/lwj_smoke" 2>/dev/null || true

# 2. 启动服务器。
"$SERVER" "$CONF" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 0.5

# 3. 检查 4 个 FIFO 存在且类型正确。
for name in lwjregister lwjlogin lwjsendmsg lwjlogout; do
    path="$FIFO_DIR/$name"
    [[ -p "$path" ]] || fail "FIFO not created: $path"
done
echo "[smoke] 4 FIFOs OK:"; ls -l "$FIFO_DIR" | tee -a "$LOG"

# 4. 注册 → 期望 register ok。
USER="lwj_smoke"
PASS="1234"
OUT_REG=$( ( echo "/register"; sleep 0.4; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_REG" >> "$LOG"
echo "$OUT_REG" | grep -q "OK: register ok" || fail "register did not succeed; output: $OUT_REG"
echo "[smoke] register ok"

# 5. 登录 → 登出 → 期望两条 OK。
OUT_LOGIN=$( ( echo "/login"; sleep 0.4; echo "/logout"; sleep 0.4; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_LOGIN" >> "$LOG"
echo "$OUT_LOGIN" | grep -q "OK: login ok"  || fail "login did not succeed; output: $OUT_LOGIN"
echo "$OUT_LOGIN" | grep -q "OK: logout ok" || fail "logout did not succeed; output: $OUT_LOGIN"
echo "[smoke] login + logout ok"

# 6. 关服务器，验证 4 个 FIFO 被 unlink。
kill -TERM "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
sleep 0.2
remaining=$(ls "$FIFO_DIR" 2>/dev/null | wc -l)
if [[ "$remaining" -ne 0 ]]; then
    fail "FIFOs not cleaned up after server exit: $(ls "$FIFO_DIR")"
fi
echo "[smoke] FIFOs cleaned up"

echo "STAGE 01 PASS"
