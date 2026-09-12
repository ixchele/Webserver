#!/usr/bin/env bash
# ==============================================================================
# webserv_tester_examples_based.sh
#
# LARGE black-box tester for a C++98 webserv.
#
# IMPORTANT:
#   This version DOES NOT use a guessed grammar.
#   It is based only on the configuration forms shown in the user's real examples.
#
# Proven-by-example syntax that this tester treats as supported:
#   server { ... }
#   listen <port>;
#   host 0.0.0.0;
#   host localhost;
#   server_name name1 name2 ...;
#   client_max_body_size 1m;
#   client_max_body_size 100m;
#   error_page 404 path;
#   root path;
#   index file;
#   autoindex on|off;
#   location /path { ... }
#   location .bla { ... }              # important: no leading slash required
#   methods GET POST DELETE;
#   upload_path path;
#   cgi_pass .py /usr/bin/python3;
#   cgi_pass .bla /path/to/program;
#   return 301 /index.html;
#   return 301 https://google.com;
#   comments beginning with '#'
#
# Anything not established by those examples is NOT treated as a strict parser
# requirement. Ambiguous parser behavior is logged as OBSERVE rather than FAIL.
#
# Runtime tests cover:
#   - static GET
#   - autoindex
#   - 404/custom error page
#   - method restrictions / 405
#   - DELETE
#   - client_max_body_size / 413
#   - redirects
#   - CGI GET/POST/env/body/status/cookies/errors/large output
#   - malformed HTTP
#   - traversal attempts
#   - chunked requests
#   - CL + TE ambiguity/smuggling defense
#   - slow clients
#   - slow CGI vs normal clients
#   - keep-alive
#   - virtual hosts
#   - concurrency
#   - optional heavier stress
#
# Usage:
#   chmod +x webserv_tester_examples_based.sh
#   ./webserv_tester_examples_based.sh --bin ./webserv
#   ./webserv_tester_examples_based.sh --bin ./webserv --stress
#   ./webserv_tester_examples_based.sh --bin ./webserv --config-only
#   ./webserv_tester_examples_based.sh --bin ./webserv --runtime-only
#
# Assumed webserv invocation:
#   ./webserv config.conf
# ==============================================================================

set -u
set -o pipefail
export LC_ALL=C

WEBSERV_BIN="./webserv"
MODE="all"                  # all | config | runtime
RUN_STRESS=0
KEEP_TMP=0
VERBOSE=0
REQUEST_TIMEOUT=5
STARTUP_TIMEOUT=3.0
USE_VALGRIND=0

RUN_ID="$(date '+%Y%m%d_%H%M%S')_$$"
RESULTS_DIR="${WEBSERV_TEST_RESULTS_DIR:-./webserv_test_results_examples/$RUN_ID}"
TMP_BASE="${TMPDIR:-/tmp}/webserv_examples_tester_$RUN_ID"

PASS=0
FAIL=0
WARN=0
OBSERVE=0
TEST_NO=0
SERVER_PID=""
SERVER_DIR=""
ACTIVE_CONFIG=""
START_EPOCH="$(date +%s)"

if [ -t 1 ]; then
    RED=$'\033[31m'
    GREEN=$'\033[32m'
    YELLOW=$'\033[33m'
    CYAN=$'\033[36m'
    MAGENTA=$'\033[35m'
    BOLD=$'\033[1m'
    RESET=$'\033[0m'
else
    RED=""; GREEN=""; YELLOW=""; CYAN=""; MAGENTA=""; BOLD=""; RESET=""
fi

usage() {
    cat <<'EOF'
Usage:
  ./webserv_tester_examples_based.sh [options]

Options:
  --bin PATH             Path to webserv executable. Default: ./webserv
  --config-only          Only configuration/parser compatibility tests
  --runtime-only         Only HTTP/runtime tests
  --stress               Add heavier concurrency/resilience tests
  --keep-tmp             Keep generated configs/site files
  --request-timeout N    curl/socket timeout in seconds. Default: 5
  --startup-timeout N    startup wait in seconds. Default: 3
  --valgrind             Run runtime server under valgrind
  -v, --verbose          Print debug details
  -h, --help             Show this help

Result meanings:
  PASS     A behavior established by your examples or a strong HTTP/core invariant passed.
  FAIL     A strong expected behavior failed.
  WARN     Suspicious/likely-bug behavior, but not proven by the examples alone.
  OBSERVE  Intentionally unspecified behavior; tester records what your parser/server does.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --bin)
            WEBSERV_BIN="${2:-}"
            shift 2
            ;;
        --config-only)
            MODE="config"
            shift
            ;;
        --runtime-only)
            MODE="runtime"
            shift
            ;;
        --stress)
            RUN_STRESS=1
            shift
            ;;
        --keep-tmp)
            KEEP_TMP=1
            shift
            ;;
        --request-timeout)
            REQUEST_TIMEOUT="${2:-5}"
            shift 2
            ;;
        --startup-timeout)
            STARTUP_TIMEOUT="${2:-3}"
            shift 2
            ;;
        --valgrind)
            USE_VALGRIND=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

mkdir -p "$RESULTS_DIR" "$TMP_BASE"
SUMMARY_LOG="$RESULTS_DIR/summary.log"
FAILURES_LOG="$RESULTS_DIR/failures.log"
WARNINGS_LOG="$RESULTS_DIR/warnings.log"
OBSERVE_LOG="$RESULTS_DIR/observations.log"
ENV_LOG="$RESULTS_DIR/environment.log"
: >"$SUMMARY_LOG"
: >"$FAILURES_LOG"
: >"$WARNINGS_LOG"
: >"$OBSERVE_LOG"

ts() { date '+%Y-%m-%d %H:%M:%S'; }

log() {
    printf '[%s] %s\n' "$(ts)" "$*" | tee -a "$SUMMARY_LOG"
}

debug() {
    [ "$VERBOSE" -eq 1 ] || return 0
    printf '[%s] DEBUG %s\n' "$(ts)" "$*" | tee -a "$SUMMARY_LOG"
}

sanitize() {
    printf '%s' "$1" | tr ' /:()[]{}' '_' | tr -cd 'A-Za-z0-9_.-'
}

new_test() {
    TEST_NO=$((TEST_NO + 1))
    CURRENT_TEST_DIR="$RESULTS_DIR/$(printf '%04d' "$TEST_NO")_$(sanitize "$1")"
    mkdir -p "$CURRENT_TEST_DIR"
    printf '%s\n' "$1" >"$CURRENT_TEST_DIR/name.txt"
}

result() {
    local kind="$1"
    local name="$2"
    local reason="$3"
    case "$kind" in
        PASS)
            PASS=$((PASS + 1))
            printf '%s[PASS]%s %s -- %s\n' "$GREEN" "$RESET" "$name" "$reason" | tee -a "$SUMMARY_LOG"
            ;;
        FAIL)
            FAIL=$((FAIL + 1))
            printf '%s[FAIL]%s %s -- %s\n' "$RED" "$RESET" "$name" "$reason" | tee -a "$SUMMARY_LOG"
            printf '[FAIL] %s -- %s -- %s\n' "$name" "$reason" "$CURRENT_TEST_DIR" >>"$FAILURES_LOG"
            ;;
        WARN)
            WARN=$((WARN + 1))
            printf '%s[WARN]%s %s -- %s\n' "$YELLOW" "$RESET" "$name" "$reason" | tee -a "$SUMMARY_LOG"
            printf '[WARN] %s -- %s -- %s\n' "$name" "$reason" "$CURRENT_TEST_DIR" >>"$WARNINGS_LOG"
            ;;
        OBSERVE)
            OBSERVE=$((OBSERVE + 1))
            printf '%s[OBSERVE]%s %s -- %s\n' "$MAGENTA" "$RESET" "$name" "$reason" | tee -a "$SUMMARY_LOG"
            printf '[OBSERVE] %s -- %s -- %s\n' "$name" "$reason" "$CURRENT_TEST_DIR" >>"$OBSERVE_LOG"
            ;;
    esac
}

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Missing command: $1" >&2
        exit 2
    }
}

need bash
need curl
need python3

if [ ! -x "$WEBSERV_BIN" ]; then
    echo "webserv binary is not executable: $WEBSERV_BIN" >&2
    exit 2
fi

WEBSERV_BIN="$(python3 - "$WEBSERV_BIN" <<'PY'
import os,sys
print(os.path.abspath(sys.argv[1]))
PY
)"

free_port() {
    python3 - <<'PY'
import socket
s=socket.socket()
s.bind(("127.0.0.1",0))
print(s.getsockname()[1])
s.close()
PY
}

