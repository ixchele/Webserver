#!/bin/bash

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RESET='\033[0m'

PORT=8080
HOST="127.0.0.1"
BASE_URL="http://$HOST:$PORT"

echo -e "${BLUE}====================================================${RESET}"
echo -e "${BLUE}    42 WEBSERV AUTOMATED BASH TESTER (EXTREME)      ${RESET}"
echo -e "${BLUE}====================================================${RESET}\n"

# ==============================================================================
# 1. SETUP: Create required files
# ==============================================================================
echo -e "${YELLOW}[SETUP] Creating files and CGI scripts...${RESET}"
mkdir -p www/uploads/storage
mkdir -p www/random_dir

# Directories for your new test2 and test3 location blocks
mkdir -p www/uploads/test2
mkdir -p www/uploads/test3

echo "Hello world" > www/test_file.txt
echo "Sub directory file" > www/random_dir/sub_file.txt
echo "Test 2 file" > www/uploads/test2/file.txt
echo "Test 3 file" > www/uploads/test3/file.txt
echo "Test 1 file" > www/uploads/test_file1.txt

# Create a 2MB file to test client_max_body_size (1m in your config)
dd if=/dev/zero of=large_payload.bin bs=1M count=2 2>/dev/null

# Create a simple Python CGI script
cat << 'EOF' > www/hello.py
#!/usr/bin/env python3
print("Content-Type: text/plain\r\n\r")
print("Hello from the CGI script!")
EOF
chmod +x www/hello.py

echo -e "${GREEN}Setup complete!${RESET}\n"

# ==============================================================================
# 2. TEST HARNESS FUNCTIONS
# ==============================================================================

run_curl_test() {
    local test_name="$1"
    local expected_code="$2"
    shift 2
    local curl_args="$@"

    printf "%-85s " "$test_name..."
    local actual_code=$(curl -s -o /dev/null -w "%{http_code}" $curl_args)

    if [[ "$actual_code" == "$expected_code" ]]; then
        echo -e "${GREEN}[SUCCESS] ($actual_code)${RESET}"
    else
        echo -e "${RED}[FAILED] Expected $expected_code, Got $actual_code${RESET}"
    fi
}

run_raw_test() {
    local test_name="$1"
    local expected_code="$2"
    local raw_request="$3"

    printf "%-85s " "$test_name..."
    local response=$(printf "%b" "$raw_request" | nc -w 1 $HOST $PORT 2>/dev/null | head -n 1 | tr -d '\r')
    local actual_code=$(echo "$response" | awk '{print $2}')

    if [[ "$actual_code" == "$expected_code" ]]; then
        echo -e "${GREEN}[SUCCESS] ($actual_code)${RESET}"
    else
        echo -e "${RED}[FAILED] Expected $expected_code, Got $actual_code ('$response')${RESET}"
    fi
}

# ==============================================================================
# 3. RUNNING THE TESTS
# ==============================================================================
# echo -e "${YELLOW}--- STANDARD & CGI TESTS ---${RESET}"

run_curl_test "1. GET /test_file.txt - Standard File" "200" "-X GET $BASE_URL/test_file.txt"
run_curl_test "2. GET / - Directory Autoindex" "200" "-X GET $BASE_URL/"
run_curl_test "3. GET /random_dir/sub_file.txt - File in Subdirectory" "200" "-X GET $BASE_URL/random_dir/sub_file.txt"
run_curl_test "4. GET /does_not_exist.html - 404 Not Found" "404" "-X GET $BASE_URL/does_not_exist.html"
run_curl_test "5. GET /google - Redirection (301)" "301" "-X GET $BASE_URL/google"
run_curl_test "6. POST /abc/new_file.txt - Upload file to /abc" "201" "-X POST -d 'data' $BASE_URL/abc/new_file.txt"
run_curl_test "7. DELETE /test_file.txt - Delete the test file" "204" "-X DELETE $BASE_URL/test_file.txt"
run_curl_test "8. GET /hello.py - Execute Python CGI Script" "200" "-X GET $BASE_URL/hello.py"

# echo -e "\n${YELLOW}--- COMPLEX SECURITY & CONFIG TESTS ---${RESET}"

