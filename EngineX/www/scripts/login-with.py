#!/usr/bin/env python3
import os
import sys
import urllib.parse
import html
import hashlib
import time
import json

SESSION_FILE = "/tmp/login_sessions.json"
USERS = { "admin": "admin123", "user1": "pass1234" }

# ─── UNIVERSAL ENGINX DESIGN THEME ───────────────────────────────────────────
ENGINX_THEME = """
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --primary: #6366f1; --accent: #06b6d4; --accent2: #8b5cf6;
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
  .info-box { background: rgba(255,255,255,0.05); border-left: 4px solid var(--accent); padding: 12px; margin: 16px 0; border-radius: 4px; font-size: 0.85em; color: var(--text-muted); }
  code { font-family: "JetBrains Mono", monospace; color: var(--accent); }
  .error-text { color: var(--error); font-size: 0.9em; margin-bottom: 16px; }
</style>
"""

# ── Session helpers ──────────────────────────────────────────────
def load_sessions():
    try:
        if os.path.exists(SESSION_FILE):
            with open(SESSION_FILE, "r") as f:
                return json.load(f)
    except Exception: pass
    return {}

def save_sessions(sessions):
    try:
        with open(SESSION_FILE, "w") as f:
            json.dump(sessions, f)
    except Exception: pass

def make_session_id(username):
    raw = f"{username}-{time.time()}-secret"
    return hashlib.sha256(raw.encode()).hexdigest()[:32]

# ── HTTP helpers ─────────────────────────────────────────────────
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

def send_response(html_body, status="200 OK", extra_headers=None):
    body_bytes = html_body.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write(f"Status: {status}\r\n")
    if extra_headers:
        for h in extra_headers:
            sys.stdout.write(f"{h}\r\n")
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    if html_body:
        sys.stdout.write(html_body)

# ── Main flow ────────────────────────────────────────────────────
def main():
    sessions = load_sessions()
    cookies = parse_cookies()
    post = parse_post()
    
    script_path = os.environ.get("SCRIPT_NAME", "")
    
    session_id = cookies.get("SESSIONID", "")
    session = sessions.get(session_id)

    # Clean expired sessions (older than 1 hour)
    now = time.time()
    to_delete = [sid for sid, s in sessions.items() if now - s.get("created", 0) > 3600]
    for sid in to_delete:
        del sessions[sid]
    if to_delete: save_sessions(sessions)

    # 1. Handle POST Actions
    action = post.get("action", "")
    
    if action == "login":
        username = post.get("username", "")
        password = post.get("password", "")
        
        if username in USERS and USERS[username] == password:
            new_sid = make_session_id(username)
            sessions[new_sid] = {"username": username, "created": time.time()}
            save_sessions(sessions)
            
            cookie_header = f"Set-Cookie: SESSIONID={new_sid}; Path=/; Max-Age=3600; HttpOnly"
            send_response("", status="302 Found", extra_headers=[f"Location: {script_path}", cookie_header])
            return
        else:
            send_response("", status="302 Found", extra_headers=[f"Location: {script_path}?error=1"])
            return

    elif action == "logout":
        if session_id in sessions:
            del sessions[session_id]
            save_sessions(sessions)
        
        cookie_header = "Set-Cookie: SESSIONID=; Path=/; Max-Age=0; HttpOnly"
        send_response("", status="302 Found", extra_headers=[f"Location: {script_path}", cookie_header])
        return

    # 2. Handle GET / Default Display
    query = dict(urllib.parse.parse_qsl(os.environ.get("QUERY_STRING", "")))
    error = query.get("error", "")

    if session:
        username = session["username"]
        html_body = f"""<!DOCTYPE html><html><head><title>Enginx - File Sessions</title>{ENGINX_THEME}</head>
<body><div class="box">
  <h2 style="color: var(--success);">✅ Welcome, {html.escape(username)}!</h2>
  <div class="info-box">
    <b>Session Status:</b> Active<br>
    <b>Storage:</b> <code>/tmp/login_sessions.json</code><br>
    <b>ID:</b> <code>{html.escape(session_id[:12])}...</code>
  </div>
  <form method="POST" action="{script_path}">
    <input type="hidden" name="action" value="logout">
    <button type="submit" style="background: rgba(239, 68, 68, 0.2); border: 1px solid var(--error); color: var(--error);">Logout</button>
  </form>
</div></body></html>"""
    else:
        err_msg = '<div class="error-text">❌ Invalid Login Credentials</div>' if error else ""
        html_body = f"""<!DOCTYPE html><html><head><title>Enginx - File Sessions</title>{ENGINX_THEME}</head>
<body><div class="box">
  <h2>File-Backed Sessions</h2>
  {err_msg}
  <form method="POST" action="{script_path}">
      <input type="hidden" name="action" value="login">
      <label>Username</label>
      <input type="text" name="username" placeholder="admin" required autofocus>
      <label>Password</label>
      <input type="password" name="password" placeholder="admin123" required>
      <button type="submit">Login</button>
  </form>
</div></body></html>"""
    
    send_response(html_body)

if __name__ == "__main__":
    main()