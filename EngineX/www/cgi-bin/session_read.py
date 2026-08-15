#!/usr/bin/env python3

import os

def main():
    # 1. Extract the session ID from the HTTP_COOKIE environment variable
    cookie_header = os.environ.get('HTTP_COOKIE', '')
    session_id = None
    
    if cookie_header and '=' in cookie_header:
        first_cookie = cookie_header.split(';')[0]
        if '=' in first_cookie:
            session_id = first_cookie.split('=', 1)[1].strip()

    # 2. Handle missing session
    if not session_id:
        body = "NO-SESSION"
    else:
        # 3. Attempt to read the shared file store
        filepath = f"/tmp/sess_{session_id}.txt"
        if os.path.exists(filepath):
            try:
                with open(filepath, "r") as f:
                    body = f.read()
            except IOError:
                body = "FILE-READ-ERROR"
        else:
            # If step 2 didn't write the file, or permissions are wrong
            body = "FILE-NOT-FOUND"

    # 4. Output valid HTTP CGI response
    print("Content-Type: text/plain")
    print(f"Content-Length: {len(body)}")
    print()  # Empty line separating headers from body
    print(body, end="")

if __name__ == '__main__':
    main()