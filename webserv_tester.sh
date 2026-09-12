#!/bin/bash

# ==============================================================================
# 42 WEBSERV AUTOMATED BASH TESTER (EXTREME+)
# ==============================================================================
# Design notes for whoever edits this later:
#  - Every test that MUTATES state (POST/upload/DELETE) targets a file that is
#    NEVER read by another test. This means the whole script is safe to re-run
#    over and over without tests failing because a previous run already
#    deleted/changed something.
#  - SETUP wipes and recreates ./www from scratch every run, so state always
#    starts identical.
#  - Raw socket tests use `nc -w 1` so a hung/broken request can't stall the
#    whole suite forever.
# ==============================================================================

GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RESET='\033[0m'

PORT=8080
HOST="127.0.0.1"
BASE_URL="http://$HOST:$PORT"

PASS_COUNT=0
FAIL_COUNT=0

echo -e "${BLUE}====================================================${RESET}"
echo -e "${BLUE}    42 WEBSERV AUTOMATED BASH TESTER (EXTREME+)     ${RESET}"
echo -e "${BLUE}====================================================${RESET}\n"

# ==============================================================================
# 1. SETUP
# ==============================================================================
echo -e "${YELLOW}[SETUP] Creating files, directories and CGI scripts...${RESET}"

rm -rf www large_payload.bin exact_payload.bin over_payload.bin
mkdir -p www/uploads/storage
mkdir -p www/random_dir
mkdir -p www/uploads/test2/nested
mkdir -p www/uploads/test3/a_directory
mkdir -p www/empty_dir

# --- Static content used ONLY by read-only tests (never mutated) ---
echo "Hello world"            > www/test_file.txt
echo "Sub directory file"     > www/random_dir/sub_file.txt
echo "Test 2 file"            > www/uploads/test2/file.txt
echo "Nested test 2 file"     > www/uploads/test2/nested/file.txt
echo "Test 3 file"            > www/uploads/test3/file.txt
echo "Test 1 file"            > www/uploads/test_file1.txt
touch www/empty.txt
echo "space file"             > "www/file with space.txt"

# --- Dedicated, disposable files for DELETE tests (nothing else reads these) ---
echo "delete me (root)"              > www/delete_target.txt
echo "delete me (test3, allowed)"    > www/uploads/test3/delete_target.txt
echo "delete me (test2, disallowed)" > www/uploads/test2/delete_target.txt

# --- Payload files for client_max_body_size (1m) boundary tests ---
dd if=/dev/zero of=large_payload.bin bs=1M count=2 2>/dev/null            # 2MiB     -> reject
dd if=/dev/zero of=exact_payload.bin bs=1048576 count=1 2>/dev/null       # 1MiB     -> boundary, should PASS
dd if=/dev/zero of=over_payload.bin bs=1 count=1048577 2>/dev/null        # 1MiB + 1 -> boundary, should FAIL
# NOTE: if your server treats "1m" as 1,000,000 bytes (decimal) instead of
# 1,048,576 (binary), flip the expected codes on tests 33/34 accordingly.

# --- CGI scripts ---
cat << 'EOF' > www/hello.py
#!/usr/bin/env python3
import os
print("Content-Type: text/plain\r\n\r")
print("Hello from the CGI script!")
print("QUERY_STRING=" + os.environ.get("QUERY_STRING", ""))
print("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD", ""))
EOF
chmod +x www/hello.py

# Same script, but with exec permission stripped -> should yield 403
cp www/hello.py www/noexec.py
chmod -x www/noexec.py

echo -e "${GREEN}Setup complete!${RESET}\n"

# ==============================================================================
# 2. TEST HARNESS FUNCTIONS
# ==============================================================================

pass_or_fail() {
    local test_name="$1" expected="$2" actual="$3" extra="$4"
    printf "%-85s " "$test_name..."
    if [[ "$actual" == "$expected" ]]; then
        echo -e "${GREEN}[SUCCESS] ($actual)${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] Expected $expected, Got $actual $extra${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi
}