pid_alive() {
    [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null
}

wait_port() {
    python3 - "$1" "$2" "$3" <<'PY'
import socket,sys,time
host=sys.argv[1]
port=int(sys.argv[2])
end=time.time()+float(sys.argv[3])
while time.time()<end:
    s=socket.socket()
    s.settimeout(.12)
    try:
        s.connect((host,port))
        s.close()
        raise SystemExit(0)
    except OSError:
        try:s.close()
        except Exception:pass
        time.sleep(.04)
raise SystemExit(1)
PY
}

kill_pid() {
    local pid="${1:-}"
    [ -n "$pid" ] || return 0
    if pid_alive "$pid"; then
        kill -TERM "$pid" 2>/dev/null || true
        local n=0
        while pid_alive "$pid" && [ "$n" -lt 30 ]; do
            sleep .05
            n=$((n + 1))
        done
        if pid_alive "$pid"; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null || true
}

snapshot_process() {
    local pid="$1"
    local out="$2"
    {
        echo "timestamp=$(ts)"
        echo "pid=$pid"
        ps -o pid,ppid,stat,%cpu,%mem,rss,vsz,etime,command -p "$pid" 2>/dev/null || true
        if [ -r "/proc/$pid/status" ]; then
            echo
            cat "/proc/$pid/status" 2>/dev/null || true
        fi
        if [ -d "/proc/$pid/fd" ]; then
            echo
            printf 'fd_count='
            find "/proc/$pid/fd" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l
        fi
    } >"$out"
}

stop_server() {
    if [ -n "${SERVER_PID:-}" ]; then
        if pid_alive "$SERVER_PID"; then
            snapshot_process "$SERVER_PID" "$SERVER_DIR/process.before_stop.txt" 2>/dev/null || true
        fi
        kill_pid "$SERVER_PID"
    fi
    SERVER_PID=""
}

cleanup() {
    stop_server >/dev/null 2>&1 || true
    if [ "$KEEP_TMP" -eq 0 ]; then
        rm -rf "$TMP_BASE"
    else
        log "Temp files kept at: $TMP_BASE"
    fi
}
trap cleanup EXIT INT TERM

start_server() {
    local cfg="$1"
    local host="$2"
    local port="$3"
    local label="$4"

    stop_server >/dev/null 2>&1 || true

    SERVER_DIR="$RESULTS_DIR/_server_$(sanitize "$label")_$(date +%s%N)"
    mkdir -p "$SERVER_DIR"
    cp "$cfg" "$SERVER_DIR/config.conf"
    ACTIVE_CONFIG="$cfg"

    if [ "$USE_VALGRIND" -eq 1 ]; then
        need valgrind
        valgrind \
            --leak-check=full \
            --show-leak-kinds=all \
            --track-fds=yes \
            --error-exitcode=99 \
            "$WEBSERV_BIN" "$cfg" \
            >"$SERVER_DIR/server.stdout.log" \
            2>"$SERVER_DIR/server.stderr.log" &
    else
        "$WEBSERV_BIN" "$cfg" \
            >"$SERVER_DIR/server.stdout.log" \
            2>"$SERVER_DIR/server.stderr.log" &
    fi
    SERVER_PID=$!
    echo "$SERVER_PID" >"$SERVER_DIR/pid.txt"

    if wait_port "$host" "$port" "$STARTUP_TIMEOUT"; then
        snapshot_process "$SERVER_PID" "$SERVER_DIR/process.after_start.txt"
        return 0
    fi

    {
        echo "startup failed"
        echo "pid=$SERVER_PID"
        echo "host=$host"
        echo "port=$port"
        if pid_alive "$SERVER_PID"; then echo "alive=yes"; else echo "alive=no"; fi
    } >"$SERVER_DIR/startup_failure.txt"
    return 1
}

copy_server_logs() {
    [ -n "$SERVER_DIR" ] || return 0
    cp "$SERVER_DIR/server.stdout.log" "$CURRENT_TEST_DIR/server.stdout.snapshot.log" 2>/dev/null || true
    cp "$SERVER_DIR/server.stderr.log" "$CURRENT_TEST_DIR/server.stderr.snapshot.log" 2>/dev/null || true
}

server_alive_or_fail() {
    local name="$1"
    if ! pid_alive "$SERVER_PID"; then
        copy_server_logs
        result FAIL "$name" "server process died/crashed"
        return 1
    fi
    return 0
}

{
    echo "run_id=$RUN_ID"
    echo "date=$(date -R)"
    echo "uname=$(uname -a 2>/dev/null || true)"
    echo "bash=$BASH_VERSION"
    echo "curl=$(curl --version | head -n1)"
    echo "python=$(python3 --version 2>&1)"
    echo "binary=$WEBSERV_BIN"
    echo "mode=$MODE"
    echo "stress=$RUN_STRESS"
    echo
    file "$WEBSERV_BIN" 2>/dev/null || true
    echo
    ldd "$WEBSERV_BIN" 2>/dev/null || true
} >"$ENV_LOG"

# ==============================================================================
# FIXTURES
# ==============================================================================

FIX="$TMP_BASE/fixture"
WWW="$FIX/www"
UPLOADS="$WWW/uploads"
STORAGE="$UPLOADS/storage"
CGI="$WWW/cgi"
ERRORS="$FIX/error_pages"
YOU="$FIX/YoupiBanane"
VHOST_A="$FIX/vhost_a"
VHOST_B="$FIX/vhost_b"

mkdir -p \
    "$WWW" \
    "$WWW/static" \
    "$WWW/listing/subdir" \
    "$WWW/noindex" \
    "$UPLOADS" \
    "$STORAGE" \
    "$CGI" \
    "$ERRORS" \
    "$YOU" \
    "$VHOST_A" \
    "$VHOST_B"

printf 'ROOT_INDEX_MARKER_ae741\n' >"$WWW/index.htm"
printf 'ROOT_HTML_MARKER_b319c\n' >"$WWW/index.html"
printf 'STATIC_HELLO_MARKER_9137\n' >"$WWW/static/hello.txt"
printf 'AUTO_A_MARKER\n' >"$WWW/listing/a.txt"
printf 'AUTO_B_MARKER\n' >"$WWW/listing/b.txt"
printf 'PRIVATE_NO_INDEX_MARKER\n' >"$WWW/noindex/private.txt"
printf 'UPLOAD_FILE_FOR_DELETE\n' >"$UPLOADS/delete-me.txt"
printf 'CHUNK_TARGET_OLD\n' >"$UPLOADS/chunk.txt"
printf 'CUSTOM_404_MARKER_e404\n' >"$ERRORS/not_found_404.html"
printf 'CUSTOM_404_SECOND_MARKER_e405\n' >"$ERRORS/404.html"
printf 'YOUPI_BAD_EXTENSION_MARKER\n' >"$YOU/youpi.bad_extension"
printf 'VHOST_ALPHA_MARKER\n' >"$VHOST_A/index.html"
printf 'VHOST_BETA_MARKER\n' >"$VHOST_B/index.html"

python3 - "$WWW/static/binary.bin" <<'PY'
import sys
open(sys.argv[1],"wb").write(bytes(range(256))*16)
PY

# CGI echo
cat >"$CGI/echo.py" <<'PY'
#!/usr/bin/python3
import os,sys
try:
    n=int(os.environ.get("CONTENT_LENGTH","0") or "0")
except Exception:
    n=0
body=sys.stdin.buffer.read(n) if n > 0 else b""
print("Content-Type: text/plain")
print("X-CGI-Echo: yes")
print()
print("CGI_ECHO_MARKER")
for k in (
    "REQUEST_METHOD",
    "QUERY_STRING",
    "CONTENT_TYPE",
    "CONTENT_LENGTH",
    "SCRIPT_NAME",
    "SCRIPT_FILENAME",
    "PATH_INFO",
    "SERVER_PROTOCOL",
    "SERVER_NAME",
    "SERVER_PORT",
    "HTTP_HOST",
    "HTTP_COOKIE",
    "HTTP_USER_AGENT",
    "HTTP_X_TEST_HEADER",
):
    print("%s=%s"%(k,os.environ.get(k,"")))
sys.stdout.flush()
sys.stdout.buffer.write(b"BODY="+body)
PY
chmod +x "$CGI/echo.py"

cat >"$CGI/status.py" <<'PY'
#!/usr/bin/python3
print("Status: 201 Created")
print("Content-Type: text/plain")
print("X-CGI-Status: yes")
print()
print("CGI_STATUS_201_MARKER")
PY
chmod +x "$CGI/status.py"

cat >"$CGI/redirect.py" <<'PY'
#!/usr/bin/python3
print("Status: 302 Found")
print("Location: /index.html")
print("Content-Type: text/plain")
print()
print("CGI_REDIRECT_MARKER")
PY
chmod +x "$CGI/redirect.py"

cat >"$CGI/cookie.py" <<'PY'
#!/usr/bin/python3
import os
old=os.environ.get("HTTP_COOKIE","")
n=0
for part in old.split(";"):
    part=part.strip()
    if part.startswith("count="):
        try:n=int(part.split("=",1)[1])
        except Exception:n=0
n+=1
print("Content-Type: text/plain")
print("Set-Cookie: count=%d; Path=/"%n)
print()
print("CGI_COOKIE_MARKER")
print("old_cookie="+old)
print("new_count=%d"%n)
PY
chmod +x "$CGI/cookie.py"

cat >"$CGI/large.py" <<'PY'
#!/usr/bin/python3
print("Content-Type: text/plain")
print()
print("CGI_LARGE_BEGIN")
print("L"*(512*1024))
print("CGI_LARGE_END")
PY
chmod +x "$CGI/large.py"

cat >"$CGI/crash.py" <<'PY'
#!/usr/bin/python3
import sys
sys.stderr.write("INTENTIONAL_CGI_FAILURE\n")
sys.exit(42)
PY
chmod +x "$CGI/crash.py"

cat >"$CGI/malformed.py" <<'PY'
#!/usr/bin/python3
print("NOT A CGI HEADER")
print("CGI_MALFORMED_MARKER")
PY
chmod +x "$CGI/malformed.py"

cat >"$CGI/slow.py" <<'PY'
#!/usr/bin/python3
import time
time.sleep(2)
print("Content-Type: text/plain")
print()
print("CGI_SLOW_MARKER")
PY
chmod +x "$CGI/slow.py"

# .bla executable: made as a shell script because the example only proves that
# cgi_pass can map an arbitrary extension to an arbitrary executable.
cat >"$FIX/bla_cgi_runner.sh" <<'SH'
#!/usr/bin/env bash
printf 'Content-Type: text/plain\r\n\r\n'
printf 'BLA_CGI_RUNNER_MARKER\n'
cat
SH
chmod +x "$FIX/bla_cgi_runner.sh"
printf 'dummy bla source\n' >"$WWW/test.bla"

# ==============================================================================
# CONFIG BUILDERS
# ==============================================================================

write_runtime_config() {
    local out="$1"
    local port="$2"
    cat >"$out" <<EOF
server {
    listen $port;
    host 127.0.0.1;

    server_name exemple.com hamid.com;

    client_max_body_size 1m;

    error_page 404 $ERRORS/not_found_404.html;

    root $WWW;

    location / {
        root $WWW;
        methods GET DELETE;
        autoindex on;
    }

    location /abc {
        methods GET POST DELETE;
        root $UPLOADS;
        index index.html;
        upload_path $STORAGE;
    }

    location /test1 {
        methods GET POST DELETE;
        root $UPLOADS;
        index index.html;
        autoindex on;
        upload_path $STORAGE;
    }

    location /test2 {
        methods GET POST;
        root $UPLOADS;
        index index.html;
        upload_path $STORAGE;
    }

    location /test3 {
        methods GET POST DELETE;
        root $UPLOADS;
        index index.html;
        upload_path $STORAGE;
    }

    location /cgi {
        methods GET POST;
        root $WWW;
        cgi_pass .py /usr/bin/python3;
    }

    location /ancienne-page {
        return 301 /index.html;
    }

    location /google {
        return 301 https://google.com;
    }

    location /hamid {
        return 301 https://youtube.com;
    }
}
EOF
}

write_example1_compatible() {
    local out="$1"
    local port="$2"
    cat >"$out" <<EOF
server {
    listen $port;
    host 0.0.0.0;
    root $WWW;
    index index.htm;
    autoindex on;

    location / {
        methods GET DELETE;
        autoindex on;
    }

    location /upload {
        methods GET POST DELETE;
        root $UPLOADS;
        autoindex on;
        upload_path $STORAGE;
    }

    location /cgi {
        methods GET POST;
        cgi_pass .py /usr/bin/python3;
    }
}
EOF
}

write_example2_compatible() {
    local out="$1"
    local port="$2"
    cat >"$out" <<EOF
server {
    listen $port;
    # host localhost;
    # host 127.0.1.1;
    host 0.0.0.0;

    server_name exemple.com hamid.com;

    # lower-case suffix is shown by the real example
    client_max_body_size 1m;

    error_page 404 $ERRORS/not_found_404.html;

    root $WWW ;

    location / {
        root $WWW;
        methods GET DELETE;
        autoindex on;
    }

    location /abc {
        methods GET POST DELETE;
        root $UPLOADS;
        index index.html;
        upload_path $STORAGE;
    }

    location /test2 {
        methods GET POST;
        root $UPLOADS;
        index index.html;
        upload_path $STORAGE;
    }

    location /test3 {
        methods GET POST DELETE;
        root $UPLOADS;
        index index.html;
        upload_path $STORAGE;
    }

    location /cgi {
        methods GET POST;
        cgi_pass .py /usr/bin/python3;
    }

    location /ancienne-page {
        return 301 /index.html;
    }

    location /google {
        return 301 https://google.com;
    }
}
EOF
}

write_example3_compatible() {
    local out="$1"
    local port="$2"
    cat >"$out" <<EOF
server {
    listen $port;
    host localhost;
    error_page 404 $ERRORS/404.html;

    client_max_body_size 100m;

    root $FIX;
    location / {
        methods GET;
        root $FIX;
        autoindex on;
    }

    location .bla {
        methods POST;
        cgi_pass .bla $FIX/bla_cgi_runner.sh;
    }

    location /post_body {
        methods POST;
        client_max_body_size 100m;
    }

    location /directory {
        methods GET POST;
        root $YOU;
        index youpi.bad_extension;
    }
}
EOF
}

write_vhost_config() {
    local out="$1"
    local port="$2"
    cat >"$out" <<EOF
server {
    listen $port;
    host 127.0.0.1;
    server_name alpha.local;
    root $VHOST_A;
    index index.html;

    location / {
        methods GET;
    }
}

server {
    listen $port;
    host 127.0.0.1;
    server_name beta.local;
    root $VHOST_B;
    index index.html;

    location / {
        methods GET;
    }
}
EOF
}

# ==============================================================================
# CONFIG TEST HELPERS
# ==============================================================================

config_expect_start() {
    local name="$1"
    local cfg="$2"
    local host="$3"
    local port="$4"

    new_test "$name"
    cp "$cfg" "$CURRENT_TEST_DIR/config.conf"

    "$WEBSERV_BIN" "$cfg" \
        >"$CURRENT_TEST_DIR/server.stdout.log" \
        2>"$CURRENT_TEST_DIR/server.stderr.log" &
    local pid=$!
    echo "$pid" >"$CURRENT_TEST_DIR/pid.txt"

    if wait_port "$host" "$port" "$STARTUP_TIMEOUT"; then
        result PASS "$name" "example-supported config started and listened on $host:$port"
        kill_pid "$pid"
    else
        if pid_alive "$pid"; then
            result FAIL "$name" "config was not rejected, but expected socket $host:$port never became reachable"
        else
            wait "$pid" 2>/dev/null || true
            result FAIL "$name" "example-supported configuration was rejected"
        fi
        kill_pid "$pid"
    fi
}

config_malformed_should_not_run() {
    # Only for unmistakable syntax corruption such as missing brace/semicolon.
    local name="$1"
    local cfg="$2"
    local port="$3"

    new_test "$name"
    cp "$cfg" "$CURRENT_TEST_DIR/config.conf"

    "$WEBSERV_BIN" "$cfg" \
        >"$CURRENT_TEST_DIR/server.stdout.log" \
        2>"$CURRENT_TEST_DIR/server.stderr.log" &
    local pid=$!
    echo "$pid" >"$CURRENT_TEST_DIR/pid.txt"
    sleep .35

    if wait_port 127.0.0.1 "$port" .20; then
        result WARN "$name" "obviously malformed syntax still produced a listening server"
        kill_pid "$pid"
    elif pid_alive "$pid"; then
        result WARN "$name" "malformed config left process alive without expected listener"
        kill_pid "$pid"
    else
        wait "$pid" 2>/dev/null
        local rc=$?
        result PASS "$name" "malformed syntax rejected (exit=$rc)"
    fi
}

config_observe() {
    # Use for semantics not proven by examples.
    local name="$1"
    local cfg="$2"
    local port="$3"

    new_test "$name"
    cp "$cfg" "$CURRENT_TEST_DIR/config.conf"

    "$WEBSERV_BIN" "$cfg" \
        >"$CURRENT_TEST_DIR/server.stdout.log" \
        2>"$CURRENT_TEST_DIR/server.stderr.log" &
    local pid=$!
    echo "$pid" >"$CURRENT_TEST_DIR/pid.txt"

    sleep .35

    if wait_port 127.0.0.1 "$port" .20; then
        result OBSERVE "$name" "implementation accepted config and listened"
        kill_pid "$pid"
    elif pid_alive "$pid"; then
        result OBSERVE "$name" "process stayed alive but expected listener was not reachable"
        kill_pid "$pid"
    else
        wait "$pid" 2>/dev/null
        local rc=$?
        result OBSERVE "$name" "implementation rejected config (exit=$rc)"
    fi
}

run_config_suite() {
    log "${BOLD}=== EXAMPLE-BASED CONFIG COMPATIBILITY ===${RESET}"

    local D="$TMP_BASE/configs"
    mkdir -p "$D"
    local p cfg

    p="$(free_port)"
    cfg="$D/example1.conf"
    write_example1_compatible "$cfg" "$p"
    config_expect_start "config_example1_shape" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/example2.conf"
    write_example2_compatible "$cfg" "$p"
    config_expect_start "config_example2_shape_comments_server_name_lowercase_m_redirects" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/example3.conf"
    write_example3_compatible "$cfg" "$p"
    config_expect_start "config_example3_shape_host_localhost_100m_dot_bla_location" "$cfg" 127.0.0.1 "$p"

    # Explicit individual compatibility checks.
    p="$(free_port)"
    cfg="$D/host_localhost.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host localhost;
    root $WWW;
    location / {
        methods GET;
    }
}
EOF
    config_expect_start "config_host_localhost_supported" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/comments.conf"
    cat >"$cfg" <<EOF
# comment before server
server {
    listen $p;
    # inline full-line comment
    host 127.0.0.1;
    root $WWW;
    # another comment
    location / {
        methods GET;
    }
}
EOF
    config_expect_start "config_hash_comments_supported" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/lowercase_m.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    client_max_body_size 1m;
    root $WWW;
    location / {
        methods GET;
    }
}
EOF
    config_expect_start "config_client_max_body_size_lowercase_m" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/large_100m.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    client_max_body_size 100m;
    root $WWW;
    location / {
        methods GET;
    }
}
EOF
    config_expect_start "config_client_max_body_size_100m" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/server_names.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    server_name exemple.com hamid.com;
    root $WWW;
    location / {
        methods GET;
    }
}
EOF
    config_expect_start "config_multiple_server_names" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/location_dot_bla.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    location .bla {
        methods POST;
        cgi_pass .bla $FIX/bla_cgi_runner.sh;
    }
}
EOF
    config_expect_start "config_location_token_dot_bla_no_leading_slash" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/index_arbitrary_ext.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $YOU;
    location / {
        methods GET;
        root $YOU;
        index youpi.bad_extension;
    }
}
EOF
    config_expect_start "config_index_arbitrary_extension" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/return_absolute_url.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    location /google {
        return 301 https://google.com;
    }
}
EOF
    config_expect_start "config_return_absolute_url" "$cfg" 127.0.0.1 "$p"

    p="$(free_port)"
    cfg="$D/location_body_limit.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    client_max_body_size 1m;
    root $WWW;
    location /post_body {
        methods POST;
        client_max_body_size 100m;
    }
}
EOF
    config_expect_start "config_client_body_size_inside_location" "$cfg" 127.0.0.1 "$p"

    # --------------------------------------------------------------------------
    # Obvious syntax corruption: these are not "grammar assumptions"; they are
    # basic parser robustness checks.
    # --------------------------------------------------------------------------

    p="$(free_port)"
    cfg="$D/missing_semicolon.conf"
    cat >"$cfg" <<EOF
