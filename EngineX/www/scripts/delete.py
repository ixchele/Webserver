#!/usr/bin/env python3
# test_delete.py — Demonstrates handling an HTTP DELETE request in CGI

import os
import sys
import urllib.parse
import json

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
    --error: #ef4444; --error-bg: rgba(239, 68, 68, 0.1);
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
    border-radius: 12px; padding: 32px; width: 100%; max-width: 500px;
    box-shadow: 0 8px 32px rgba(0,0,0,0.5); backdrop-filter: blur(10px);
  }
  h2 { font-weight: 600; margin-bottom: 8px; font-size: 1.5em; color: var(--text); }
  p { color: var(--text-muted); font-size: 0.9em; margin-bottom: 24px; line-height: 1.5; }
  ul { list-style: none; margin-bottom: 24px; }
  li {
    display: flex; justify-content: space-between; align-items: center;
    padding: 12px 16px; background: rgba(255,255,255,0.03);
    border: 1px solid var(--border); border-radius: 8px; margin-bottom: 8px;
    transition: all 0.3s ease;
  }
  button {
    background: var(--error-bg); color: var(--error);
    border: 1px solid rgba(239, 68, 68, 0.3); padding: 6px 12px;
    border-radius: 6px; cursor: pointer; font-size: 0.85em; font-weight: 500;
    transition: all 0.2s;
  }
  button:hover { background: var(--error); color: white; }
  .log-box h3 { font-size: 0.9em; color: var(--text-muted); margin-bottom: 8px; text-transform: uppercase; letter-spacing: 1px; }
  #log {
    padding: 16px; background: rgba(0,0,0,0.5); color: #4af626;
    font-family: "JetBrains Mono", monospace; font-size: 0.85em;
    border-radius: 8px; border: 1px solid rgba(255,255,255,0.05);
    white-space: pre-wrap; min-height: 100px; overflow-y: auto;
  }
</style>
"""

def send_response(body_str, content_type="text/html; charset=utf-8", status="200 OK"):
    body_bytes = body_str.encode('utf-8')
    content_length = len(body_bytes)
    sys.stdout.write(f"Status: {status}\r\n")
    sys.stdout.write(f"Content-Type: {content_type}\r\n")
    sys.stdout.write(f"Content-Length: {content_length}\r\n\r\n")
    sys.stdout.write(body_str)

def main():
    method = os.environ.get("REQUEST_METHOD", "GET").upper()
    query = dict(urllib.parse.parse_qsl(os.environ.get("QUERY_STRING", "")))
    script_name = os.environ.get("SCRIPT_NAME", "/test_delete.py")

    if method == "DELETE":
        item_id = query.get("id", "").strip()
        if not item_id:
            error_resp = json.dumps({"status": "error", "message": "Missing item 'id' in query string."})
            send_response(error_resp, content_type="application/json", status="400 Bad Request")
            return
        
        success_resp = json.dumps({"status": "success", "message": f"Item {item_id} successfully deleted."})
        send_response(success_resp, content_type="application/json", status="200 OK")
        return

    else:
        html_body = f"""<!DOCTYPE html>
<html>
<head>
  <title>Enginx DELETE Test</title>
  {ENGINX_THEME}
</head>
<body>
<div class="box">
  <h2>🗑️ Resource Management</h2>
  <p>Test the HTTP <b>DELETE</b> method using the Fetch API. Forms normally restrict you to GET and POST.</p>
  
  <ul id="itemList">
    <li id="item-1"><span>📄 Configuration.json</span> <button onclick="deleteItem('1')">Delete</button></li>
    <li id="item-2"><span>🖼️ Server_Logo.png</span> <button onclick="deleteItem('2')">Delete</button></li>
    <li id="item-3"><span>📊 Access_Log_Old.log</span> <button onclick="deleteItem('3')">Delete</button></li>
  </ul>

  <div class="log-box">
    <h3>Network Terminal</h3>
    <div id="log">Waiting for action...</div>
  </div>
</div>

<script>
  function deleteItem(id) {{
    const logEl = document.getElementById('log');
    logEl.textContent = "> Sending DELETE request for item " + id + "...\\n";

    fetch(`{script_name}?id=${{id}}`, {{ method: 'DELETE' }})
    .then(response => {{
      logEl.textContent += "> HTTP Status: " + response.status + "\\n";
      return response.json();
    }})
    .then(data => {{
      logEl.textContent += "> Response Body: " + JSON.stringify(data, null, 2);
      if (data.status === 'success') {{
         const li = document.getElementById('item-' + id);
         if (li) {{
             li.style.transform = 'scale(0.95)';
             li.style.opacity = '0';
             setTimeout(() => li.remove(), 300);
         }}
      }}
    }})
    .catch(err => {{ logEl.textContent += "\\n> Error: " + err; }});
  }}
</script>
</body>
</html>"""
        send_response(html_body)

if __name__ == "__main__":
    main()