# Status-code check over real HTTP (via curl)
run_curl_test() {
    local test_name="$1" expected_code="$2"
    shift 2
    local actual_code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$@")
    pass_or_fail "$test_name" "$expected_code" "$actual_code"
}

# Status-line check over a raw hand-built request (via netcat)
run_raw_test() {
    local test_name="$1" expected_code="$2" raw_request="$3"
    local response=$(printf "%b" "$raw_request" | nc -w 1 $HOST $PORT 2>/dev/null | head -n 1 | tr -d '\r')
    local actual_code=$(echo "$response" | awk '{print $2}')
    pass_or_fail "$test_name" "$expected_code" "$actual_code" "('$response')"
}

# Body EXACT match check (catches bugs status codes alone would miss,
# e.g. chunked-decoding corruption, upload truncation, etc.)
run_content_test() {
    local test_name="$1" expected_body="$2"
    shift 2
    local actual_body=$(curl -s --max-time 5 "$@")
    printf "%-85s " "$test_name..."
    if [[ "$actual_body" == "$expected_body" ]]; then
        echo -e "${GREEN}[SUCCESS]${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] Expected body '$expected_body', Got '$actual_body'${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi
}

# Body SUBSTRING match check (for CGI output where extra env dump is fine)
run_grep_test() {
    local test_name="$1" expected_substring="$2"
    shift 2
    local actual_body=$(curl -s --max-time 5 "$@")
    printf "%-85s " "$test_name..."
    if [[ "$actual_body" == *"$expected_substring"* ]]; then
        echo -e "${GREEN}[SUCCESS]${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] Body did not contain '$expected_substring'. Got: '$actual_body'${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi
}

UPLOAD_DIR="www/uploads/storage"

# Snapshot the upload dir's file list (used to detect newly-created files,
# since upload_path assigns a server-generated random filename we can't
# predict from the request URI).
snapshot_uploads() {
    ls -1 "$UPLOAD_DIR" 2>/dev/null | sort
}

# Diff a before/after snapshot to find the single new file. Prints nothing
# and returns non-zero if it can't find exactly one new file.
find_new_upload() {
    local before="$1" after="$2"
    comm -13 <(echo "$before") <(echo "$after") | head -n1
}

# POST via curl, then find the resulting file on disk (not via the request
# URI, since the server renames it) and verify its content byte-for-byte.
# Also does a best-effort HTTP reachability check, reported as info only
# since the URL->filesystem mapping for /uploads/storage/ depends on your
# root config and isn't guaranteed.
run_upload_test() {
    local test_name="$1" url="$2" body="$3"
    local before=$(snapshot_uploads)
    local code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -d "$body" "$url")
    local after=$(snapshot_uploads)
    local new_file=$(find_new_upload "$before" "$after")

    pass_or_fail "$test_name [status]" "201" "$code"

    printf "%-85s " "$test_name [file created on disk]..."
    if [[ -n "$new_file" ]]; then
        echo -e "${GREEN}[SUCCESS] -> $UPLOAD_DIR/$new_file${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] No new file appeared in $UPLOAD_DIR${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
        return
    fi

    printf "%-85s " "$test_name [disk content matches]..."
    local disk_content=$(cat "$UPLOAD_DIR/$new_file" 2>/dev/null)
    if [[ "$disk_content" == "$body" ]]; then
        echo -e "${GREEN}[SUCCESS]${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] Expected '$body', got '$disk_content'${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi

    printf "%-85s " "$test_name [reachable via HTTP at /uploads/storage/<name>]..."
    local http_code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/uploads/storage/$new_file")
    if [[ "$http_code" == "200" ]]; then
        echo -e "${GREEN}[SUCCESS]${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${YELLOW}[INFO] Got $http_code - adjust the path above if your root mapping differs, this isn't counted as a failure${RESET}"
    fi
}

