#!/usr/bin/env python3
import os
import json
import re
import secrets
from http.cookies import SimpleCookie
from urllib.parse import parse_qs
from html import escape

COOKIE_NAME = "CLICKER_SID"
SESSION_DIR = "/tmp/webserv_clicker_sessions"

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
    data = {"clicks": 0}
    is_new = True
    return sid, data, is_new

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
action = params.get("action", [""])[0]

if action == "click":
    session["clicks"] = int(session.get("clicks", 0)) + 1
elif action == "reset":
    session["clicks"] = 0

save_session(sid, session)

print("Content-Type: text/html; charset=utf-8")
if is_new:
    print("Set-Cookie: %s=%s; Path=/; HttpOnly; SameSite=Lax" % (COOKIE_NAME, sid))
print()

clicks = int(session.get("clicks", 0))

print("""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Session Clicker</title>
<style>
body { font-family: sans-serif; text-align: center; padding-top: 60px; }
.counter { font-size: 72px; margin: 25px; }
button { font-size: 22px; padding: 14px 28px; margin: 8px; cursor: pointer; }
.sid { margin-top: 35px; font-family: monospace; color: #666; }
</style>
</head>
<body>
<h1>CGI Session Clicker</h1>
<p>This counter is stored server-side in your CGI session.</p>
<div class="counter">%d</div>

<form method="post">
    <button name="action" value="click">CLICK ME</button>
    <button name="action" value="reset">RESET</button>
</form>

<div class="sid">
Session cookie: %s<br>
Session ID: %s
</div>
</body>
</html>""" % (clicks, escape(COOKIE_NAME), escape(sid)))
