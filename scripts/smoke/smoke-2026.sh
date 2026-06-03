#!/usr/bin/env bash
# 2026 试题对齐版端到端冒烟。
#
# 覆盖：二进制名 chatserver_lwj_1.0、守护化、~/Server/fifo/lwj_*_fifo 4 个公共 FIFO、
# server.log + threads.log（0600）、线程池 100 线程、注册/重名拒绝、登录(含在线名单)/
# 失败登录、在线时一对一 5 次发送、登出广播、离线暂存与重登回推、机器人增/发/删、
# SIGTERM 后 FIFO 与 shm 清理 + 线程池干净关闭。
#
# 日志现位于 ~/log/chat-logs（用户家目录），因此本脚本可在“有 sudo”与“无 sudo”两种
# 模式下运行：试题演示用 sudo 启动（日志 root 所有）；自动验证无密码时退回非 sudo 模式
# （日志归当前用户、其余功能不变）。需 Linux /dev/shm。
set -u
set -o pipefail

cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
CONF="$ROOT/config/chatserver.conf"
get() { awk -F= -v k="$1" '$0 ~ "^[[:space:]]*"k"[[:space:]]*=" {gsub(/[ \t]/,""); sub("^"k"=",""); print; exit}' "$CONF"; }
SHORT=$(get short_name); VER=$(get version); PREFIX=$(get fifo_prefix)
FIFO_DIR=$(get fifo_dir); CLIENT_DIR=$(get client_fifo_dir); LOG_DIR=$(get log_dir)
SERVER="$ROOT/bin/chatserver_${SHORT}_${VER}"
CLIENT="$ROOT/bin/chatclient"
SERVER_LOG="$LOG_DIR/server/server.log"
THREADS_LOG="$LOG_DIR/server/threads.log"
SHM_FILE="/dev/shm/chatroom_${SHORT}_users"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/smoke2026.XXXXXX")
C1_IN="$WORK/c1.in"; C1_OUT="$WORK/c1.out"
C2_IN="$WORK/c2.in"; C2_OUT="$WORK/c2.out"
SERVER_PID=""
C1_PID=""; C2_PID=""

find_server_pids() {
    ps -axo pid,args 2>/dev/null | awk -v s="$SERVER" -v c="$CONF" \
        'index($0,s)>0 && index($0,c)>0 {print $1}'
}

cleanup() {
    [[ -n "${C1_PID:-}" ]] && kill "$C1_PID" 2>/dev/null || true
    [[ -n "${C2_PID:-}" ]] && kill "$C2_PID" 2>/dev/null || true
    exec 3>&- 2>/dev/null || true
    exec 4>&- 2>/dev/null || true
    if [[ -n "${SERVER_PID:-}" ]] && $SUDO kill -0 "$SERVER_PID" 2>/dev/null; then
        $SUDO kill -TERM "$SERVER_PID" 2>/dev/null || true
        sleep 0.4
    fi
    rm -rf "$WORK"
}
fail() { echo "STAGE 2026 FAIL: $*"; echo "--- server.log tail ---" >&2; $SUDO tail -n 30 "$SERVER_LOG" 2>/dev/null >&2 || true; cleanup; exit 1; }
trap cleanup EXIT

# 0. 构建。
[[ -x "$SERVER" && -x "$CLIENT" ]] || { echo "[smoke] building..."; make -C "$ROOT" >/dev/null || fail "build failed"; }
# 1. 二进制名必须是 chatserver_lwj_1.0。
[[ "$SERVER" == *"/chatserver_lwj_1.0" ]] || fail "unexpected server binary name: $SERVER"
echo "[smoke] server binary: $(basename "$SERVER")"

# 2. 需要 Linux /dev/shm。
[[ -d /dev/shm ]] || { echo "STAGE 2026 SKIP: /dev/shm absent (need Linux euler container)"; exit 2; }

# 3. 决定是否用 sudo（试题用 sudo；无密码时退回非 sudo）。
SUDO=""
if sudo -n true 2>/dev/null; then SUDO="sudo"
elif sudo -v 2>/dev/null; then SUDO="sudo"
else echo "[smoke] sudo unavailable -> running WITHOUT sudo (logs owned by current user)"; fi

# 4. 清场。
for p in $(find_server_pids); do $SUDO kill -TERM "$p" 2>/dev/null || true; done
sleep 0.4
$SUDO rm -f "$FIFO_DIR"/${PREFIX}_*_fifo 2>/dev/null || true
rm -f "$CLIENT_DIR"/liwenjun_1 "$CLIENT_DIR"/liwenjun_2 "$CLIENT_DIR"/liwenjun_3 2>/dev/null || true
[[ -z "$(find_server_pids)" && -e "$SHM_FILE" ]] && $SUDO rm -f "$SHM_FILE" 2>/dev/null || true