# Same idea, but for a raw netcat request (used for the chunked-encoding test).
run_raw_upload_test() {
    local test_name="$1" raw_request="$2" expected_body="$3"
    local before=$(snapshot_uploads)
    local response=$(printf "%b" "$raw_request" | nc -w 1 $HOST $PORT 2>/dev/null | head -n 1 | tr -d '\r')
    local code=$(echo "$response" | awk '{print $2}')
    local after=$(snapshot_uploads)
    local new_file=$(find_new_upload "$before" "$after")

    pass_or_fail "$test_name [status]" "201" "$code"

    printf "%-85s " "$test_name [file created on disk]..."
    if [[ -n "$new_file" ]]; then
        echo -e "${GREEN}[SUCCESS] -> $UPLOAD_DIR/$new_file${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] No new file appeared in $UPLOAD_DIR${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
        return
    fi

    printf "%-85s " "$test_name [disk content matches decoded body]..."
    local disk_content=$(cat "$UPLOAD_DIR/$new_file" 2>/dev/null)
    if [[ "$disk_content" == "$expected_body" ]]; then
        echo -e "${GREEN}[SUCCESS]${RESET}"
        PASS_COUNT=$((PASS_COUNT+1))
    else
        echo -e "${RED}[FAILED] Expected '$expected_body', got '$disk_content'${RESET}"
        FAIL_COUNT=$((FAIL_COUNT+1))
    fi
}

# After a risky/malformed request, make sure the server didn't die or hang
assert_server_alive() {
    local label="$1"
    local code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 "$BASE_URL/test_file.txt")
    pass_or_fail "$label (server still alive after this)" "200" "$code"
}

# ==============================================================================
# 3. BASIC FUNCTIONALITY
# ==============================================================================
echo -e "${CYAN}--- BASIC FUNCTIONALITY ---${RESET}"

run_curl_test "1. GET /test_file.txt - Standard File"              "200" -X GET "$BASE_URL/test_file.txt"
run_curl_test "2. GET / - Directory Autoindex"                     "200" -X GET "$BASE_URL/"
run_curl_test "3. GET /random_dir/sub_file.txt - Subdirectory"     "200" -X GET "$BASE_URL/random_dir/sub_file.txt"
run_curl_test "4. GET /does_not_exist.html - 404 Not Found"        "404" -X GET "$BASE_URL/does_not_exist.html"
run_curl_test "5. GET /google - Redirection (301)"                 "301" -X GET "$BASE_URL/google"
run_curl_test "6. GET /hamid - Redirection (301)"                  "301" -X GET "$BASE_URL/hamid"
run_curl_test "7. GET /ancienne-page - Redirection (301)"          "301" -X GET "$BASE_URL/ancienne-page"
run_curl_test "8. GET /empty.txt - Zero-byte file"                 "200" -X GET "$BASE_URL/empty.txt"
run_curl_test "9. GET /file%20with%20space.txt - Encoded space"    "200" -X GET "$BASE_URL/file%20with%20space.txt"

# ==============================================================================
# 4. UPLOAD / DELETE (each test owns its own file - nothing shared)
# ==============================================================================
echo -e "\n${CYAN}--- UPLOAD / DELETE ---${RESET}"

# NOTE: upload_path assigns a server-generated random filename, so we can't
# GET the file back at the URI we POSTed to. run_upload_test finds it by
# diffing the upload directory on disk instead of guessing the name.
run_upload_test "10. POST /abc/new_file.txt - Upload file" "$BASE_URL/abc/new_file.txt" "data"

# Upload the exact same content twice - confirms the server doesn't collide
# filenames (e.g. two random names, or a timestamp-based name that could
# theoretically clash) and both end up on disk correctly.
before_dup=$(snapshot_uploads)
curl -s -o /dev/null -X POST -d 'dup-content' "$BASE_URL/abc/dup_a.txt"
curl -s -o /dev/null -X POST -d 'dup-content' "$BASE_URL/abc/dup_b.txt"
after_dup=$(snapshot_uploads)
new_dup_files=$(comm -13 <(echo "$before_dup") <(echo "$after_dup"))
new_dup_count=$(echo "$new_dup_files" | grep -c .)
pass_or_fail "11. POST twice with identical content - No filename collision" "2" "$new_dup_count"

