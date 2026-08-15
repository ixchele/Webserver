#!/usr/bin/env python3
import os
import sys

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
    text-align: center;
  }
  h2 { font-weight: 600; margin-bottom: 8px; font-size: 1.5em; color: var(--text); }
  button {
    width: 100%; padding: 12px; font-size: 1.1em; font-family: inherit;
    background: linear-gradient(135deg, var(--primary), var(--accent2));
    color: white; border: none; border-radius: 6px;
    font-weight: 600; cursor: pointer; transition: opacity 0.2s, transform 0.1s;
  }
  button:hover { opacity: 0.9; }
  button:active { transform: scale(0.98); }
  .info-box {
    background: rgba(255,255,255,0.05); border-left: 4px solid var(--accent);
    padding: 12px; margin: 20px 0 0 0; border-radius: 4px; font-size: 0.85em; color: var(--text-muted); text-align: left;
  }
  code { font-family: "JetBrains Mono", monospace; color: var(--accent); }
</style>
"""

def parse_cookies():
    cookies = {}
    raw = os.environ.get("HTTP_COOKIE", "")
    for part in raw.split(";"):
        part = part.strip()
        if "=" in part:
            k, v = part.split("=", 1)
            cookies[k.strip()] = v.strip()
    return cookies

def send_response(html_body):
    body_bytes = html_body.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    sys.stdout.write(html_body)

cookies = parse_cookies()
method  = os.environ.get("REQUEST_METHOD", "GET").upper()
script_name = os.environ.get("SCRIPT_NAME", "")

if method == "POST":
    try: counter = int(cookies.get("VISIT_COUNT", "0"))
    except ValueError: counter = 0
    counter += 1

    sys.stdout.write("Status: 302 Found\r\n")
    sys.stdout.write(f"Location: {script_name}\r\n")
    sys.stdout.write(f"Set-Cookie: VISIT_COUNT={counter}; Path=/; Max-Age=3600\r\n")
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write("Content-Length: 0\r\n\r\n")

else:
    try: counter = int(cookies.get("VISIT_COUNT", "0"))
    except ValueError: counter = 0

    html_body = f"""<!DOCTYPE html>
<html>
<head>
  <title>Enginx Counter</title>
  {ENGINX_THEME}
</head>
<body>
<div class="box">
  <h2>⚡ Metrics Event</h2>
  <p style="color: var(--text-muted); font-size: 0.9em;">Session clicks registered:</p>
  
  <div style="font-size: 5em; font-weight: 800; margin: 20px 0; background: linear-gradient(135deg, var(--accent), var(--accent2)); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">
    {counter}
  </div>
  
  <form method="POST" action="{script_name}">
    <button type="submit">Execute Click</button>
  </form>
  
  <div class="info-box">
    State: <code>VISIT_COUNT = {counter}</code><br>
    Persistence: <code>Valid (Cookie)</code>
  </div>
</div>
</body>
</html>"""

    send_response(html_body)