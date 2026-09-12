#!/usr/bin/env python3
import os
import json
import re
import secrets
from http.cookies import SimpleCookie
from urllib.parse import parse_qs
from html import escape

COOKIE_NAME = "THEME_SID"
SESSION_DIR = "/tmp/webserv_theme_sessions"
VALID_MODES = ("light", "dark", "hacker")

def ensure_dir():
    os.makedirs(SESSION_DIR, exist_ok=True)

def get_cookie_sid():
    raw = os.environ.get("HTTP_COOKIE", "")
    cookie = SimpleCookie()
    try:
        cookie.load(raw)
    except Exception:
        return None
    if COOKIE_NAME not in cookie:
        return None
    sid = cookie[COOKIE_NAME].value
    if not re.fullmatch(r"[0-9a-f]{32}", sid):
        return None
    return sid

def session_path(sid):
    return os.path.join(SESSION_DIR, sid + ".json")

def load_or_create_session():
    ensure_dir()
    sid = get_cookie_sid()
    is_new = False

    if sid:
        try:
            with open(session_path(sid), "r") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                raise ValueError()
            return sid, data, is_new
        except Exception:
            pass

    sid = secrets.token_hex(16)
    return sid, {"mode": "light", "visits": 0}, True

def save_session(sid, data):
    with open(session_path(sid), "w") as f:
        json.dump(data, f)

def get_params():
    method = os.environ.get("REQUEST_METHOD", "GET").upper()
    if method == "POST":
        try:
            length = int(os.environ.get("CONTENT_LENGTH", "0"))
        except ValueError:
            length = 0
        raw = os.sys.stdin.read(length)
    else:
        raw = os.environ.get("QUERY_STRING", "")
    return parse_qs(raw, keep_blank_values=True)

sid, session, is_new = load_or_create_session()
params = get_params()

requested_mode = params.get("mode", [""])[0]
if requested_mode in VALID_MODES:
    session["mode"] = requested_mode

session["visits"] = int(session.get("visits", 0)) + 1
mode = session.get("mode", "light")
save_session(sid, session)

styles = {
    "light": """
        body { background:#f4f4f4; color:#181818; }
        .card { background:white; border:1px solid #ccc; }
        button { background:#222; color:white; }
    """,
    "dark": """
        body { background:#111; color:#eee; }
        .card { background:#1d1d1d; border:1px solid #444; }
        button { background:#eee; color:#111; }
    """,
    "hacker": """
        body { background:#000; color:#00ff66; font-family:monospace; }
        .card { background:#020702; border:1px solid #00ff66;
                box-shadow:0 0 18px rgba(0,255,102,.35); }
        button { background:#000; color:#00ff66; border:1px solid #00ff66; }
    """
}

print("Content-Type: text/html; charset=utf-8")
if is_new:
    print("Set-Cookie: %s=%s; Path=/; HttpOnly; SameSite=Lax" % (COOKIE_NAME, sid))
print()

print("""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Session Theme Switcher</title>
<style>
body { transition:.2s; padding:60px; text-align:center; font-family:sans-serif; }
.card { max-width:650px; margin:auto; padding:35px; border-radius:14px; }
button { padding:12px 22px; margin:7px; cursor:pointer; font-size:18px; }
%s
</style>
</head>
<body>
<div class="card">
    <h1>Persistent Theme Session</h1>
    <p>Your selected mode survives refreshes because the CGI remembers it.</p>

    <form method="post">
        <button name="mode" value="light">LIGHT MODE</button>
        <button name="mode" value="dark">DARK MODE</button>
        <button name="mode" value="hacker">HACKER MODE</button>
    </form>

    <h2>Current mode: %s</h2>
    <p>Visits in this session: %d</p>
    <p style="font-family:monospace">Session ID: %s</p>
</div>
</body>
</html>""" % (styles[mode], escape(mode.upper()), int(session["visits"]), escape(sid)))
