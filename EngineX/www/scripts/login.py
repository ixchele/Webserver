#!/usr/bin/env python3
import os
import sys
import urllib.parse
import html
import hashlib
import time

SESSIONS = {}
USERS = {"admin": "admin123", "user1": "pass1234"}

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
    background: linear-gradient(135deg, var(--primary), var(--accent2));
    color: white; border: none; border-radius: 6px;
    font-weight: 600; cursor: pointer; transition: opacity 0.2s;
  }
  button:hover { opacity: 0.9; }
  .info-box {
    background: rgba(255,255,255,0.05); border-left: 4px solid var(--accent);
    padding: 12px; margin: 16px 0; border-radius: 4px; font-size: 0.85em; color: var(--text-muted);
  }
  code { font-family: "JetBrains Mono", monospace; color: var(--accent); }
  a { color: var(--primary-light); text-decoration: none; font-size: 0.9em; }
  a:hover { text-decoration: underline; }
  .error-text { color: var(--error); font-size: 0.9em; margin-bottom: 16px; }
</style>
"""

# ─── Helpers ──────────────────────────────────────────────────────────────────
def parse_cookies():
    cookies = {}
    raw = os.environ.get("HTTP_COOKIE", "")
    for part in raw.split(";"):
        part = part.strip()
        if "=" in part:
            k, v = part.split("=", 1)
            cookies[k.strip()] = v.strip()
    return cookies

def parse_post():
    if os.environ.get("REQUEST_METHOD", "GET").upper() != "POST":
        return {}
    try:
        length_env = os.environ.get("CONTENT_LENGTH")
        if length_env and length_env.isdigit():
            length = int(length_env)
            body = sys.stdin.buffer.read(length).decode("utf-8", errors="replace")
        else:
            body = sys.stdin.read()
        return dict(urllib.parse.parse_qsl(body))
    except Exception:
        return {}

def make_session_id(username):
    raw = f"{username}-{time.time()}-secret"
    return hashlib.sha256(raw.encode()).hexdigest()[:32]

def set_cookie(name, value, max_age=3600):
    return f"Set-Cookie: {name}={value}; Path=/; Max-Age={max_age}; HttpOnly"

def send_response(html_body, status="200 OK", extra_headers=None):
    body_bytes = html_body.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write(f"Status: {status}\r\n")
    if extra_headers:
        for h in extra_headers:
            sys.stdout.write(f"{h}\r\n")
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    sys.stdout.write(html_body)

# ─── Pages ────────────────────────────────────────────────────────────────────
def page_login(error="", extra_headers=None):
    script_name = os.environ.get("SCRIPT_NAME", "")
    error_html = f'<div class="error-text">{html.escape(error)}</div>' if error else ""
    
    html_body = f"""<!DOCTYPE html><html><head><title>Enginx Login</title>{ENGINX_THEME}</head>
<body>
<div class="box">
  <h2>🔐 Secure Access</h2>
  {error_html}
  <form method="POST" action="{script_name}">
    <input type="hidden" name="action" value="login">
    <label>Username</label>
    <input type="text" name="username" placeholder="admin" required autofocus>
    <label>Password</label>
    <input type="password" name="password" placeholder="••••••••" required>
    <button type="submit">Authenticate</button>
  </form>
  <p style="margin-top: 16px; font-size: 0.8em; color: var(--text-muted); text-align: center;">
    Test credentials: <code>admin / admin123</code>
  </p>
</div>
</body></html>"""
    send_response(html_body, extra_headers=extra_headers)

def page_dashboard(username, session_id, extra_headers=None):
    script_name = os.environ.get("SCRIPT_NAME", "")
    html_body = f"""<!DOCTYPE html><html><head><title>Dashboard</title>{ENGINX_THEME}</head>
<body>
<div class="box">
  <h2 style="color: var(--success);">✅ Welcome, {html.escape(username)}!</h2>
  <p style="color: var(--text-muted); font-size: 0.9em;">System connection established.</p>
  <div class="info-box">
    <b>Session ID:</b><br><code>{html.escape(session_id)}</code><br><br>
    <b>Access Level:</b><br><code>Administrator</code>
  </div>
  <form method="POST" action="{script_name}">
    <input type="hidden" name="action" value="logout">
    <button type="submit" style="background: rgba(239, 68, 68, 0.2); border: 1px solid var(--error); color: var(--error);">
      Terminate Session
    </button>
  </form>
</div>
</body></html>"""
    send_response(html_body, extra_headers=extra_headers)

def page_logged_out(extra_headers=None):
    script_name = os.environ.get("SCRIPT_NAME", "")
    html_body = f"""<!DOCTYPE html><html><head><title>Disconnected</title>{ENGINX_THEME}</head>
<body>
<div class="box" style="text-align: center;">
  <h2>👋 Disconnected</h2>
  <p style="color: var(--text-muted); margin-bottom: 24px;">Your secure session has been terminated.</p>
  <a href="{script_name}">← Return to Portal</a>
</div>
</body></html>"""
    send_response(html_body, extra_headers=extra_headers)

# ─── Main ─────────────────────────────────────────────────────────────────────
def main():
    cookies = parse_cookies()
    post = parse_post()
    query = dict(urllib.parse.parse_qsl(os.environ.get("QUERY_STRING", "")))
    action = post.get("action") or query.get("action", "")

    session_id = cookies.get("SESSIONID", "")
    session = SESSIONS.get(session_id)

    if action == "login":
        username = post.get("username", "").strip()
        password = post.get("password", "").strip()
        if username in USERS and USERS[username] == password:
            new_sid = make_session_id(username)
            SESSIONS[new_sid] = {"username": username, "created": time.time()}
            headers = [set_cookie("SESSIONID", new_sid, max_age=3600)]
            page_dashboard(username, new_sid, extra_headers=headers)
            return
        else:
            page_login(error="Invalid authentication credentials.")
            return

    elif action == "logout":
        headers = [set_cookie("SESSIONID", "", max_age=0)]
        if session_id in SESSIONS: del SESSIONS[session_id]
        page_logged_out(extra_headers=headers)
        return

    if session: page_dashboard(session["username"], session_id)
    else: page_login()

if __name__ == "__main__":
    main()