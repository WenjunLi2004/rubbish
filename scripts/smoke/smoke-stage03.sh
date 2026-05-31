#!/usr/bin/env bash
# 阶段 03 端到端冒烟。
# 在阶段 02（守护化 + server.log）基础上，验证用户表已迁入 POSIX 共享内存：
#   - /dev/shm 下出现共享内存对象，权限 0600
#   - 注册 / 重名拒绝 / 登录 / 登出 生命周期仍然正常
#   - server.log 记录共享内存初始化与各业务事件
#   - 正常关闭后共享内存对象被 shm_unlink 删除
# 需要 Linux /dev/shm + sudo（写 /var/log 与以 root 起 daemon）。目标环境是 euler。
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
LOG_DIR=$(awk -F= '/^[[:space:]]*log_dir/{gsub(/[ \t]/,""); print $2}' "$CONF")
FIFO_DIR="$DATA_DIR/server_fifo"
SERVER_LOG="$LOG_DIR/server/server.log"
# 共享内存对象名由 short_name 派生：/chatroom_<short>_users -> /dev/shm/chatroom_<short>_users
SHM_NAME="chatroom_${SHORT}_users"
SHM_FILE="/dev/shm/${SHM_NAME}"

USER="lwj_smoke"
PASS="1234"

SERVER_PID=""

# 只匹配“命令行里同时含本 $SERVER 绝对路径和本 $CONF 绝对路径”的进程，
# 用 index() 做子串精确匹配，避免误伤同名但不同实例/配置的 chatserver。
find_server_pids() {
    ps -axo pid,args 2>/dev/null | awk -v s="$SERVER" -v c="$CONF" '
        index($0, s) > 0 && index($0, c) > 0 { print $1 }'
}

fail() {
    echo "STAGE 03 FAIL: $*"
    echo "---- tail of $SERVER_LOG ----" >&2
    sudo tail -n 40 "$SERVER_LOG" 2>/dev/null >&2 || true
    cleanup
    exit 1
}

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && sudo kill -0 "$SERVER_PID" 2>/dev/null; then
        sudo kill -TERM "$SERVER_PID" 2>/dev/null || true
        sleep 0.3
    fi
}
trap cleanup EXIT

# 0. 构建（如缺二进制）。
if [[ ! -x "$SERVER" || ! -x "$CLIENT" ]]; then
    echo "[smoke] building binaries..."
    make -C "$ROOT" >/dev/null || fail "build failed"
fi
[[ -x "$SERVER" ]] || fail "server binary not found: $SERVER"
[[ -x "$CLIENT" ]] || fail "client binary not found: $CLIENT"

# 1. 要求 Linux /dev/shm；缺失（如 macOS）则明确 SKIP，不伪造通过。
if [[ ! -d /dev/shm ]]; then
    echo "STAGE 03 SKIP: /dev/shm not available (need Linux euler container for POSIX shm)"
    exit 2
fi

# 2. sudo 必须可用，否则无法写 /var/log、无法以 root 起 daemon。
if ! sudo -n true 2>/dev/null; then
    if ! sudo -v 2>/dev/null; then
        echo "STAGE 03 SKIP: sudo is required to write $LOG_DIR but is unavailable"
        exit 2
    fi
fi

# 3. 清场：杀掉残留 daemon（仅本 server+本 conf 的实例）。
OLD_PIDS=$(find_server_pids || true)
if [[ -n "$OLD_PIDS" ]]; then
    echo "[smoke] killing stale server: $OLD_PIDS"
    # shellcheck disable=SC2086
    sudo kill -TERM $OLD_PIDS 2>/dev/null || true
    sleep 0.3