run_curl_test "9. GET /../../../etc/passwd - Directory Traversal Attempt" "404" "-X GET $BASE_URL/../../../etc/passwd" --path-as-is
run_curl_test "10. POST / - Method Not Allowed" "405" "-X POST -d 'data' $BASE_URL/"
run_curl_test "11. POST /abc/big_upload.bin - Payload Too Large (>1MB)" "413" "-X POST --data-binary @large_payload.bin $BASE_URL/abc/big_upload.bin"

Properly numbered DELETE tests based on your locations
run_curl_test "12. DELETE /cgi/hello.py - Method Not Allowed on CGI" "405" "-X DELETE $BASE_URL/cgi/hello.py"
run_curl_test "13. DELETE /test2/file.txt - Method Not Allowed on test2" "405" "-X DELETE $BASE_URL/test2/file.txt"
run_curl_test "14. DELETE /test3/file.txt - Allowed on test3" "204" "-X DELETE $BASE_URL/test3/file.txt"

# # echo -e "\n${YELLOW}--- RAW / EDGE CASE TESTS (Netcat) ---${RESET}"

run_raw_test "15. RAW GET / - Missing Host Header" "400" \
"GET / HTTP/1.1\r\n\r\n"

run_raw_test "16. RAW GET /index.html - Malformed Request Line" "400" \
"GET /index.html\r\nHost: $HOST\r\n\r\n"

run_raw_test "17. RAW INVALID / - Unknown/Bad Method" "501" \
"INVALID / HTTP/1.1\r\nHost: $HOST\r\n\r\n"

run_raw_test "18. RAW GET / - Space Before Colon in Header" "400" \
"GET / HTTP/1.1\r\nHost : $HOST\r\n\r\n"

LONG_URI=$(printf 'a%.0s' {1..8500})
run_raw_test "19. RAW GET /[8500_chars] - URI Too Long" "414" \
"GET /$LONG_URI HTTP/1.1\r\nHost: $HOST\r\n\r\n"

run_raw_test "20. RAW POST /abc/chunk.txt - Chunked Transfer Encoding" "201" \
"POST /abc/chunk.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n"

run_raw_test "21. RAW GET / - Multiple Host Headers (Smuggling Probe)" "400" \
"GET / HTTP/1.1\r\nHost: $HOST\r\nHost: evil.com\r\n\r\n"

run_raw_test "22. RAW GET / - Unsupported HTTP Version (HTTP/2.0)" "505" \
"GET / HTTP/2.0\r\nHost: $HOST\r\n\r\n"

run_raw_test "23. RAW POST /abc/smuggle.txt - Conflicting TE and CL Headers" "400" \
"POST /abc/smuggle.txt HTTP/1.1\r\nHost: $HOST:$PORT\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"

run_raw_test "24. RAW GET /test_file.txt - Empty Header Value" "200" \
"GET /test_file.txt HTTP/1.1\r\nHost: $HOST\r\nCustom-Header: \r\n\r\n"

# ==============================================================================
# 4. CONCURRENCY STRESS TEST
# ==============================================================================
# echo -e "\n${YELLOW}--- STRESS TESTING (CONCURRENCY) ---${RESET}"
# printf "%-85s " "25. GET / - Sending 200 concurrent requests..."

# # Launch 200 curl requests in the background simultaneously
# for i in {1..200}; do
#     curl -s -o /dev/null $BASE_URL/ &
# done

# wait 

# ALIVE=$(curl -s -o /dev/null -w "%{http_code}" $BASE_URL/)
# if [[ "$ALIVE" == "200" ]]; then
#     echo -e "${GREEN}[SUCCESS] Server survived and is still responding!${RESET}"
# else
#     echo -e "${RED}[FAILED] Server crashed or became unresponsive!${RESET}"
# fi

# # ==============================================================================
# # 5. TEARDOWN
# # ==============================================================================
# echo -e "\n${YELLOW}[TEARDOWN] Cleaning up...${RESET}"
# rm -rf www/random_dir
# rm -rf www/uploads/test2
# rm -rf www/uploads/test3
# rm -f large_payload.bin
# rm -f www/hello.py
# echo -e "${GREEN}Cleanup complete!${RESET}\n"