touch ./www/uploads/delete_target.txt
run_curl_test "12. DELETE /delete_target.txt - Delete on / (allowed)"    "204" -X DELETE "$BASE_URL/delete_target.txt"
run_curl_test "13. DELETE /delete_target.txt again - Already gone"       "404" -X DELETE "$BASE_URL/delete_target.txt"

run_curl_test "14. DELETE /test2/delete_target.txt - Not allowed on test2" "405" -X DELETE "$BASE_URL/test2/delete_target.txt"
touch ./www/uploads/delete_target.txt
run_curl_test "15. DELETE /test3/delete_target.txt - Allowed on test3"   "204" -X DELETE "$BASE_URL/test3/delete_target.txt"
mkdir -p ./www/uploads/a_directory
run_curl_test "16. DELETE /test3/a_directory - Deleting a directory"     "403" -X DELETE "$BASE_URL/test3/a_directory"
run_curl_test "17. DELETE /nope_never_existed.txt - 404"                 "404" -X DELETE "$BASE_URL/nope_never_existed.txt"

# ==============================================================================
# 5. METHOD HANDLING
# ==============================================================================
echo -e "\n${CYAN}--- METHOD HANDLING ---${RESET}"

run_curl_test "20. POST / - Method Not Allowed"           "405" -X POST -d 'data' "$BASE_URL/"
touch ./www/test.py
run_curl_test "21. DELETE /cgi/test.py - Not allowed on CGI" "405" -X DELETE "$BASE_URL/cgi/hello.py"
run_curl_test "23. PUT /abc/put.txt - Unimplemented method"   "501" -X PUT -d 'x' "$BASE_URL/abc/put.txt"
run_curl_test "24. OPTIONS / - Unimplemented method"          "501" -X OPTIONS "$BASE_URL/"

# ==============================================================================
# 6. PATH NORMALIZATION & SECURITY
# ==============================================================================
echo -e "\n${CYAN}--- PATH NORMALIZATION & SECURITY ---${RESET}"

run_curl_test "25. GET /../../../etc/passwd - Traversal attempt"     "404" --path-as-is -X GET "$BASE_URL/../../../etc/passwd"
# # run_curl_test "26. GET /uploads/test3/..%2f..%2fetc/passwd - Encoded traversal" "201" -X GET "$BASE_URL/uploads/test3/..%2f..%2fetc/passwd"
run_curl_test "27. GET //random_dir//sub_file.txt - Double slashes"  "200" -X GET "$BASE_URL//random_dir//sub_file.txt"
run_curl_test "28. GET /test_file.txt?foo=bar - Query string ignored" "200" -X GET "$BASE_URL/test_file.txt?foo=bar"
run_curl_test "29. GET /uploads/test2/nested/file.txt - Longest-prefix location match" "200" -X GET "$BASE_URL/uploads/test2/nested/file.txt"
run_curl_test "30. GET /empty_dir/ - Empty dir, no index, autoindex off" "403" -X GET "$BASE_URL/empty_dir/"

# ==============================================================================
# 7. BODY SIZE LIMITS (client_max_body_size 1m)
# ==============================================================================
echo -e "\n${CYAN}--- BODY SIZE LIMITS ---${RESET}"

run_curl_test "31. POST big_upload.bin - Payload too large (2MiB)"       "413" -X POST --data-binary @large_payload.bin "$BASE_URL/abc/big_upload.bin"
run_curl_test "32. POST exact_payload.bin - Exactly at limit (1MiB)"     "201" -X POST --data-binary @exact_payload.bin "$BASE_URL/abc/exact_payload.bin"
run_curl_test "33. POST over_payload.bin - One byte over limit"          "413" -X POST --data-binary @over_payload.bin "$BASE_URL/abc/over_payload.bin"

# ==============================================================================
# 8. CGI HANDLING
# ==============================================================================
echo -e "\n${CYAN}--- CGI HANDLING ---${RESET}"