# 5. 启动守护进程。
$SUDO "$SERVER" "$CONF" || fail "server failed to launch"
sleep 1
SERVER_PID=$(find_server_pids | head -n1)
[[ -n "$SERVER_PID" ]] || fail "daemon pid not found"
echo "[smoke] daemon pid=$SERVER_PID"
ps -o pid,ppid,comm -p "$SERVER_PID" 2>/dev/null || true

# 6. 4 个公共 FIFO。
for n in ${PREFIX}_reg_fifo ${PREFIX}_login_fifo ${PREFIX}_msg_fifo ${PREFIX}_logout_fifo; do
    [[ -p "$FIFO_DIR/$n" ]] || fail "missing FIFO: $FIFO_DIR/$n"
done
echo "[smoke] 4 public FIFOs OK"

# 7. server.log / threads.log 存在且 0600。
$SUDO test -f "$SERVER_LOG"  || fail "server.log missing"
$SUDO test -f "$THREADS_LOG" || fail "threads.log missing"
[[ "$($SUDO stat -c %a "$SERVER_LOG")"  == "600" ]] || fail "server.log not 0600"
[[ "$($SUDO stat -c %a "$THREADS_LOG")" == "600" ]] || fail "threads.log not 0600"
echo "[smoke] server.log + threads.log exist, mode 600"

# 8. 启动日志含启动时间与 100 线程线程池。
LOG=$($SUDO cat "$SERVER_LOG")
echo "$LOG" | grep -q "server starting at"          || fail "no startup time in server.log"
echo "$LOG" | grep -q "thread pool created: 100 threads" || fail "no 100-thread pool log"
echo "[smoke] startup time + thread pool(100) logged"

# 8b. Linux/euler 上必须证明用户表互斥锁是进程间共享（PTHREAD_PROCESS_SHARED）。
echo "$LOG" | grep -q "user store mutex initialized as process-shared" \
    || fail "server.log lacks process-shared mutex confirmation (required on Linux/euler)"
echo "[smoke] user-store mutex confirmed process-shared"

# 9. 注册 liwenjun_1 / liwenjun_2 / 重名 liwenjun_2。
reg() { ( echo "/register"; sleep .5; echo "/quit" ) | "$CLIENT" "$CONF" "$1" "$2" 2>&1; }
reg liwenjun_1 pw1 | grep -q "OK: register ok"            || fail "register liwenjun_1"
reg liwenjun_2 pw2 | grep -q "OK: register ok"            || fail "register liwenjun_2"
reg liwenjun_2 pw2 | grep -q "ERROR: username already exists" || fail "dup liwenjun_2 not rejected"
echo "[smoke] register + duplicate rejection OK"

# 10. 失败登录 liwenjun_3（未注册）。
( echo "/login"; sleep .5; echo "/quit" ) | "$CLIENT" "$CONF" liwenjun_3 pw3 2>&1 \
    | grep -q "ERROR: username does not exist" || fail "failed login liwenjun_3 not rejected"
echo "[smoke] failed login liwenjun_3 rejected"

# 10b. 注册但【未登录】的用户不能管理机器人（机器人管理是在线客户端动作）。
# 先注册 liwenjun_3（注册不等于登录），再用同名客户端只发 /bot add 不登录：
# 服务器应回 "bot manager requires login"，且不得真的创建机器人。
reg liwenjun_3 pw3 | grep -q "OK: register ok" || fail "register liwenjun_3 (bot-auth test)"
NOLOGIN_BOT=$( ( echo "/bot add 1"; sleep 1; echo "/quit" ) | "$CLIENT" "$CONF" liwenjun_3 pw3 2>&1 )
echo "$NOLOGIN_BOT" | grep -q "bot manager requires login" \
    || fail "registered-but-not-logged-in user was allowed to manage bots"
echo "$NOLOGIN_BOT" | grep -q "added" && fail "bot was created by a not-logged-in user"
echo "[smoke] registered-but-not-logged-in user cannot manage bots"

# 11. 起两个持久客户端（stdin 走 FIFO，stdout 落文件），观察异步广播/消息。
mkfifo "$C1_IN" "$C2_IN"
: > "$C1_OUT"; : > "$C2_OUT"
"$CLIENT" "$CONF" liwenjun_1 pw1 < "$C1_IN" > "$C1_OUT" 2>&1 & C1_PID=$!
"$CLIENT" "$CONF" liwenjun_2 pw2 < "$C2_IN" > "$C2_OUT" 2>&1 & C2_PID=$!
exec 3>"$C1_IN"; exec 4>"$C2_IN"
s1() { echo "$*" >&3; }
s2() { echo "$*" >&4; }
sleep 0.5

s1 "/login"; sleep 0.8
grep -q "login ok; online" "$C1_OUT" || fail "liwenjun_1 login reply lacks online list"
s2 "/login"; sleep 0.9
grep -q "login ok" "$C2_OUT" || fail "liwenjun_2 login failed"
grep -Eq "liwenjun_2 is now online|online 2" "$C1_OUT" || fail "liwenjun_1 did not see liwenjun_2 join"
echo "[smoke] login + online-list broadcast OK"