server {
    listen $p
    host 127.0.0.1;
    root $WWW;
}
EOF
    config_malformed_should_not_run "config_malformed_missing_semicolon" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/missing_close_brace.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
EOF
    config_malformed_should_not_run "config_malformed_missing_closing_brace" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/extra_close_brace.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
}}
EOF
    config_malformed_should_not_run "config_malformed_extra_closing_brace" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/broken_location.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    location / {
        methods GET
    }
}
EOF
    config_malformed_should_not_run "config_malformed_location_directive_without_semicolon" "$cfg" "$p"

    # --------------------------------------------------------------------------
    # Unknown semantics: observe, do NOT invent requirements.
    # --------------------------------------------------------------------------

    p="$(free_port)"
    cfg="$D/no_root.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
}
EOF
    config_observe "config_observe_missing_root" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/no_listen.conf"
    cat >"$cfg" <<EOF
server {
    host 127.0.0.1;
    root $WWW;
}
EOF
    config_observe "config_observe_missing_listen" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/unknown_directive.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    completely_unknown abc;
}
EOF
    config_observe "config_observe_unknown_directive" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/duplicate_root.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    root $WWW;
}
EOF
    config_observe "config_observe_duplicate_root" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/duplicate_location.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    location /x {
        methods GET;
    }
    location /x {
        methods GET;
    }
}
EOF
    config_observe "config_observe_duplicate_location" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/methods_server_scope.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    methods GET;
}
EOF
    config_observe "config_observe_methods_at_server_scope" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/listen_location.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    location /x {
        listen 9099;
        methods GET;
    }
}
EOF
    config_observe "config_observe_listen_inside_location" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/nested_location.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    location /x {
        methods GET;
        location /y {
            methods GET;
        }
    }
}
EOF
    config_observe "config_observe_nested_location" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/quoted_root.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root "$WWW";
}
EOF
    config_observe "config_observe_quoted_root" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/invalid_size.conf"
    cat >"$cfg" <<EOF