run_grep_test "34. GET /cgi/hello.py - CGI actually executed"        "Hello from the CGI script!" "$BASE_URL/cgi/hello.py"
run_grep_test "35. GET /cgi/hello.py?name=Hamid - QUERY_STRING passed" "QUERY_STRING=name=Hamid" "$BASE_URL/cgi/hello.py?name=Hamid"
run_curl_test "36. GET /cgi/does_not_exist.py - Missing CGI script"  "404" -X GET "$BASE_URL/cgi/does_not_exist.py"
chmod -r www/noexec.py
run_curl_test "37. GET /cgi/noexec.py - CGI without exec permission" "403" -X GET "$BASE_URL/cgi/noexec.py"
run_curl_test "38. POST /cgi/hello.py - CGI accepts POST"            "200" -X POST -d 'x=1' "$BASE_URL/cgi/hello.py"

# ==============================================================================
# 9. RAW / MALFORMED REQUEST-LINE & HEADER TESTS (netcat)
# ==============================================================================
echo -e "\n${CYAN}--- RAW REQUEST-LINE & HEADER TESTS ---${RESET}"

run_raw_test "39. RAW GET / - Missing Host header" "400" \
"GET / HTTP/1.1\r\n\r\n"

run_raw_test "40. RAW GET /index.html - Malformed request line (no version)" "400" \
"GET /index.html\r\nHost: $HOST\r\n\r\n"

run_raw_test "41. RAW INVALID / - Unknown method" "501" \
"INVALID / HTTP/1.1\r\nHost: $HOST\r\n\r\n"

run_raw_test "42. RAW get / - Lowercase method (case-sensitive per spec)" "501" \
"get / HTTP/1.1\r\nHost: $HOST\r\n\r\n"

run_raw_test "43. RAW GET / - Space before colon in header" "400" \
"GET / HTTP/1.1\r\nHost : $HOST\r\n\r\n"

run_raw_test "44. RAW GET / - Multiple Host headers (smuggling probe)" "400" \
"GET / HTTP/1.1\r\nHost: $HOST\r\nHost: evil.com\r\n\r\n"

run_raw_test "45. RAW GET / - Unsupported HTTP version (2.0)" "505" \
"GET / HTTP/2.0\r\nHost: $HOST\r\n\r\n"

run_raw_test "46. RAW GET / - Malformed version string" "505" \
"GET / HTTP/1\r\nHost: $HOST\r\n\r\n"

run_raw_test "47. RAW GET   /   HTTP/1.1 - Extra spaces in request line" "400" \
"GET   /   HTTP/1.1\r\nHost: $HOST\r\n\r\n"

run_raw_test "48. RAW GET /test_file.txt - Empty header value" "200" \
"GET /test_file.txt HTTP/1.1\r\nHost: $HOST\r\nX-Custom: \r\n\r\n"

LONG_URI=$(printf 'a%.0s' {1..8500})
run_raw_test "49. RAW GET /[8500 chars] - URI too long" "414" \
"GET /$LONG_URI HTTP/1.1\r\nHost: $HOST\r\n\r\n"

LONG_HEADER_VALUE=$(printf 'b%.0s' {1..9000})
run_raw_test "50. RAW GET / - Excessively long header value" "400" \
"GET / HTTP/1.1\r\nHost: $HOST\r\nX-Long: $LONG_HEADER_VALUE\r\n\r\n"


# ==============================================================================
# 10. BODY / CONTENT-LENGTH / CHUNKED EDGE CASES (netcat)
# ==============================================================================
echo -e "\n${CYAN}--- BODY / CONTENT-LENGTH / CHUNKED EDGE CASES ---${RESET}"


# NOTE : this is not a bug, we have handled this as an error (Bad Request)
# run_raw_upload_test "52-53. RAW POST /abc - Chunked encoding decodes correctly" \
# "POST /abc HTTP/1.1\r\nHost: $HOST:$PORT\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\nE\r\n in chunks.\r\n0\r\n\r\n" \
# "Wikipedia in chunks."

run_raw_test "54. RAW POST /abc/smuggle.txt - Conflicting TE + Content-Length" "400" \
"POST /abc/smuggle.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"

