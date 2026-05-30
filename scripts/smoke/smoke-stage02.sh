#!/usr/bin/env bash
# 阶段 02 端到端冒烟。
# 服务器守护进程化并把日志写到 /var/log/chat-logs/server/server.log，
# 该目录需 root 权限，因此服务器必须用 sudo 启动、用 sudo 读日志。
# 客户端仍以普通用户身份运行（FIFO 创建后会 chown 回普通用户）。
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
    echo "STAGE 02 FAIL: $*"
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

# 1. sudo 必须可用（无交互），否则本阶段无法写 /var/log。
if ! sudo -n true 2>/dev/null; then
    # 给一次交互提权机会；仍失败则明确报错。
    if ! sudo -v 2>/dev/null; then
        echo "STAGE 02 SKIP: sudo is required to write $LOG_DIR but is unavailable"
        exit 2
    fi
fi

# 2. 清场：杀掉残留 daemon（仅本 server+本 conf 的实例），删掉残留公共 FIFO。
OLD_PIDS=$(find_server_pids || true)
if [[ -n "$OLD_PIDS" ]]; then
    echo "[smoke] killing stale server: $OLD_PIDS"
    # shellcheck disable=SC2086
    sudo kill -TERM $OLD_PIDS 2>/dev/null || true
    sleep 0.3
fi
sudo rm -f "$FIFO_DIR"/* "$DATA_DIR/client_fifo/$USER" 2>/dev/null || true

# 3. 用 sudo 启动服务器；它会 fork+setsid 守护化，父进程立即退出。
sudo "$SERVER" "$CONF" || fail "server failed to launch"
sleep 1

# 4. 守护化后父进程已退出，必须按 server+conf 精确定位真正的 daemon pid（不是 $!）。
SERVER_PID=$(find_server_pids | head -n1)
[[ -n "$SERVER_PID" ]] || fail "daemon pid not found (server=$SERVER conf=$CONF)"
echo "[smoke] daemon pid = $SERVER_PID"
ps -o pid,ppid,comm -p "$SERVER_PID" 2>/dev/null || true

# 5. 4 个公共 FIFO 应存在且类型为 FIFO。
for name in lwjregister lwjlogin lwjsendmsg lwjlogout; do
    path="$FIFO_DIR/$name"
    [[ -p "$path" ]] || fail "FIFO not created: $path"
done
echo "[smoke] 4 FIFOs OK:"; ls -l "$FIFO_DIR"

# 6. 注册 → 期望 register ok。sleep 放宽到 1s，避免客户端在服务器回包前就 /quit unlink 私有 FIFO。
OUT_REG=$( ( echo "/register"; sleep 1; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_REG" | grep -q "OK: register ok" || fail "register did not succeed; output: $OUT_REG"
echo "[smoke] register ok"

# 7. 登录 → 登出 → 期望两条 OK。
OUT_LOGIN=$( ( echo "/login"; sleep 1; echo "/logout"; sleep 1; echo "/quit" ) | "$CLIENT" "$CONF" "$USER" "$PASS" 2>&1 || true )
echo "$OUT_LOGIN" | grep -q "OK: login ok"  || fail "login did not succeed; output: $OUT_LOGIN"
echo "$OUT_LOGIN" | grep -q "OK: logout ok" || fail "logout did not succeed; output: $OUT_LOGIN"
echo "[smoke] login + logout ok"

# 8. 校验 server.log：存在、权限约为 0600、含关键事件。
sudo test -f "$SERVER_LOG" || fail "server.log not found: $SERVER_LOG"
MODE=$(sudo stat -c %a "$SERVER_LOG" 2>/dev/null || sudo stat -f %Lp "$SERVER_LOG" 2>/dev/null || echo "?")
echo "[smoke] server.log mode = $MODE"
[[ "$MODE" == "600" ]] || fail "server.log mode is $MODE (expected 600)"

LOGTXT=$(sudo cat "$SERVER_LOG" 2>/dev/null || true)
for needle in "server starting" "ready" "registered user: $USER" "login user: $USER" "logout user: $USER"; do
    echo "$LOGTXT" | grep -q "$needle" || fail "server.log missing entry: '$needle'"
done
echo "[smoke] server.log has startup / ready / register / login / logout entries"

# 9. 给 daemon 发 SIGTERM，确认 4 个公共 FIFO 被清理。
sudo kill -TERM "$SERVER_PID"
SERVER_PID=""
sleep 0.4
remaining=$(ls "$FIFO_DIR" 2>/dev/null | wc -l | tr -d ' ')
[[ "$remaining" == "0" ]] || fail "FIFOs not cleaned up after SIGTERM: $(ls "$FIFO_DIR")"
echo "[smoke] FIFOs cleaned up after SIGTERM"

echo "STAGE 02 PASS"
