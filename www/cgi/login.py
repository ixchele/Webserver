#!/usr/bin/env python3
import os
import sys
import urllib.parse
from http import cookies

def main():
    # 1. Parse incoming cookies to see if we already know the user
    cookie_header = os.environ.get("HTTP_COOKIE", "")
    cookie = cookies.SimpleCookie()
    cookie.load(cookie_header)
    
    saved_user = cookie.get("session_user")
    saved_user_val = saved_user.value if saved_user else None

    # 2. Parse POST data if a form was just submitted
    method = os.environ.get("REQUEST_METHOD", "GET")
    username = None
    password = None
    
    if method == "POST":
        try:
            content_length = int(os.environ.get("CONTENT_LENGTH", "0"))
            if content_length > 0:
                body = sys.stdin.read(content_length)
                # Parse URL-encoded form data (username=xxx&password=yyy)
                parsed_body = urllib.parse.parse_qs(body)
                username = parsed_body.get("username", [None])[0]
                password = parsed_body.get("password", [None])[0]
        except Exception:
            pass

    # 3. Handle Header Generation
    sys.stdout.write("Content-Type: text/html\r\n")
    
    # If a new user just logged in via POST, set the cookie!
    if username:
        # URL encode the username to safely put it in a cookie header
        safe_user = urllib.parse.quote(username)
        sys.stdout.write(f"Set-Cookie: session_user={safe_user}; Max-Age=3600; Path=/\r\n")
        current_user = username
    else:
        # Otherwise, fall back to the cookie value if it exists
        current_user = urllib.parse.unquote(saved_user_val) if saved_user_val else None

    # End of headers
    sys.stdout.write("\r\n")

    # 4. Generate the Response HTML
    html_content = ""
    if current_user:
        html_content = f"""
        <div class="card success">
            <h1>Welcome back, <span class="highlight">{current_user}</span>!</h1>
            <p>Your webserv successfully read the <code>HTTP_COOKIE</code> environment variable.</p>
            <p>The cookie <code>session_user</code> is saved in your browser.</p>
            {"<p class='alert'>⚠️ Just received your credentials via POST payload!</p>" if username else ""}
            <a href="/login.html" class="btn">Back to login form</a>
        </div>
        """
    else:
        html_content = """
        <div class="card error">
            <h1>No Session Found</h1>
            <p>We couldn't detect a cookie, and no login data was posted.</p>
            <a href="/login.html" class="btn">Go to Login Page</a>
        </div>
        """

    full_page = f"""<!DOCTYPE html>
<html>
<head>
    <title>CGI Cookie Session Result</title>
    <style>
        body {{ font-family: Arial, sans-serif; background: #f4f4f9; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }}
        .card {{ background: white; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); text-align: center; max-width: 400px; }}
        .highlight {{ color: #007bff; font-weight: bold; }}
        code {{ background: #eee; padding: 2px 6px; border-radius: 4px; font-family: monospace; }}
        .btn {{ display: inline-block; margin-top: 20px; padding: 10px 20px; background: #007bff; color: white; text-decoration: none; border-radius: 4px; }}
        .btn:hover {{ background: #0056b3; }}
        .alert {{ color: #d9534f; font-weight: bold; font-size: 14px; margin-top: 15px; }}
    </style>
</head>
<body>
    {html_content}
</body>
</html>
"""
    sys.stdout.write(full_page)

if __name__ == "__main__":
    main()