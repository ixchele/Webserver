#!/usr/bin/env python3

import os
import sys

def main():
    # 1. Extract the session ID from the HTTP_COOKIE environment variable
    cookie_header = os.environ.get('HTTP_COOKIE', '')
    session_id = None
    
    # The test sends a single cookie like: "Cookie: SESSION_NAME=12345"
    if cookie_header and '=' in cookie_header:
        # Split by '=' and take the value (the right side)
        # Using .split(';') first handles cases if multiple cookies are sent
        first_cookie = cookie_header.split(';')[0]
        if '=' in first_cookie:
            session_id = first_cookie.split('=', 1)[1].strip()

    # 2. If there is no cookie, fail early so the test can catch "NO-SESSION"
    if not session_id:
        body = "NO-SESSION"
    else:
        # 3. Read the body from stdin based on CONTENT_LENGTH
        length = int(os.environ.get('CONTENT_LENGTH', 0))
        data = sys.stdin.read(length) if length > 0 else ''
        
        # 4. Write to the shared file store
        filepath = f"/tmp/sess_{session_id}.txt"
        try:
            with open(filepath, "w") as f:
                f.write(data)
            body = "WRITE-OK"
        except IOError:
            body = "FILE-ERROR"

    # 5. Output valid HTTP CGI response
    print("Content-Type: text/plain")
    print(f"Content-Length: {len(body)}")
    print()  # Empty line separating headers from body
    print(body, end="")

if __name__ == '__main__':
    main()