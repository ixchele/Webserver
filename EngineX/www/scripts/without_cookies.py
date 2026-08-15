#!/usr/bin/env python3
import os
import sys
import urllib.parse

# ─── UNIVERSAL ENGINX DESIGN THEME ───────────────────────────────────────────
ENGINX_THEME = """
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --primary: #6366f1; --accent: #06b6d4; --accent2: #8b5cf6;
    --bg: #030712; --bg-card: rgba(15, 23, 42, 0.6);
    --text: #f1f5f9; --text-muted: #94a3b8;
    --border: rgba(99, 102, 241, 0.15);
    --warn: #f59e0b; --warn-bg: rgba(245, 158, 11, 0.1);
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
    box-shadow: 0 8px 32px rgba(0,0,0,0.5); backdrop-filter: blur(10px); text-align: center;
  }
  h2 { font-weight: 600; margin-bottom: 8px; font-size: 1.5em; color: var(--text); }
  button {
    width: 100%; padding: 12px; font-size: 1.1em; font-family: inherit;
    background: transparent; color: var(--text); 
    border: 1px solid var(--primary); border-radius: 6px;
    font-weight: 600; cursor: pointer; transition: all 0.2s;
  }
  button:hover { background: var(--primary); color: white; }
  .warn-box {
    background: var(--warn-bg); border-left: 4px solid var(--warn);
    padding: 16px; margin-top: 24px; border-radius: 4px; font-size: 0.85em; 
    color: var(--text); text-align: left; line-height: 1.6;
  }
  code { font-family: "JetBrains Mono", monospace; color: var(--warn); }
  .url {
    background: rgba(0,0,0,0.4); padding: 8px; border-radius: 4px;
    margin: 8px 0; word-break: break-all; color: var(--text-muted); font-family: "JetBrains Mono", monospace;
  }
</style>
"""

def send_response(html_body):
    body_bytes = html_body.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    sys.stdout.write(html_body)

method = os.environ.get("REQUEST_METHOD", "GET").upper()
query  = dict(urllib.parse.parse_qsl(os.environ.get("QUERY_STRING", "")))
script_name = os.environ.get("SCRIPT_NAME", "")

if method == "POST":
    try: counter = int(query.get("count", "0"))
    except ValueError: counter = 0
    counter += 1

    sys.stdout.write("Status: 302 Found\r\n")
    sys.stdout.write(f"Location: {script_name}?count={counter}\r\n")
    sys.stdout.write("Content-Type: text/html; charset=utf-8\r\n")
    sys.stdout.write("Content-Length: 0\r\n\r\n")

else:
    try: counter = int(query.get("count", "0"))
    except ValueError: counter = 0

    html_body = f"""<!DOCTYPE html>
<html>
<head><title>Enginx - Without Cookies</title>{ENGINX_THEME}</head>
<body>
<div class="box">
  <h2>❌ Volatile Counter</h2>
  <p style="color: var(--text-muted); font-size: 0.9em;">Tracking via URL string only.</p>
  
  <div style="font-size: 5em; font-weight: 800; margin: 20px 0; color: var(--warn);">
    {counter}
  </div>
  
  <form method="POST" action="{script_name}?count={counter}">
    <button type="submit">Increment Value</button>
  </form>
  
  <div class="warn-box">
    <b>⚠ Security & Persistence Warning</b><br>
    State is maintained entirely in the URL:<br>
    <div class="url">{script_name}?count={counter}</div>
    • Close the tab → resets to 0.<br>
    • Editable by user: <code>?count=9999</code>
  </div>
</div>
</body></html>"""
    send_response(html_body)