server {
    listen $p;
    host 127.0.0.1;
    root $WWW;
    client_max_body_size potato;
}
EOF
    config_observe "config_observe_invalid_body_size_token" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/invalid_port.conf"
    cat >"$cfg" <<EOF
server {
    listen 99999;
    host 127.0.0.1;
    root $WWW;
}
EOF
    config_observe "config_observe_port_out_of_range" "$cfg" "$p"

    p="$(free_port)"
    cfg="$D/empty_server.conf"
    cat >"$cfg" <<EOF
server {
}
EOF
    config_observe "config_observe_empty_server" "$cfg" "$p"
}

# ==============================================================================
# HTTP HELPERS
# ==============================================================================

curl_test() {
    local name="$1"
    local method="$2"
    local url="$3"
    local expected_csv="$4"
    shift 4

    new_test "$name"
    {
        echo "method=$method"
        echo "url=$url"
        echo "expected=$expected_csv"
        printf 'extra='
        printf '%q ' "$@"
        echo
    } >"$CURRENT_TEST_DIR/request.meta"

    local before after rc status
    before="$(date +%s%N)"

    curl \
        -sS \
        --http1.1 \
        --max-time "$REQUEST_TIMEOUT" \
        -X "$method" \
        -D "$CURRENT_TEST_DIR/response.headers" \
        -o "$CURRENT_TEST_DIR/response.body" \
        -w '%{http_code}' \
        "$@" \
        "$url" \
        >"$CURRENT_TEST_DIR/status.txt" \
        2>"$CURRENT_TEST_DIR/curl.stderr"
    rc=$?

    after="$(date +%s%N)"
    echo $(( (after-before)/1000000 )) >"$CURRENT_TEST_DIR/elapsed_ms.txt"
    echo "$rc" >"$CURRENT_TEST_DIR/curl_exit.txt"
    status="$(cat "$CURRENT_TEST_DIR/status.txt" 2>/dev/null || true)"

    copy_server_logs
    if ! server_alive_or_fail "$name"; then
        return 1
    fi

    if [ "$rc" -ne 0 ]; then
        result FAIL "$name" "curl failed rc=$rc status=${status:-none}"
        return 1
    fi

    local good=0 x
    IFS=',' read -r -a STS <<<"$expected_csv"
    for x in "${STS[@]}"; do
        if [ "$status" = "$x" ]; then
            good=1
            break
        fi
    done

    if [ "$good" -eq 1 ]; then
        result PASS "$name" "HTTP $status"
    else
        result FAIL "$name" "expected {$expected_csv}, got ${status:-none}"
        return 1
    fi
}

curl_observe() {
    local name="$1"
    local method="$2"
    local url="$3"
    shift 3

    new_test "$name"
    curl \
        -sS \
        --http1.1 \
        --max-time "$REQUEST_TIMEOUT" \
        -X "$method" \
        -D "$CURRENT_TEST_DIR/response.headers" \
        -o "$CURRENT_TEST_DIR/response.body" \
        -w '%{http_code}' \
        "$@" \
        "$url" \
        >"$CURRENT_TEST_DIR/status.txt" \
        2>"$CURRENT_TEST_DIR/curl.stderr"
    local rc=$?
    echo "$rc" >"$CURRENT_TEST_DIR/curl_exit.txt"
    copy_server_logs

    if ! server_alive_or_fail "$name"; then
        return 1
    fi

    local status
    status="$(cat "$CURRENT_TEST_DIR/status.txt" 2>/dev/null || true)"
    result OBSERVE "$name" "curl_rc=$rc HTTP=${status:-none}"
}

assert_contains() {
    local name="$1"
    local file="$2"
    local text="$3"
    local severity="${4:-FAIL}"

    new_test "$name"
    cp "$file" "$CURRENT_TEST_DIR/checked.bin" 2>/dev/null || true
    echo "$text" >"$CURRENT_TEST_DIR/expected.txt"

    if grep -Fq -- "$text" "$file" 2>/dev/null; then
        result PASS "$name" "found '$text'"
    else
        if [ "$severity" = "WARN" ]; then
            result WARN "$name" "missing '$text'"
        else
            result FAIL "$name" "missing '$text'"
        fi
    fi
}

assert_not_contains() {
    local name="$1"
    local file="$2"
    local text="$3"

    new_test "$name"
    cp "$file" "$CURRENT_TEST_DIR/checked.bin" 2>/dev/null || true
    if grep -Fq -- "$text" "$file" 2>/dev/null; then
        result FAIL "$name" "forbidden text leaked: '$text'"
    else
        result PASS "$name" "forbidden text absent"
    fi
}