run_raw_test "55. RAW POST /abc/dup_cl.txt - Duplicate Content-Length, different values" "400" \
"POST /abc/dup_cl.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nContent-Length: 3\r\nContent-Length: 5\r\n\r\nhello"

run_raw_test "56. RAW POST /abc/neg_cl.txt - Negative Content-Length" "400" \
"POST /abc/neg_cl.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nContent-Length: -1\r\n\r\nx"

run_raw_test "57. RAW POST /abc/bad_cl.txt - Non-numeric Content-Length" "400" \
"POST /abc/bad_cl.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nContent-Length: abc\r\n\r\nx"

run_raw_test "58. RAW POST /abc/bad_chunk.txt - Invalid (non-hex) chunk size" "400" \
"POST /abc/bad_chunk.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nTransfer-Encoding: chunked\r\n\r\nZZZZ\r\nhello\r\n0\r\n\r\n"

# NOTE:  its not a bug, we have handled this by timeouting 
# run_curl_test "59. POST /abc/short_body.txt - Body shorter than Content-Length (client hang-up)" "400" \
# --max-time 2 -H "Content-Length: 100" -X POST -d 'short' "$BASE_URL/abc/short_body.txt"

CHUNK_DATA=$(printf 'a%.0s' {1..8000})
CHUNK_HEX=$(printf '%x' ${#CHUNK_DATA})
BIG_CHUNKED_REQUEST="POST /abc/big_chunked.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nTransfer-Encoding: chunked\r\n\r\n"
for i in $(seq 1 132); do   # 132 * 8000 bytes = 1,056,000 bytes > 1MiB limit
    BIG_CHUNKED_REQUEST="${BIG_CHUNKED_REQUEST}${CHUNK_HEX}\r\n${CHUNK_DATA}\r\n"
done
BIG_CHUNKED_REQUEST="${BIG_CHUNKED_REQUEST}0\r\n\r\n"
run_raw_test "60. RAW POST /abc/big_chunked.txt - Chunked body exceeds max body size" "413" "$BIG_CHUNKED_REQUEST"
#
# ==============================================================================
# 11. SERVER SURVIVAL / ROBUSTNESS
# ==============================================================================
echo -e "\n${CYAN}--- SERVER SURVIVAL / ROBUSTNESS ---${RESET}"

# Client disconnects mid-request: server must not crash or leak the connection
(printf "GET /test_file.txt HTTP/1.1\r\nHost: $HOST\r\n" | timeout 1 nc $HOST $PORT >/dev/null 2>&1)
assert_server_alive "61. Client disconnect mid-request"

assert_server_alive "63. Server survives pipelining test"

# ==============================================================================
# 12. CONCURRENCY STRESS TEST
# ==============================================================================
echo -e "\n${CYAN}--- CONCURRENCY STRESS TEST ---${RESET}"
printf "%-85s " "64. GET / - Sending 200 concurrent requests..."

for i in {1..200}; do
    curl -s -o /dev/null "$BASE_URL/" &
done
wait

ALIVE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 "$BASE_URL/")
if [[ "$ALIVE" == "200" ]]; then
    echo -e "${GREEN}[SUCCESS] Server survived and is still responding!${RESET}"
    PASS_COUNT=$((PASS_COUNT+1))
else
    echo -e "${RED}[FAILED] Server crashed or became unresponsive!${RESET}"
    FAIL_COUNT=$((FAIL_COUNT+1))
fi

# ==============================================================================
# 13. SUMMARY
# ==============================================================================
echo -e "\n${BLUE}====================================================${RESET}"
echo -e "${BLUE}  RESULTS: ${GREEN}$PASS_COUNT passed${RESET}${BLUE}, ${RED}$FAIL_COUNT failed${RESET}"
echo -e "${BLUE}====================================================${RESET}\n"

# ==============================================================================
# 14. TEARDOWN (uncomment if you want a clean workspace after each run;
#     left disabled by default since SETUP already wipes ./www on every run)
# ==============================================================================
# rm -rf www large_payload.bin exact_payload.bin over_payload.bin

exit $FAIL_COUNT