fi
# 4. 删掉残留公共/客户端 FIFO。
sudo rm -f "$FIFO_DIR"/* "$DATA_DIR/client_fifo/$USER" 2>/dev/null || true
# 5. 仅当没有本实例在运行时，删掉残留共享内存对象（可能由 kill -9 残留）。
if [[ -z "$(find_server_pids || true)" && -e "$SHM_FILE" ]]; then
    echo "[smoke] removing stale shm object: $SHM_FILE"
    sudo rm -f "$SHM_FILE" 2>/dev/null || true
fi

# 6. 用 sudo 启动服务器；fork+setsid 守护化，父进程立即退出。
sudo "$SERVER" "$CONF" || fail "server failed to launch"
sleep 1

# 7. 守护化后必须按 server+conf 精确定位真正的 daemon pid（不是 $!）。
SERVER_PID=$(find_server_pids | head -n1)
[[ -n "$SERVER_PID" ]] || fail "daemon pid not found (server=$SERVER conf=$CONF)"
echo "[smoke] daemon pid = $SERVER_PID"
ps -o pid,ppid,comm -p "$SERVER_PID" 2>/dev/null || true

# 8. 共享内存对象应存在于 /dev/shm 且权限为 0600。
[[ -e "$SHM_FILE" ]] || fail "shm object not found: $SHM_FILE"
SHM_MODE=$(sudo stat -c %a "$SHM_FILE" 2>/dev/null || echo "?")
echo "[smoke] shm object $SHM_FILE mode = $SHM_MODE"
[[ "$SHM_MODE" == "600" ]] || fail "shm object mode is $SHM_MODE (expected 600)"

# 9. 4 个公共 FIFO 仍应存在且类型为 FIFO。
for name in lwjregister lwjlogin lwjsendmsg lwjlogout; do
    path="$FIFO_DIR/$name"
    [[ -p "$path" ]] || fail "FIFO not created: $path"
done
echo "[smoke] 4 FIFOs OK"

# 10. 注册 → 期望 register ok。
OUT_REG=$( ( echo "/register"; sleep 1; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_REG" | grep -q "OK: register ok" || fail "register did not succeed; output: $OUT_REG"
echo "[smoke] register ok"

# 11. 再次注册同名 → 期望 username already exists（证明 shm 表跨客户端持久）。
OUT_DUP=$( ( echo "/register"; sleep 1; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_DUP" | grep -q "ERROR: username already exists" || fail "duplicate register not rejected; output: $OUT_DUP"
echo "[smoke] duplicate register rejected"

# 12. 登录 → 登出 → 期望两条 OK。
OUT_LOGIN=$( ( echo "/login"; sleep 1; echo "/logout"; sleep 1; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_LOGIN" | grep -q "OK: login ok"  || fail "login did not succeed; output: $OUT_LOGIN"
echo "$OUT_LOGIN" | grep -q "OK: logout ok" || fail "logout did not succeed; output: $OUT_LOGIN"
echo "[smoke] login + logout ok"

# 13. 校验 server.log：共享内存初始化 + 注册 / 重名拒绝 / 登录 / 登出。
sudo test -f "$SERVER_LOG" || fail "server.log not found: $SERVER_LOG"
LOGTXT=$(sudo cat "$SERVER_LOG" 2>/dev/null || true)
for needle in \
    "shm user store ready" \
    "registered user: $USER" \
    "duplicate register rejected: $USER" \
    "login user: $USER" \
    "logout user: $USER"; do
    echo "$LOGTXT" | grep -q "$needle" || fail "server.log missing entry: '$needle'"
done
echo "[smoke] server.log has shm-init / register / duplicate / login / logout entries"

# 14. 给 daemon 发 SIGTERM。
sudo kill -TERM "$SERVER_PID"
SERVER_PID=""
sleep 0.5

# 15. 确认 4 个公共 FIFO 被清理。
remaining=$(ls "$FIFO_DIR" 2>/dev/null | wc -l | tr -d ' ')
[[ "$remaining" == "0" ]] || fail "FIFOs not cleaned up after SIGTERM: $(ls "$FIFO_DIR")"
echo "[smoke] FIFOs cleaned up after SIGTERM"

# 16. 确认共享内存对象在正常关闭后被 shm_unlink 删除。
[[ ! -e "$SHM_FILE" ]] || fail "shm object not removed after shutdown: $SHM_FILE"
echo "[smoke] shm object removed after shutdown"

echo "STAGE 03 PASS"