assert_header() {
    local name="$1"
    local file="$2"
    local regex="$3"
    local severity="${4:-FAIL}"

    new_test "$name"
    cp "$file" "$CURRENT_TEST_DIR/headers.txt" 2>/dev/null || true
    echo "$regex" >"$CURRENT_TEST_DIR/expected.regex"

    if grep -Eiq -- "$regex" "$file"; then
        result PASS "$name" "header matched /$regex/i"
    else
        if [ "$severity" = "WARN" ]; then
            result WARN "$name" "header missing /$regex/i"
        else
            result FAIL "$name" "header missing /$regex/i"
        fi
    fi
}

raw_request() {
    local name="$1"
    local host="$2"
    local port="$3"
    local expected_csv="$4"
    local req="$5"

    new_test "$name"
    cp "$req" "$CURRENT_TEST_DIR/request.raw"

    python3 - "$host" "$port" "$req" "$CURRENT_TEST_DIR/response.raw" "$REQUEST_TIMEOUT" <<'PY'
import socket,sys
host=sys.argv[1]
port=int(sys.argv[2])
data=open(sys.argv[3],"rb").read()
out=sys.argv[4]
timeout=float(sys.argv[5])
s=socket.socket()
s.settimeout(timeout)
chunks=[]
try:
    s.connect((host,port))
    s.sendall(data)
    while True:
        try:b=s.recv(65536)
        except socket.timeout:break
        if not b:break
        chunks.append(b)
except Exception as e:
    open(out+".error","w").write(repr(e))
finally:
    try:s.close()
    except Exception:pass
open(out,"wb").write(b"".join(chunks))
PY

    local status
    status="$(python3 - "$CURRENT_TEST_DIR/response.raw" <<'PY'
import re,sys
try:d=open(sys.argv[1],"rb").read(4096)
except Exception:d=b""
m=re.match(br"HTTP/\d(?:\.\d)?\s+(\d{3})",d)
print(m.group(1).decode() if m else "")
PY
)"
    echo "$status" >"$CURRENT_TEST_DIR/status.txt"

    copy_server_logs
    if ! server_alive_or_fail "$name"; then
        return 1
    fi

    local good=0 x
    IFS=',' read -r -a STS <<<"$expected_csv"
    for x in "${STS[@]}"; do
        if [ "$status" = "$x" ]; then
            good=1
            break
        fi
    done

    if [ "$good" -eq 1 ]; then
        result PASS "$name" "raw HTTP status $status"
    else
        result FAIL "$name" "expected {$expected_csv}, got ${status:-none}"
    fi
}

raw_observe() {
    local name="$1"
    local host="$2"
    local port="$3"
    local req="$4"

    new_test "$name"
    cp "$req" "$CURRENT_TEST_DIR/request.raw"

    python3 - "$host" "$port" "$req" "$CURRENT_TEST_DIR/response.raw" "$REQUEST_TIMEOUT" <<'PY'
import socket,sys
host=sys.argv[1]; port=int(sys.argv[2])
data=open(sys.argv[3],"rb").read()
out=sys.argv[4]; timeout=float(sys.argv[5])
s=socket.socket(); s.settimeout(timeout); chunks=[]
try:
    s.connect((host,port)); s.sendall(data)
    while True:
        try:b=s.recv(65536)
        except socket.timeout:break
        if not b:break
        chunks.append(b)
except Exception as e:
    open(out+".error","w").write(repr(e))
finally:
    try:s.close()
    except Exception:pass
open(out,"wb").write(b"".join(chunks))
PY

    local status
    status="$(python3 - "$CURRENT_TEST_DIR/response.raw" <<'PY'
import re,sys
try:d=open(sys.argv[1],"rb").read(4096)
except Exception:d=b""
m=re.match(br"HTTP/\d(?:\.\d)?\s+(\d{3})",d)
print(m.group(1).decode() if m else "")
PY
)"
    copy_server_logs
    if ! server_alive_or_fail "$name"; then
        return 1
    fi
    result OBSERVE "$name" "raw status=${status:-none}"
}

make_req() {
    local out="$1"
    shift
    printf '%b' "$*" >"$out"
}

# ==============================================================================
# RUNTIME SUITE
# ==============================================================================