# 12. liwenjun_1 -> liwenjun_2 发 5 次（在线，应实时收到）。
for i in 1 2 3 4 5; do s1 "/send liwenjun_2 How are you, liwenjun_2"; sleep 0.3; done
sleep 0.6
RECV=$(grep -c "How are you, liwenjun_2" "$C2_OUT")
[[ "$RECV" -ge 5 ]] || fail "liwenjun_2 received $RECV/5 live messages"
SENTLOG=$($SUDO cat "$SERVER_LOG" | grep -c "(liwenjun_1, liwenjun_2, .*, sent)")
[[ "$SENTLOG" -ge 5 ]] || fail "server.log shows $SENTLOG/5 sent tuples"
echo "[smoke] 5 live sends delivered + logged"

# 12b. 第 6 条成功发送（累计 >5）应在发送方 ack 里把对方标记为重要朋友 *。
# 用 grep -F 按字面匹配（'*' 不当正则），避免前 5 条无星 ack 误命中。
s1 "/send liwenjun_2 You are my important friend now"; sleep 0.6
grep -qF "message sent to liwenjun_2*" "$C1_OUT" \
    || fail "important-friend * not marked on the 6th (>5) successful send"
echo "[smoke] important-friend * threshold OK (6th send marked liwenjun_2*)"

# 13. liwenjun_2 登出 -> liwenjun_1 看到登出与名单更新。
s2 "/logout"; sleep 0.8
grep -Eq "liwenjun_2 has logged out|online 1" "$C1_OUT" || fail "liwenjun_1 did not see liwenjun_2 logout"
echo "[smoke] logout broadcast OK"

# 14. liwenjun_2 离线时收到离线消息 -> 暂存。
s1 "/send liwenjun_2 Hi, let's play badminton?"; sleep 0.6
grep -q "pending" "$C1_OUT" || fail "offline message not marked pending"
$SUDO cat "$SERVER_LOG" | grep -q "(liwenjun_1, liwenjun_2, .*, pending)" || fail "no pending tuple in log"
echo "[smoke] offline message stored as pending"

# 15. liwenjun_2 重新登录 -> 回推离线消息（原始时间）。
s2 "/login"; sleep 0.9
grep -q "offline from liwenjun_1" "$C2_OUT" || fail "offline message not pushed on re-login"
grep -q "badminton" "$C2_OUT" || fail "pushed offline content missing"
echo "[smoke] offline message pushed on re-login"

# 16. 机器人：add 2 -> 在线名单含机器人。
s1 "/bot add 2"; sleep 0.9
grep -q "added 2 bot(s)" "$C1_OUT" || fail "bot add reply missing"
BOT=$(grep -oE "lwjbot_[0-9]+_[0-9]+" "$C1_OUT" | head -n1)
[[ -n "$BOT" ]] || fail "could not extract a bot name"
grep -Eq "online [0-9]+\].*$BOT" "$C1_OUT" || fail "online list does not include bot $BOT"
echo "[smoke] /bot add OK, bot=$BOT visible in online list"

# 17. 给机器人发消息 -> 固定回复。
s1 "/send $BOT hello there"; sleep 0.7
grep -q "幸会，liwenjun_1，很高兴认识您" "$C1_OUT" || fail "bot reply text missing"
echo "[smoke] bot reply OK"

# 18. 机器人：del 1。
s1 "/bot del 1"; sleep 0.8
grep -q "removed 1 bot(s)" "$C1_OUT" || fail "bot del reply missing"
echo "[smoke] /bot del OK"

# 19. threads.log 含派发/回收。
$SUDO cat "$THREADS_LOG" | grep -q "dispatch" || fail "threads.log lacks dispatch lines"
$SUDO cat "$THREADS_LOG" | grep -q "recycle"  || fail "threads.log lacks recycle lines"
echo "[smoke] threads.log has dispatch + recycle"

# 20. 收尾客户端 + 关服务器，验证 FIFO 与 shm 清理 + 线程池关闭。
s1 "/quit"; s2 "/quit"; sleep 0.4
exec 3>&-; exec 4>&-
C1_PID=""; C2_PID=""
$SUDO kill -TERM "$SERVER_PID"; sleep 0.8
remaining=$(ls "$FIFO_DIR"/${PREFIX}_*_fifo 2>/dev/null | wc -l | tr -d ' ')
[[ "$remaining" == "0" ]] || fail "public FIFOs not cleaned ($remaining left)"
[[ ! -e "$SHM_FILE" ]] || fail "shm object not removed after shutdown"
$SUDO cat "$SERVER_LOG" | grep -q "thread pool shut down" || fail "no clean thread-pool shutdown log"
SERVER_PID=""
echo "[smoke] FIFO + shm cleaned, thread pool shut down cleanly"

echo "STAGE 2026 PASS"
