#!/usr/bin/env python3
import os
import sys
import urllib.parse
import html

USERS = { "admin": "admin123", "user1": "pass1234" }

# ─── UNIVERSAL ENGINX DESIGN THEME ───────────────────────────────────────────
ENGINX_THEME = """
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --primary: #6366f1; --primary-light: #818cf8;
    --bg: #030712; --bg-card: rgba(15, 23, 42, 0.6);
    --text: #f1f5f9; --text-muted: #94a3b8;
    --border: rgba(99, 102, 241, 0.15);
    --error: #ef4444; --warn: #f59e0b; --warn-bg: rgba(245, 158, 11, 0.1);
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: "Inter", system-ui, sans-serif;
    background-color: var(--bg); color: var(--text);
    display: flex; justify-content: center; align-items: center;
    min-height: 100vh; padding: 20px;
  }
  .box {
    background: var(--bg-card); border: 1px solid var(--border);
    border-radius: 12px; padding: 32px; width: 100%; max-width: 400px;
    box-shadow: 0 8px 32px rgba(0,0,0,0.5); backdrop-filter: blur(10px);
  }
  h2 { font-weight: 600; margin-bottom: 20px; font-size: 1.5em; }
  label { font-size: 0.85em; color: var(--text-muted); margin-bottom: 6px; display: block; }
  input {
    width: 100%; padding: 12px; margin-bottom: 16px;
    background: rgba(255,255,255,0.05); border: 1px solid var(--border);
    color: var(--text); border-radius: 6px; font-family: inherit; font-size: 1em;
  }
  input:focus { outline: none; border-color: var(--primary); }
  button {
    width: 100%; padding: 12px; font-size: 1em; font-family: inherit;
    background: var(--primary); color: white; border: none; border-radius: 6px;
    font-weight: 600; cursor: pointer; transition: opacity 0.2s;
  }
  button:hover { opacity: 0.9; }
  .warn-box {
    background: var(--warn-bg); border-left: 4px solid var(--warn);
    padding: 16px; margin: 16px 0; border-radius: 4px; font-size: 0.85em; 
    color: var(--text); line-height: 1.6;
  }
  code { font-family: "JetBrains Mono", monospace; color: var(--warn); }
  .url { background: rgba(0,0,0,0.4); padding: 8px; border-radius: 4px; margin: 8px 0; font-family: "JetBrains Mono", monospace; color: var(--text-muted); }
  a { color: var(--primary-light); text-decoration: none; font-size: 0.9em; display: inline-block; margin-top: 10px; }
  a:hover { text-decoration: underline; }
  .error-text { color: var(--error); font-size: 0.9em; margin-bottom: 16px; }
</style>
"""

def parse_post():
    if os.environ.get("REQUEST_METHOD", "GET").upper() != "POST": return {}
    try:
        length_env = os.environ.get("CONTENT_LENGTH")
        if length_env and length_env.isdigit():
            length = int(length_env)
            body = sys.stdin.buffer.read(length).decode("utf-8", errors="replace")
        else:
            body = sys.stdin.read()
        return dict(urllib.parse.parse_qsl(body))
    except Exception: return {}

def send_response(html_body):
    body_bytes = html_body.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    sys.stdout.write(html_body)

post   = parse_post()
method = os.environ.get("REQUEST_METHOD", "GET").upper()
query  = dict(urllib.parse.parse_qsl(os.environ.get("QUERY_STRING", "")))
script_name = os.environ.get("SCRIPT_NAME", "")
http_host = os.environ.get("HTTP_HOST", "localhost")

if method == "POST":
    action   = post.get("action", "")
    username = post.get("username", "").strip()
    password = post.get("password", "").strip()

    if action == "login":
        if username in USERS and USERS[username] == password:
            sys.stdout.write("Status: 302 Found\r\n")
            sys.stdout.write(f"Location: {script_name}?logged_in=1\r\n")
            sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
            sys.stdout.write("Content-Length: 0\r\n\r\n")
        else:
            sys.stdout.write("Status: 302 Found\r\n")
            sys.stdout.write(f"Location: {script_name}?error=1\r\n")
            sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
            sys.stdout.write("Content-Length: 0\r\n\r\n")

else:
    logged_in = query.get("logged_in", "")
    error     = query.get("error", "")

    if logged_in:
        html_body = f"""<!DOCTYPE html>
<html>
<head><title>Enginx - Faux Dashboard</title>{ENGINX_THEME}</head>
<body>
<div class="box">
  <h2>⚠ Bypass Detected</h2>
  <div class="warn-box">
    <b>No secure session found.</b><br>
    The redirect worked, but the server only identifies you via the URL string:
    <div class="url">http://{http_host}{script_name}?logged_in=1</div>
    • Anyone can type this URL to bypass authentication.<br>
    • Removing <code>?logged_in=1</code> invalidates the session.
  </div>
  <a href="{script_name}">← Return to Secure Login</a>
</div>
</body></html>"""
        send_response(html_body)

    else:
        error_html = f'<div class="error-text">❌ {html.escape("Invalid credentials.")}</div>' if error else ""
        html_body = f"""<!DOCTYPE html>
<html>
<head><title>Enginx - Stateless Login</title>{ENGINX_THEME}</head>
<body>
<div class="box">
  <h2>❌ Stateless Login</h2>
  <p style="color: var(--text-muted); font-size: 0.85em; margin-bottom: 20px;">
    Testing redirect-only logic. Cookies are disabled.
  </p>
  {error_html}
  <form method="POST" action="{script_name}">
    <input type="hidden" name="action" value="login">
    <label>Username</label>
    <input type="text" name="username" placeholder="admin" required autofocus>
    <label>Password</label>
    <input type="password" name="password" placeholder="••••••••" required>
    <button type="submit" style="background: var(--warn); color: #000;">Insecure Login</button>
  </form>
</div>
</body></html>"""
        send_response(html_body)