run_runtime_suite() {
    log "${BOLD}=== HTTP / RUNTIME TORTURE SUITE ===${RESET}"

    local port cfg base tdir
    port="$(free_port)"
    cfg="$TMP_BASE/runtime.conf"
    write_runtime_config "$cfg" "$port"

    if ! start_server "$cfg" 127.0.0.1 "$port" "runtime"; then
        new_test "runtime_start"
        cp "$SERVER_DIR/server.stdout.log" "$CURRENT_TEST_DIR/" 2>/dev/null || true
        cp "$SERVER_DIR/server.stderr.log" "$CURRENT_TEST_DIR/" 2>/dev/null || true
        result FAIL "runtime_start" "generated example-compatible runtime config did not start"
        return 1
    fi

    base="http://127.0.0.1:$port"

    # --------------------------------------------------------------------------
    # Static, index, autoindex, 404
    # --------------------------------------------------------------------------

    curl_test "get_root" GET "$base/" "200"
    tdir="$CURRENT_TEST_DIR"
    # root/index inheritance varies; either index.htm or autoindex is acceptable.
    new_test "get_root_content_observation"
    cp "$tdir/response.body" "$CURRENT_TEST_DIR/body"
    if grep -Fq 'ROOT_INDEX_MARKER_ae741' "$tdir/response.body"; then
        result PASS "get_root_content_observation" "served inherited index.htm"
    elif grep -Eq 'index\.htm|static|uploads|cgi' "$tdir/response.body"; then
        result PASS "get_root_content_observation" "served autoindex/root listing"
    else
        result WARN "get_root_content_observation" "HTTP 200 but body did not look like expected index or listing"
    fi

    curl_test "get_static_text" GET "$base/static/hello.txt" "200"
    tdir="$CURRENT_TEST_DIR"
    assert_contains "get_static_text_marker" "$tdir/response.body" "STATIC_HELLO_MARKER_9137"

    curl_test "get_static_binary" GET "$base/static/binary.bin" "200"
    tdir="$CURRENT_TEST_DIR"
    new_test "get_static_binary_size"
    bytes="$(wc -c <"$tdir/response.body" | tr -d ' ')"
    if [ "$bytes" = "4096" ]; then
        result PASS "get_static_binary_size" "exact 4096-byte binary response"
    else
        result FAIL "get_static_binary_size" "expected 4096 bytes, got $bytes"
    fi

    curl_test "missing_file_404" GET "$base/definitely-not-here-84731" "404"
    tdir="$CURRENT_TEST_DIR"
    assert_contains "custom_404_page" "$tdir/response.body" "CUSTOM_404_MARKER_e404" WARN

    curl_test "autoindex_root_or_directory" GET "$base/listing/" "200"
    tdir="$CURRENT_TEST_DIR"
    assert_contains "autoindex_lists_a_txt" "$tdir/response.body" "a.txt" WARN
    assert_contains "autoindex_lists_b_txt" "$tdir/response.body" "b.txt" WARN

    # --------------------------------------------------------------------------
    # Method restrictions explicitly demonstrated in example comments.
    # --------------------------------------------------------------------------

    curl_test "root_post_rejected_405" POST "$base/" "405" --data 'hello=world'

    curl_test "test2_delete_rejected_405" DELETE "$base/test2/delete-me.txt" "405"

    cp "$UPLOADS/delete-me.txt" "$UPLOADS/delete-runtime.txt"
    curl_test "test3_delete_allowed" DELETE "$base/test3/delete-runtime.txt" "204,200"
    new_test "test3_delete_removed_file"
    if [ ! -e "$UPLOADS/delete-runtime.txt" ]; then
        result PASS "test3_delete_removed_file" "DELETE removed underlying file"
    else
        result WARN "test3_delete_removed_file" "DELETE returned success but file remained on disk"
    fi

    # GET to a POST-only/unsupported route behavior can vary based on location
    # resolution; observe rather than invent.
    curl_observe "test2_get_observation" GET "$base/test2/"

    # --------------------------------------------------------------------------
    # Redirects from real example
    # --------------------------------------------------------------------------

    curl_test "redirect_internal_301" GET "$base/ancienne-page" "301"
    tdir="$CURRENT_TEST_DIR"
    assert_header "redirect_internal_location" "$tdir/response.headers" '^Location:[[:space:]]*/index\.html' FAIL

    curl_test "redirect_google_301" GET "$base/google" "301"
    tdir="$CURRENT_TEST_DIR"
    assert_header "redirect_google_location" "$tdir/response.headers" '^Location:[[:space:]]*https://google\.com/?[[:space:]]*$' FAIL

    curl_test "redirect_youtube_301" GET "$base/hamid" "301"
    tdir="$CURRENT_TEST_DIR"
    assert_header "redirect_youtube_location" "$tdir/response.headers" '^Location:[[:space:]]*https://youtube\.com/?[[:space:]]*$' FAIL

    # --------------------------------------------------------------------------
    # client_max_body_size: example explicitly says 2MB should return 413 when
    # server limit is 1m.
    # --------------------------------------------------------------------------

    python3 - "$TMP_BASE/body_512k.bin" "$TMP_BASE/body_2m.bin" <<'PY'
import sys
open(sys.argv[1],"wb").write(b"A"*(512*1024))
open(sys.argv[2],"wb").write(b"B"*(2*1024*1024))
PY

    curl_test "body_under_1m_on_post_route" POST "$base/test1/upload.bin" "200,201,204" \
        -H 'Content-Type: application/octet-stream' \
        --data-binary "@$TMP_BASE/body_512k.bin"

    curl_test "body_2m_rejected_413" POST "$base/test1/too-big.bin" "413" \
        -H 'Content-Type: application/octet-stream' \
        --data-binary "@$TMP_BASE/body_2m.bin"

    # Snapshot storage to make upload bugs debuggable without assuming your
    # exact filename policy.
    new_test "upload_storage_snapshot"
    {
        echo "UPLOADS:"
        find "$UPLOADS" -maxdepth 3 -type f -printf '%p | %s bytes\n' 2>/dev/null || true
        echo
        echo "STORAGE:"
        find "$STORAGE" -maxdepth 3 -type f -printf '%p | %s bytes\n' 2>/dev/null || true
    } >"$CURRENT_TEST_DIR/files.txt"
    result OBSERVE "upload_storage_snapshot" "saved post-upload filesystem snapshot"

    # --------------------------------------------------------------------------
    # CGI
    # --------------------------------------------------------------------------

    curl_test "cgi_get_echo" GET "$base/cgi/echo.py?alpha=1&beta=two" "200" \
        -H 'User-Agent: WebservTester/2' \
        -H 'X-Test-Header: hello-cgi'
    tdir="$CURRENT_TEST_DIR"
    assert_contains "cgi_get_marker" "$tdir/response.body" "CGI_ECHO_MARKER"
    assert_contains "cgi_get_method_env" "$tdir/response.body" "REQUEST_METHOD=GET"
    assert_contains "cgi_query_string_env" "$tdir/response.body" "QUERY_STRING=alpha=1&beta=two"
    assert_contains "cgi_custom_header_env" "$tdir/response.body" "HTTP_X_TEST_HEADER=hello-cgi" WARN

    curl_test "cgi_post_echo" POST "$base/cgi/echo.py?mode=post" "200" \
        -H 'Content-Type: application/x-www-form-urlencoded' \
        --data 'name=adam&value=42'
    tdir="$CURRENT_TEST_DIR"
    assert_contains "cgi_post_method_env" "$tdir/response.body" "REQUEST_METHOD=POST"
    assert_contains "cgi_post_content_type_env" "$tdir/response.body" "CONTENT_TYPE=application/x-www-form-urlencoded"
    assert_contains "cgi_post_body" "$tdir/response.body" "BODY=name=adam&value=42"

    curl_test "cgi_status_header_201" GET "$base/cgi/status.py" "201"
    tdir="$CURRENT_TEST_DIR"
    assert_contains "cgi_status_body" "$tdir/response.body" "CGI_STATUS_201_MARKER"

    curl_test "cgi_redirect" GET "$base/cgi/redirect.py" "302"
    tdir="$CURRENT_TEST_DIR"
    assert_header "cgi_redirect_location" "$tdir/response.headers" '^Location:[[:space:]]*/index\.html' FAIL

    curl_test "cgi_cookie_first" GET "$base/cgi/cookie.py" "200"
    tdir="$CURRENT_TEST_DIR"
    assert_header "cgi_cookie_set_cookie_passthrough" "$tdir/response.headers" '^Set-Cookie:[[:space:]]*count=1' FAIL

    curl_test "cgi_cookie_second" GET "$base/cgi/cookie.py" "200" -H 'Cookie: count=7'
    tdir="$CURRENT_TEST_DIR"
    assert_contains "cgi_cookie_received" "$tdir/response.body" "old_cookie=count=7"
    assert_header "cgi_cookie_increment" "$tdir/response.headers" '^Set-Cookie:[[:space:]]*count=8' FAIL

    curl_test "cgi_large_output" GET "$base/cgi/large.py" "200"
    tdir="$CURRENT_TEST_DIR"
    assert_contains "cgi_large_output_complete" "$tdir/response.body" "CGI_LARGE_END"

    # Exact gateway error code is implementation-specific. 500 is common;
    # 502 is also reasonable. But returning 200 for a failed CGI is suspicious.
    curl_test "cgi_child_nonzero_exit" GET "$base/cgi/crash.py" "500,502"
    curl_test "cgi_malformed_output" GET "$base/cgi/malformed.py" "500,502"

    # Slow CGI must not freeze unrelated clients in an event-driven server.
    new_test "cgi_slow_does_not_block_normal_client"
    (
        curl -sS --max-time 5 \
            -D "$CURRENT_TEST_DIR/slow.headers" \
            -o "$CURRENT_TEST_DIR/slow.body" \
            -w '%{http_code}' \
            "$base/cgi/slow.py" \
            >"$CURRENT_TEST_DIR/slow.status" \
            2>"$CURRENT_TEST_DIR/slow.stderr"
        echo $? >"$CURRENT_TEST_DIR/slow.rc"
    ) &
    slowpid=$!
    sleep .2
    begin="$(date +%s%N)"
    quick="$(curl -sS --max-time 1.5 -o "$CURRENT_TEST_DIR/quick.body" -w '%{http_code}' "$base/static/hello.txt" 2>"$CURRENT_TEST_DIR/quick.stderr" || true)"
    end="$(date +%s%N)"
    ms=$(( (end-begin)/1000000 ))
    wait "$slowpid" 2>/dev/null || true
    copy_server_logs
    if ! server_alive_or_fail "cgi_slow_does_not_block_normal_client"; then
        :
    elif [ "$quick" != "200" ]; then
        result FAIL "cgi_slow_does_not_block_normal_client" "normal client failed while CGI slept; status=$quick"
    elif [ "$ms" -ge 1700 ]; then
        result FAIL "cgi_slow_does_not_block_normal_client" "normal request blocked for ${ms}ms behind slow CGI"
    else
        result PASS "cgi_slow_does_not_block_normal_client" "normal request returned 200 in ${ms}ms"
    fi

    # --------------------------------------------------------------------------
    # URI / traversal security
    # --------------------------------------------------------------------------

    curl_test "query_string_not_part_of_static_filename" GET "$base/static/hello.txt?cache=123" "200"
    tdir="$CURRENT_TEST_DIR"
    assert_contains "query_static_content_marker" "$tdir/response.body" "STATIC_HELLO_MARKER_9137"

    curl_test "path_traversal_plain" GET "$base/../../../../etc/passwd" "400,403,404" --path-as-is
    tdir="$CURRENT_TEST_DIR"
    assert_not_contains "path_traversal_plain_no_passwd" "$tdir/response.body" "root:x:"

    curl_test "path_traversal_encoded" GET "$base/%2e%2e/%2e%2e/etc/passwd" "400,403,404" --path-as-is
    tdir="$CURRENT_TEST_DIR"
    assert_not_contains "path_traversal_encoded_no_passwd" "$tdir/response.body" "root:x:"

    curl_test "encoded_nul_in_uri" GET "$base/static/hello.txt%00.html" "400,404"

    # --------------------------------------------------------------------------
    # Raw HTTP parser torture
    # --------------------------------------------------------------------------

    RAW="$TMP_BASE/raw"
    mkdir -p "$RAW"

    make_req "$RAW/http10.req" \
        "GET /static/hello.txt HTTP/1.0\r\nConnection: close\r\n\r\n"
    raw_observe "raw_http_1_0_observation" 127.0.0.1 "$port" "$RAW/http10.req"

    make_req "$RAW/missing_host.req" \
        "GET / HTTP/1.1\r\nConnection: close\r\n\r\n"
    raw_request "raw_http11_missing_host" 127.0.0.1 "$port" "400" "$RAW/missing_host.req"

    make_req "$RAW/duplicate_host.req" \
        "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\nConnection: close\r\n\r\n"
    raw_request "raw_duplicate_host" 127.0.0.1 "$port" "400" "$RAW/duplicate_host.req"

    make_req "$RAW/no_colon.req" \
        "GET / HTTP/1.1\r\nHost: localhost\r\nBrokenHeader\r\nConnection: close\r\n\r\n"
    raw_request "raw_header_without_colon" 127.0.0.1 "$port" "400" "$RAW/no_colon.req"

    make_req "$RAW/bad_cl.req" \
        "POST /cgi/echo.py HTTP/1.1\r\nHost: localhost\r\nContent-Length: potato\r\nConnection: close\r\n\r\nhello"
    raw_request "raw_bad_content_length" 127.0.0.1 "$port" "400" "$RAW/bad_cl.req"

    make_req "$RAW/negative_cl.req" \
        "POST /cgi/echo.py HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\nConnection: close\r\n\r\n"
    raw_request "raw_negative_content_length" 127.0.0.1 "$port" "400" "$RAW/negative_cl.req"

    make_req "$RAW/conflicting_cl.req" \
        "POST /cgi/echo.py HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\nContent-Length: 9\r\nConnection: close\r\n\r\nabcdefghi"
    raw_request "raw_conflicting_content_lengths" 127.0.0.1 "$port" "400" "$RAW/conflicting_cl.req"

    make_req "$RAW/bad_version.req" \
        "GET / HTTP/9.9\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    raw_request "raw_bad_http_version" 127.0.0.1 "$port" "400,505" "$RAW/bad_version.req"

    make_req "$RAW/bad_line.req" \
        "GET\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    raw_request "raw_malformed_request_line" 127.0.0.1 "$port" "400" "$RAW/bad_line.req"

    make_req "$RAW/extra_token.req" \
        "GET / HTTP/1.1 EXTRA\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    raw_request "raw_extra_request_line_token" 127.0.0.1 "$port" "400" "$RAW/extra_token.req"

    printf 'GET / HTTP/1.1\nHost: localhost\nConnection: close\n\n' >"$RAW/lf.req"
    raw_request "raw_lf_only_lines" 127.0.0.1 "$port" "400" "$RAW/lf.req"

    python3 - "$RAW/nul.req" <<'PY'
import sys
open(sys.argv[1],"wb").write(
    b"GET / HTTP/1.1\r\n"
    b"Host: localhost\r\n"
    b"X-Nul: before\x00after\r\n"
    b"Connection: close\r\n\r\n"
)
PY
    raw_request "raw_nul_in_header" 127.0.0.1 "$port" "400" "$RAW/nul.req"

    python3 - "$RAW/long_uri.req" <<'PY'
import sys
uri=b"/"+b"a"*20000
open(sys.argv[1],"wb").write(
    b"GET "+uri+b" HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
)
PY
    raw_request "raw_20k_uri" 127.0.0.1 "$port" "400,414" "$RAW/long_uri.req"

    python3 - "$RAW/huge_header.req" <<'PY'
import sys
open(sys.argv[1],"wb").write(
    b"GET / HTTP/1.1\r\n"
    b"Host: localhost\r\n"
    b"X-Huge: "+b"Z"*50000+b"\r\n"
    b"Connection: close\r\n\r\n"
)
PY
    raw_request "raw_50k_header" 127.0.0.1 "$port" "400,431" "$RAW/huge_header.req"

    # --------------------------------------------------------------------------
    # Chunked body and request-smuggling defense.
    # The example comments explicitly mention raw chunked/smuggling tests on /abc.
    # --------------------------------------------------------------------------

    make_req "$RAW/chunked.req" \
        "POST /abc/chunk.txt HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"
    raw_observe "raw_chunked_post_to_abc_observation" 127.0.0.1 "$port" "$RAW/chunked.req"

    make_req "$RAW/cl_te.req" \
        "POST /abc/chunk.txt HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n0\r\n\r\n"
    raw_request "raw_cl_plus_te_rejected" 127.0.0.1 "$port" "400" "$RAW/cl_te.req"

    make_req "$RAW/unsupported_te.req" \
        "POST /abc/chunk.txt HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: gzip\r\nConnection: close\r\n\r\nabc"
    raw_request "raw_unsupported_transfer_encoding" 127.0.0.1 "$port" "400,501" "$RAW/unsupported_te.req"

    # --------------------------------------------------------------------------
    # Keep-alive
    # --------------------------------------------------------------------------

    new_test "keepalive_two_requests_same_connection"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/result.txt" <<'PY'
import socket,sys,re
host=sys.argv[1]; port=int(sys.argv[2]); out=sys.argv[3]
s=socket.socket(); s.settimeout(3); s.connect((host,port))
buf=b""
def one(req):
    global buf
    s.sendall(req)
    while b"\r\n\r\n" not in buf:
        b=s.recv(65536)
        if not b:return "",b""
        buf+=b
    head,buf=buf.split(b"\r\n\r\n",1)
    m=re.search(br"(?im)^Content-Length:\s*(\d+)\s*$",head)
    if m:
        n=int(m.group(1))
        while len(buf)<n:
            b=s.recv(65536)
            if not b:break
            buf+=b
        body=buf[:n]; buf=buf[n:]
    else:
        body=b""
    sm=re.match(br"HTTP/\d(?:\.\d)?\s+(\d{3})",head)
    return sm.group(1).decode() if sm else "",body
st1,b1=one(b"GET /static/hello.txt HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n")
try:
    st2,b2=one(b"GET /static/hello.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
except Exception:
    st2=""; b2=b""
open(out,"w").write("status1=%s\nstatus2=%s\n"% (st1,st2))
s.close()
PY
    copy_server_logs
    if ! server_alive_or_fail "keepalive_two_requests_same_connection"; then
        :
    elif grep -q '^status1=200$' "$CURRENT_TEST_DIR/result.txt" && grep -q '^status2=200$' "$CURRENT_TEST_DIR/result.txt"; then
        result PASS "keepalive_two_requests_same_connection" "two requests completed on one TCP connection"
    else
        result WARN "keepalive_two_requests_same_connection" "keep-alive did not complete both requests; inspect result.txt"
    fi

    # --------------------------------------------------------------------------
    # Partial client should not freeze event loop.
    # --------------------------------------------------------------------------

    new_test "partial_client_does_not_block_other_clients"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/partial.log" <<'PY' &
import socket,sys,time
s=socket.socket(); s.settimeout(5); s.connect((sys.argv[1],int(sys.argv[2])))
s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nX-Slow: ")
open(sys.argv[3],"w").write("partial header sent\n")
time.sleep(2)
s.close()
PY
    partial_pid=$!
    sleep .2
    quick="$(curl -sS --max-time 1.5 -o "$CURRENT_TEST_DIR/quick.body" -w '%{http_code}' "$base/static/hello.txt" 2>"$CURRENT_TEST_DIR/quick.stderr" || true)"
    wait "$partial_pid" 2>/dev/null || true
    copy_server_logs
    if ! server_alive_or_fail "partial_client_does_not_block_other_clients"; then
        :
    elif [ "$quick" = "200" ]; then
        result PASS "partial_client_does_not_block_other_clients" "normal request returned 200 while another socket held a partial header"
    else
        result FAIL "partial_client_does_not_block_other_clients" "normal request failed/status=$quick"
    fi

    # --------------------------------------------------------------------------
    # Moderate concurrency
    # --------------------------------------------------------------------------

    new_test "concurrency_100_parallel_gets"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/results.txt" <<'PY'
import concurrent.futures,http.client,sys,time
host=sys.argv[1]; port=int(sys.argv[2]); out=sys.argv[3]
def one(i):
    t=time.time()
    try:
        c=http.client.HTTPConnection(host,port,timeout=5)
        c.request("GET","/static/hello.txt",headers={"Host":"localhost","Connection":"close"})
        r=c.getresponse(); b=r.read(); c.close()
        return i,r.status,b"STATIC_HELLO_MARKER_9137" in b,time.time()-t,""
    except Exception as e:
        return i,0,False,time.time()-t,repr(e)
with concurrent.futures.ThreadPoolExecutor(max_workers=25) as ex:
    rows=list(ex.map(one,range(100)))
with open(out,"w") as f:
    for r in rows:f.write(repr(r)+"\n")
print(sum(1 for r in rows if r[1]==200 and r[2]))
PY
    ok="$(python3 - "$CURRENT_TEST_DIR/results.txt" <<'PY'
import ast,sys
n=0
for line in open(sys.argv[1],errors="replace"):
    try:
        r=ast.literal_eval(line.strip())
        if r[1]==200 and r[2]:n+=1
    except Exception:pass
print(n)
PY
)"
    copy_server_logs
    if ! server_alive_or_fail "concurrency_100_parallel_gets"; then
        :
    elif [ "$ok" = "100" ]; then
        result PASS "concurrency_100_parallel_gets" "100/100 returned correct 200 response"
    else
        result FAIL "concurrency_100_parallel_gets" "$ok/100 returned correct response"
    fi

    curl_test "final_survival_after_runtime_torture" GET "$base/static/hello.txt" "200"

    stop_server

    # --------------------------------------------------------------------------
    # host localhost + arbitrary index extension config runtime check.
    # --------------------------------------------------------------------------

    log "${BOLD}--- Example 3 runtime checks ---${RESET}"
    port="$(free_port)"
    cfg="$TMP_BASE/example3_runtime.conf"
    write_example3_compatible "$cfg" "$port"

    if start_server "$cfg" 127.0.0.1 "$port" "example3_runtime"; then
        base="http://127.0.0.1:$port"

        curl_test "example3_directory_custom_index_extension" GET "$base/directory/" "200"
        tdir="$CURRENT_TEST_DIR"
        assert_contains "example3_directory_custom_index_body" "$tdir/response.body" "YOUPI_BAD_EXTENSION_MARKER" WARN

        # We do not assert URL behavior of location .bla because the example proves
        # parser syntax, not its location-matching semantics.
        curl_observe "example3_dot_bla_location_runtime_observation" POST "$base/test.bla" --data 'hello-bla'

        stop_server
    else
        new_test "example3_runtime_start"
        result FAIL "example3_runtime_start" "example-3-shaped config failed at runtime"
    fi

    # --------------------------------------------------------------------------
    # Virtual hosts on same endpoint.
    # --------------------------------------------------------------------------

    log "${BOLD}--- Virtual host checks ---${RESET}"
    port="$(free_port)"
    cfg="$TMP_BASE/vhosts.conf"
    write_vhost_config "$cfg" "$port"

    if start_server "$cfg" 127.0.0.1 "$port" "vhosts"; then
        base="http://127.0.0.1:$port"

        curl_test "vhost_alpha" GET "$base/" "200" -H 'Host: alpha.local'
        tdir="$CURRENT_TEST_DIR"
        assert_contains "vhost_alpha_content" "$tdir/response.body" "VHOST_ALPHA_MARKER"

        curl_test "vhost_beta" GET "$base/" "200" -H 'Host: beta.local'
        tdir="$CURRENT_TEST_DIR"
        assert_contains "vhost_beta_content" "$tdir/response.body" "VHOST_BETA_MARKER"

        curl_test "vhost_beta_host_with_port" GET "$base/" "200" -H "Host: beta.local:$port"
        tdir="$CURRENT_TEST_DIR"
        assert_contains "vhost_beta_with_port_content" "$tdir/response.body" "VHOST_BETA_MARKER" WARN

        curl_observe "vhost_unknown_host_default_observation" GET "$base/" -H 'Host: unknown.local'

        stop_server
    else
        new_test "vhost_config_start"
        result FAIL "vhost_config_start" "two server blocks sharing host/port did not start"
    fi

    if [ "$RUN_STRESS" -eq 1 ]; then
        run_stress_suite
    fi
}

