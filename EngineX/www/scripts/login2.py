#!/usr/bin/env python3
import os
import sys
import urllib.parse
import html

# ─── UNIVERSAL ENGINX DESIGN THEME ───────────────────────────────────────────
ENGINX_THEME = """
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --primary: #6366f1; --primary-light: #818cf8;
    --accent: #06b6d4; --accent2: #8b5cf6;
    --bg: #030712; --bg-card: rgba(15, 23, 42, 0.6);
    --text: #f1f5f9; --text-muted: #94a3b8;
    --border: rgba(99, 102, 241, 0.15);
    --error: #ef4444; --success: #10b981;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: "Inter", system-ui, sans-serif; background-color: var(--bg); color: var(--text);
    display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 20px;
  }
  .box {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 12px; 
    padding: 32px; width: 100%; max-width: 400px; box-shadow: 0 8px 32px rgba(0,0,0,0.5); backdrop-filter: blur(10px);
  }
  h2 { font-weight: 600; margin-bottom: 20px; font-size: 1.5em; }
  label { font-size: 0.85em; color: var(--text-muted); margin-bottom: 6px; display: block; }
  input {
    width: 100%; padding: 12px; margin-bottom: 16px; background: rgba(255,255,255,0.05); 
    border: 1px solid var(--border); color: var(--text); border-radius: 6px; font-family: inherit; font-size: 1em;
  }
  input:focus { outline: none; border-color: var(--primary); }
  button {
    width: 100%; padding: 12px; font-size: 1em; font-family: inherit;
    background: linear-gradient(135deg, var(--primary), var(--accent2)); color: white; 
    border: none; border-radius: 6px; font-weight: 600; cursor: pointer; transition: opacity 0.2s;
  }
  button:hover { opacity: 0.9; }
  .msg-success { background: rgba(16, 185, 129, 0.1); border-left: 4px solid var(--success); padding: 12px; margin-bottom: 16px; border-radius: 4px; color: var(--success); font-size: 0.9em; }
  .msg-error { background: rgba(239, 68, 68, 0.1); border-left: 4px solid var(--error); padding: 12px; margin-bottom: 16px; border-radius: 4px; color: var(--error); font-size: 0.9em; }
</style>
"""

def send_response(html_body):
    body_bytes = html_body.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    sys.stdout.write(html_body)

def main():
    request_method = os.environ.get("REQUEST_METHOD", "GET")
    script_name = os.environ.get("SCRIPT_NAME", "")
    message_html = ""

    if request_method == "POST":
        content_length_env = os.environ.get("CONTENT_LENGTH")
        if content_length_env and content_length_env.isdigit():
            post_data = sys.stdin.read(int(content_length_env))
        else:
            post_data = sys.stdin.read()
            
        parsed_data = urllib.parse.parse_qs(post_data)
        username = parsed_data.get("username", [""])[0]
        password = parsed_data.get("password", [""])[0]
        
        if username == "admin" and password == "secret":
            message_html = "<div class='msg-success'><strong>Authentication Successful!</strong></div>"
        else:
            message_html = "<div class='msg-error'><strong>Invalid credentials.</strong></div>"

    html_body = f"""<!DOCTYPE html>
<html>
<head><title>Enginx Strict Login</title>{ENGINX_THEME}</head>
<body>
<div class="box">
  <h2>Strict CGI Login Test</h2>
  {message_html}
  <form method="POST" action="{script_name}">
      <label>Username</label>
      <input type="text" name="username" placeholder="admin" required>
      <label>Password</label>
      <input type="password" name="password" placeholder="secret" required>
      <button type="submit">Log In</button>
  </form>
</div>
</body>
</html>
"""
    send_response(html_body)

if __name__ == "__main__":
    main()