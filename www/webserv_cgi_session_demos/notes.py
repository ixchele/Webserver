#!/usr/bin/env python3
import os
import json
import re
import secrets
from http.cookies import SimpleCookie
from urllib.parse import parse_qs
from html import escape

COOKIE_NAME = "NOTES_SID"
SESSION_DIR = "/tmp/webserv_notes_sessions"

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
    if sid:
        try:
            with open(session_path(sid), "r") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                raise ValueError()
            return sid, data, False
        except Exception:
            pass

    sid = secrets.token_hex(16)
    return sid, {"notes": []}, True

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

notes = session.get("notes", [])
if not isinstance(notes, list):
    notes = []

if action == "add":
    text = params.get("note", [""])[0].strip()
    if text:
        notes.append(text[:200])
elif action == "clear":
    notes = []

session["notes"] = notes
save_session(sid, session)

print("Content-Type: text/html; charset=utf-8")
if is_new:
    print("Set-Cookie: %s=%s; Path=/; HttpOnly; SameSite=Lax" % (COOKIE_NAME, sid))
print()

items = ""
if notes:
    for i, note in enumerate(notes, 1):
        items += "<li><b>%d.</b> %s</li>" % (i, escape(note))
else:
    items = "<li><i>No notes yet.</i></li>"

print("""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Session Notes</title>
<style>
body { font-family:sans-serif; background:#eef2f6; padding:50px; }
.box { max-width:720px; margin:auto; background:white; padding:30px;
       border-radius:14px; box-shadow:0 8px 30px rgba(0,0,0,.08); }
input { width:70%%; padding:12px; font-size:17px; }
button { padding:12px 18px; font-size:16px; cursor:pointer; }
li { margin:12px 0; }
.sid { font-family:monospace; color:#777; margin-top:30px; }
</style>
</head>
<body>
<div class="box">
<h1>Private Session Notes</h1>
<p>Each browser session gets its own server-side note list.</p>

<form method="post">
    <input name="note" maxlength="200" placeholder="Write a note...">
    <button name="action" value="add">ADD</button>
</form>

<ul>
%s
</ul>

<form method="post">
    <button name="action" value="clear">CLEAR ALL</button>
</form>

<div class="sid">Session ID: %s</div>
</div>
</body>
</html>""" % (items, escape(sid)))