# ==============================================================================
# STRESS
# ==============================================================================

run_stress_suite() {
    log "${BOLD}=== HEAVIER STRESS ===${RESET}"

    local port cfg base
    port="$(free_port)"
    cfg="$TMP_BASE/stress.conf"
    write_runtime_config "$cfg" "$port"

    if ! start_server "$cfg" 127.0.0.1 "$port" "stress"; then
        new_test "stress_start"
        result FAIL "stress_start" "stress server failed to start"
        return 1
    fi

    base="http://127.0.0.1:$port"

    new_test "stress_1000_requests_concurrency_80"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/results.txt" <<'PY'
import concurrent.futures,http.client,sys,time
host=sys.argv[1]; port=int(sys.argv[2]); out=sys.argv[3]
N=1000
def one(i):
    try:
        c=http.client.HTTPConnection(host,port,timeout=8)
        c.request("GET","/static/hello.txt",headers={"Host":"localhost","Connection":"close"})
        r=c.getresponse(); b=r.read(); c.close()
        return i,r.status,b"STATIC_HELLO_MARKER_9137" in b,""
    except Exception as e:
        return i,0,False,repr(e)
with concurrent.futures.ThreadPoolExecutor(max_workers=80) as ex:
    rows=list(ex.map(one,range(N)))
with open(out,"w") as f:
    for r in rows:f.write(repr(r)+"\n")
print(sum(1 for r in rows if r[1]==200 and r[2]))
PY
    ok="$(python3 - "$CURRENT_TEST_DIR/results.txt" <<'PY'
import ast,sys
n=0
for line in open(sys.argv[1],errors="replace"):
    try:
        r=ast.literal_eval(line)
        if r[1]==200 and r[2]:n+=1
    except Exception:pass
print(n)
PY
)"
    copy_server_logs
    if ! server_alive_or_fail "stress_1000_requests_concurrency_80"; then
        :
    elif [ "$ok" = "1000" ]; then
        result PASS "stress_1000_requests_concurrency_80" "1000/1000 succeeded"
    else
        result FAIL "stress_1000_requests_concurrency_80" "$ok/1000 succeeded"
    fi

    new_test "stress_150_partial_clients"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/result.txt" <<'PY' &
import socket,sys,time
host=sys.argv[1]; port=int(sys.argv[2]); ss=[]
for i in range(150):
    try:
        s=socket.socket(); s.settimeout(2); s.connect((host,port))
        s.sendall(("GET / HTTP/1.1\r\nHost: localhost\r\nX-Slow-%d: "%i).encode())
        ss.append(s)
    except Exception:pass
open(sys.argv[3],"w").write("opened=%d\n"%len(ss))
time.sleep(2)
for s in ss:
    try:s.close()
    except Exception:pass
PY
    partial=$!
    sleep .4
    status="$(curl -sS --max-time 2 -o "$CURRENT_TEST_DIR/normal.body" -w '%{http_code}' "$base/static/hello.txt" 2>"$CURRENT_TEST_DIR/normal.stderr" || true)"
    wait "$partial" 2>/dev/null || true
    copy_server_logs
    if ! server_alive_or_fail "stress_150_partial_clients"; then
        :
    elif [ "$status" = "200" ]; then
        result PASS "stress_150_partial_clients" "server served normal request with 150 partial clients"
    else
        result FAIL "stress_150_partial_clients" "normal request failed/status=$status"
    fi

    new_test "stress_2000_connect_disconnect"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/result.txt" <<'PY'
import socket,sys
host=sys.argv[1];port=int(sys.argv[2]);ok=0
for _ in range(2000):
    try:
        s=socket.socket();s.settimeout(1);s.connect((host,port));s.close();ok+=1
    except Exception:pass
open(sys.argv[3],"w").write("successful=%d\n"%ok)
PY
    copy_server_logs
    if ! server_alive_or_fail "stress_2000_connect_disconnect"; then
        :
    else
        result PASS "stress_2000_connect_disconnect" "server survived connect/disconnect storm"
    fi

    new_test "stress_200_cgi_requests"
    python3 - 127.0.0.1 "$port" "$CURRENT_TEST_DIR/results.txt" <<'PY'
import concurrent.futures,http.client,sys
host=sys.argv[1];port=int(sys.argv[2]);out=sys.argv[3]
def one(i):
    try:
        c=http.client.HTTPConnection(host,port,timeout=8)
        c.request("GET","/cgi/echo.py?n=%d"%i,headers={"Host":"localhost","Connection":"close"})
        r=c.getresponse();b=r.read();c.close()
        return i,r.status,b"CGI_ECHO_MARKER" in b,""
    except Exception as e:
        return i,0,False,repr(e)
with concurrent.futures.ThreadPoolExecutor(max_workers=25) as ex:
    rows=list(ex.map(one,range(200)))
with open(out,"w") as f:
    for r in rows:f.write(repr(r)+"\n")
print(sum(1 for r in rows if r[1]==200 and r[2]))
PY
    ok="$(python3 - "$CURRENT_TEST_DIR/results.txt" <<'PY'
import ast,sys
n=0
for line in open(sys.argv[1],errors="replace"):
    try:
        r=ast.literal_eval(line)
        if r[1]==200 and r[2]:n+=1
    except Exception:pass
print(n)
PY
)"
    copy_server_logs
    if ! server_alive_or_fail "stress_200_cgi_requests"; then
        :
    elif [ "$ok" = "200" ]; then
        result PASS "stress_200_cgi_requests" "200/200 CGI requests succeeded"
    else
        result FAIL "stress_200_cgi_requests" "$ok/200 CGI requests succeeded"
    fi

    curl_test "stress_final_survival" GET "$base/static/hello.txt" "200"
    stop_server
}

final_report() {
    local elapsed=$(( $(date +%s)-START_EPOCH ))
    local total=$((PASS+FAIL+WARN+OBSERVE))
    {
        echo
        echo "============================================================================"
        echo "FINAL SUMMARY"
        echo "============================================================================"
        echo "Total checks : $total"
        echo "PASS         : $PASS"
        echo "FAIL         : $FAIL"
        echo "WARN         : $WARN"
        echo "OBSERVE      : $OBSERVE"
        echo "Elapsed      : ${elapsed}s"
        echo "Results      : $RESULTS_DIR"
        echo
        echo "This tester intentionally does NOT enforce a guessed config grammar."
        echo "Example-backed syntax is strict; unspecified parser semantics are OBSERVE."
        echo
        echo "Start debugging with:"
        echo "  $FAILURES_LOG"
        echo
        echo "Then open the numbered test directory; each one keeps request/response/logs."
        echo "============================================================================"
    } | tee -a "$SUMMARY_LOG"

    if [ "$FAIL" -gt 0 ]; then
        echo
        echo "${RED}${BOLD}Failures:${RESET}"
        cat "$FAILURES_LOG"
    fi
    if [ "$WARN" -gt 0 ]; then
        echo
        echo "${YELLOW}${BOLD}Warnings:${RESET}"
        cat "$WARNINGS_LOG"
    fi
}

log "examples-based webserv tester"
log "binary: $WEBSERV_BIN"
log "results: $RESULTS_DIR"
log "No guessed grammar is being enforced."

case "$MODE" in
    all)
        run_config_suite
        run_runtime_suite
        ;;
    config)
        run_config_suite
        ;;
    runtime)
        run_runtime_suite
        ;;
esac

final_report

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
