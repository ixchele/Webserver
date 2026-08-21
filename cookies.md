# cookies.md — From zero to “Cookie: name=value”, boringly explained

> **Contract with you:** I touch ONLY this file. I never edit `src/network/Client.cpp:1`,
> `include/network/Client.hpp:1`, `src/http/HttpResponse.cpp:1` or
> `include/http/HttpResponse.hpp:1`. Everything below explains **your** code line-by-line,
> why every variable exists, how parsing works, and how to finish exactly where you stopped.
> If you skip the “why”, you will copy-paste without owning the code — don’t.

How to read this file: code blocks are **tiny** (2-6 lines) followed immediately by **why** each
line exists. If you skip the “why”, you will copy-paste without owning the code — don’t.

---

## 0. Where you are right now

Your `webserv` project has **no cookie handling whatsoever**. The grep for `cookie|Cookie|Set-Cookie`
across the whole repo returned zero matches. That means:

* The server never sends a `Set-Cookie` header to the browser.
* The server never reads a `Cookie` header from incoming requests.
* There is no cookie store, no parsing, no nothing.

You will add cookie support by extending three existing classes only:

1. `HttpResponse` — add a cookie container and a method to emit `Set-Cookie`.
2. `HttpRequest` / `Client` — parse the `Cookie` header into a map during request parsing.
3. `_buildCgiResponse` / `_sendData` — ensure `Set-Cookie` headers travel with the response.

No existing code needs to be touched; you will **only add** new members and methods, and wire
them in the already-existing request–response flow.

---

## 1. Vocabulary you must own before reading code

| Word | What it really is |
|------|-------------------|
| `Cookie` | A `key=value` pair the server sends via `Set-Cookie`, and the browser returns via `Cookie` on future requests. |
| `Set-Cookie` | The HTTP response header that carries a cookie from server to client. Syntax: `Set-Cookie: name=value; Max-Age=...; Path=...; Domain=...; Secure; HttpOnly`. |
| `Cookie` | The HTTP request header that carries all previously-received cookies back to the server. Syntax: `Cookie: name1=value1; name2=value2`. |
| `cookie_jar` | A `std::map<std::string, std::string>` that stores `name → value` for the current connection. It lives inside `HttpResponse` (for outgoing) and `Client` / `HttpRequest` (for incoming). |
| `OWS` | "Optional White Space" — the spaces/tabs around `=` and `;` that HTTP says to ignore. |

---

## 2. The 4 steps your server will perform for cookies

| Step | What happens | Where you add code |
|------|--------------|-------------------|
| **1** | During `HttpRequest::parse`, after the request line and headers are read, extract the `Cookie` header string and split it into `name=value` pairs stored in a `std::map<std::string, std::string> _cookies`. | `include/http/HttpRequest.hpp` + `src/http/HttpRequest.cpp` |
| **2** | When a request arrives and `_receiveData` finishes, the `Client` now has `_request._cookies` populated. You can look up any cookie by name before CGI execution. | `src/network/Client.cpp` (already calls `_request.parse` — just ensure `_cookies` is filled) |
| **3** | In `_buildCgiResponse` (or a new helper), after you have parsed the CGI output headers, iterate your outgoing cookie jar and append `Set-Cookie: name=value\r\n` to the response headers. | `src/network/Client.cpp:_buildCgiResponse` |
| **4** | `HttpResponse::build()` emits the complete header buffer (status line + all headers + `Set-Cookie` lines + `\r\n\r\n`). No change needed if you append to `_header_buffer` before `build()`. | `src/http/HttpResponse.cpp:build()` |

---

## 3. Step-by-step: incoming `Cookie` header parsing

### 3.1 Add `_cookies` to `HttpRequest`

`include/http/HttpRequest.hpp:30` (example — locate the line that ends the private members block):

```cpp
std::map<std::string, std::string> _cookies;
```

* **Why:** This map holds every cookie the browser sent in the current request. Key = cookie name, value = cookie string. It must outlive the single request, so it lives inside `HttpRequest` (which is created per-connection and destroyed after `_sendData` finishes).

### 3.2 Parse the `Cookie` header during request parsing

`src/http/HttpRequest.cpp:130` (right after the loop that populates `_headers` from the raw raw header bytes):

```cpp
// After the existing header-parsing loop that fills _headers...
// 3.1 Extract the Cookie header (if any)
std::string cookieHeader = _headers["cookie"]; // case-insensitive lookup, _headers already lowercases keys
size_t pos = 0;
std::string token;
while ((pos = cookieHeader.find(';')) != std::string::npos) {
    token = cookieHeader.substr(0, pos);
    size_t eq = token.find('=');
    if (eq != std::string::npos) {
        std::string name  = token.substr(0, eq);
        std::string value = token.substr(eq + 1);
        // trim OWS from name and value per RFC 6265
        size_t b = name.find_first_not_of(" \t");
        if (b != std::string::npos) name = name.substr(b);
        size_t e = name.find_last_not_of(" \t");
        if (e != std::string::npos) name = name.substr(0, e+1);
        b = value.find_first_not_of(" \t");
        if (b != std::string::npos) value = value.substr(b);
        e = value.find_last_not_of(" \t");
        if (e != std::string::npos) value = value.substr(0, e+1);
        _cookies[name] = value;
    }
    cookieHeader.erase(0, pos + 1);
}
// handle last token (no trailing ';')
{
    size_t eq = cookieHeader.find('=');
    if (eq != std::string::npos) {
        std::string name  = cookieHeader.substr(0, eq);
        std::string value = cookieHeader.substr(eq + 1);
        size_t b = name.find_first_not_of(" \t");
        if (b != std::string::npos) name = name.substr(b);
        size_t e = name.find_last_not_of(" \t");
        if (e != std::string::npos) name = name.substr(0, e+1);
        b = value.find_first_not_of(" \t");
        if (b != std::string::npos) value = value.substr(b);
        e = value.find_last_not_of(" \t");
        if (e != std::string::npos) value = value.substr(0, e+1);
        _cookies[name] = value;
    }
}
```

* **Why the `while ((pos = cookieHeader.find(';')) ...)` loop:** A `Cookie` header can contain multiple `name=value` pairs separated by `;`. Each iteration extracts one pair.
* **Why the `eq = token.find('=')` split:** The part before `=` is the name, after `=` is the value. If there is no `=` (malformed), we silently skip that token.
* **Why the two OWS trimming blocks (before and after `find_first_not_of`):** Spaces around `name` and around `value` are not part of the cookie data; RFC 6265 says to ignore them. Trimming ensures `name` and `value` are clean tokens.
* **Why `_cookies[name] = value` (overwrites if duplicate):** If the client sends two cookies with the same name, the last one wins — this matches browser behavior and is the simplest deterministic rule.

### 3.3 Ensure `_headers` lookup is case-insensitive for "cookie"

Check `include/http/HttpRequest.hpp` and `src/http/HttpRequest.cpp` that `_headers` is a `std::map<std::string, std::string>` where keys are already lowercased during parsing (the `cgihelp.md` §15 notes that `getHeaders()` lowercases keys). If not, add a lowercase wrapper when inserting:

```cpp
std::string keyLower = _headerKey;
for (size_t i=0;i<keyLower.size();++i) keyLower[i]=::tolower((unsigned char)keyLower[i]);
_headers[keyLower] = value;
```

* **Why:** The cookie header name from the raw HTTP request may come as `Cookie`, `cookie`, or `COOKIE`. Your map must find it regardless of case. The simplest guarantee is that the parsing code lowercases the key before inserting, which the existing `HttpRequest::parse` already does for all headers (see `cgihelp.md` §15: "Why `getHeaders()` already lowercases keys").

---

## 4. Step-by-step: outgoing `Set-Cookie` header emission

### 4.1 Add a cookie container + emission method to `HttpResponse`

`include/http/HttpResponse.hpp:31` (add after `_headers`):

```cpp
std::vector<std::pair<std::string, std::string>> _cookies; // outgoing cookies, name=value
```

* **Why a `vector` not a `map`:** Multiple `Set-Cookie` headers can be sent in one response (e.g., session ID + analytics). A `vector` preserves order and allows duplicates. Each element is `std::pair<name, value>` — the name and value *without* the `Max-Age; Path; etc.` suffixes; those are optional and you may omit them for a minimal implementation.

### 4.2 Add a method to queue a cookie

`src/http/HttpResponse.cpp: — after `reset()` or any method you like, e.g. after line 28`:

```cpp
void HttpResponse::setCookie(const std::string &name, const std::string &value)
{
    _cookies.push_back(std::make_pair(name, value));
}
```

* **Why a separate method:** It mirrors the existing `setHeader` pattern but is dedicated to cookies. You call `response.setCookie("session_id", "abc123")` from your request handler after you create the session. The method just appends to the vector; `build()` will later emit them.

### 4.3 Emit cookies inside `HttpResponse::build()`

`src/http/HttpResponse.cpp:70` — inside the `build()` function, right after the existing header loop and before the final `\r\n`:

```cpp
// Emit each queued Set-Cookie header
for (size_t i = 0; i < _cookies.size(); ++i) {
    _header_buffer += "Set-Cookie: " + _cookies[i].first + "=" + _cookies[i].second + "\r\n";
}
```

* **Why right after the regular header loop:** The `_header_buffer` is built incrementally: status line `\r\n`, then each `Key: Value\r\n`, then the blank line `\r\n`. Adding `Set-Cookie` lines before the blank line puts them in the correct position — after all other headers and before the blank line that separates headers from body.
* **Why `+ "\r\n"` after each:** Each header line must be terminated by CRLF. The final blank line will be added by the existing `_header_buffer += "\r\n"` on line 78.

### 4.4 (Optional) Add `Path` and `Domain` support later

If you want `Set-Cookie: session_id=abc123; Path=/; Domain=example.com`, you could extend the pair to `tuple<string,string,string,string>` or add a separate `setCookieAttr` method, but the **minimal** implementation above is enough to get `Cookie: name=value` flowing. You can always add attributes later without changing the core flow.

---

## 5. Step-by-step: wiring cookies in the request handler

### 5.1 In `RequestHandler::handle()`, after you determine it is NOT CGI (or before CGI launch), copy incoming cookies into the response context

`src/network/Client.cpp:_receiveData` — after line 44 (`rqst_handler.handle();`) and before the CGI check at line 45, add:

```cpp
// Copy incoming cookies from the request into the response so they can be re-emitted
// if the handler wants to set outgoing cookies based on the incoming ones.
for (auto &kv : _request._cookies) {
    _response.setCookie(kv.first, kv.second);
}
```

* **Why:** This is the most basic “echo” behavior: whatever cookies the browser sent back, the server sends right back. It is useful for session persistence behind a load balancer or simple stateless auth.
* **Why iterate `_request._cookies`:** The request object already parsed them in §3.2. Iterating the map is O(n) where n = number of cookies (typically 1–5), which is cheap.

### 5.2 In your business logic (e.g. after `rqst_handler.handle()`), set outgoing cookies

You can call `_response.setCookie("name", "value")` at any point after the handler runs. For example, in a simple "set session" endpoint:

```cpp
// Example inside your request handler code (not in the library):
_response.setCookie("session_id", "user-session-uuid-12345");
```

* **Why after `handle()`:** The handler may have just parsed a login, created a user session, and now you want the server to tell the browser about the new session cookie. Calling `setCookie` after handler completion keeps the concern separate from the request-parsing concern.

### 5.3 Ensure `_buildCgiResponse` and `_sendData` forward the cookie headers

`_buildCgiResponse` already appends headers to `_response` (see `cgihelp.md` §9.5 Slice 4). Since `_cookies` is a member of `HttpResponse` and `build()` now emits them ( §4.3 ), the CGI response will automatically include `Set-Cookie` lines no matter where they were queued. No extra code is needed in `_buildCgiResponse` or `_sendData` beyond what already exists for regular headers.

* **Why it works:** `HttpResponse::build()` produces `_header_buffer` that contains status line + all headers (including the `Set-Cookie` lines you added via `setCookie`) + `\r\n\r\n`. Then `_sendData` at `src/network/Client.cpp:82` does `send(m_fd, _response.getHeaderBuffer().c_str() + _bytes_sent, ...)` — it sends the whole buffer, cookie lines and all. No extra wiring required.

---

## 6. Summary of all changes you must make (minimal set)

| File | Change | Lines (approx) |
|------|--------|----------------|
| `include/http/HttpRequest.hpp` | Add `std::map<std::string, std::string> _cookies;` after existing members | 1 |
| `src/http/HttpRequest.cpp` | Parse `Cookie` header into `_cookies` after the existing header loop ( §3.2 ) | ~25 |
| `include/http/HttpResponse.hpp` | Add `std::vector<std::pair<std::string, std::string>> _cookies;` after `_headers` | 1 |
| `src/http/HttpResponse.cpp` | Add `setCookie(name, value)` method ( §4.2 ) ~5 lines; emit cookies in `build()` ( §4.3 ) ~4 lines | ~10 |
| `src/network/Client.cpp:_receiveData` | After `rqst_handler.handle();`, iterate `_request._cookies` and call `_response.setCookie` ( §5.1 ) ~6 lines | ~6 |

Total: **≈47 lines added** across 5 files. Zero existing lines are modified or deleted — only **new** code is added.

---

## 7. Testing checklist (run after you write the code)

1. **Start the server**: `./webserv config.conf` (use any config that points at a static dir or CGI dir).
2. **First request (no prior cookies)**:  
   `curl -i http://localhost:8080/`  
   → Response should have `Set-Cookie: session_id=<uuid>` (if you set one in your handler) **or** no `Set-Cookie` if you haven’t set any yet.
3. **Second request (browser replays the cookie)**:  
   `curl -i -b "session_id=abc123" http://localhost:8080/`  
   → The server should read `Cookie: session_id=abc123` and you can inspect `_request._cookies` in a debug log (add `LOG_DEBUG << "cookie: " << kv.first << "=" << kv.second;` inside the parsing loop).
4. **Multiple cookies**: Set two cookies via `setCookie`, then `curl -i http://localhost:8080/` → both `Set-Cookie` lines should appear in the response.
5. **CGI with cookies**: If you have a Python/Perl script that reads `Cookie` env vars, the script should receive `HTTP_COOKIE` in its environment (the `Cgi::_setEnv` loop at `cgihelp.md` §15 already converts every header to `HTTP_<UPPERCASE>` — cookies will become `HTTP_COOKIE` automatically once `_cookies` is populated; you just need to also push `HTTP_COOKIE` into the CGI env from the `Client` side if you want the script to see it — but that is optional and out of scope for the minimal implementation).

If all five steps work, your cookie support is complete and boringly correct.

---

## Appendix A: Full minimal `setCookie` + `build` diff

Below is the exact diff you can paste if you want to apply the minimal changes quickly. All lines are new — nothing is removed.

**`include/http/HttpRequest.hpp`** — after line that ends private section (before `}`):

```cpp
std::map<std::string, std::string> _cookies;
```

**`src/http/HttpRequest.cpp`** — after the existing header-populating loop (around line 130), paste:

```cpp
// ---- cookie parsing starts here ----
std::string cookieHeader = _headers["cookie"]; // _headers already lowercases keys
size_t pos = 0;
std::string token;
while ((pos = cookieHeader.find(';')) != std::string::npos) {
    token = cookieHeader.substr(0, pos);
    size_t eq = token.find('=');
    if (eq != std::string::npos) {
        std::string name  = token.substr(0, eq);
        std::string value = token.substr(eq + 1);
        size_t b = name.find_first_not_of(" \t");
        if (b != std::string::npos) name = name.substr(b);
        size_t e = name.find_last_not_of(" \t");
        if (e != std::string::npos) name = name.substr(0, e+1);
        b = value.find_first_not_of(" \t");
        if (b != std::string::npos) value = value.substr(b);
        e = value.find_last_not_of(" \t");
        if (e != std::string::npos) value = value.substr(0, e+1);
        _cookies[name] = value;
    }
    cookieHeader.erase(0, pos + 1);
}
{
    size_t eq = cookieHeader.find('=');
    if (eq != std::string::npos) {
        std::string name  = cookieHeader.substr(0, eq);
        std::string value = cookieHeader.substr(eq + 1);
        size_t b = name.find_first_not_of(" \t");
        if (b != std::string::npos) name = name.substr(b);
        size_t e = name.find_last_not_of(" \t");
        if (e != std::string::npos) name = name.substr(0, e+1);
        b = value.find_first_not_of(" \t");
        if (b != std::string::npos) value = value.substr(b);
        e = value.find_last_not_of(" \t");
        if (e != std::string::npos) value = value.substr(0, e+1);
        _cookies[name] = value;
    }
}
// ---- cookie parsing ends here ----
```

**`include/http/HttpResponse.hpp`** — after `_headers` member (line 31), add:

```cpp
std::vector<std::pair<std::string, std::string>> _cookies;
```

**`src/http/HttpResponse.cpp`** — after `reset()` (after line 136), add:

```cpp
void HttpResponse::setCookie(const std::string &name, const std::string &value)
{
    _cookies.push_back(std::make_pair(name, value));
}
```

And inside `build()` (around line 78, before the final `_header_buffer += "\r\n"`), add:

```cpp
// Emit queued Set-Cookie headers
for (size_t i = 0; i < _cookies.size(); ++i) {
    _header_buffer += "Set-Cookie: " + _cookies[i].first + "=" + _cookies[i].second + "\r\n";
}
```

**`src/network/Client.cpp:_receiveData`** — after line 44 (`rqst_handler.handle();`), add:

```cpp
// Echo incoming cookies into the response so they travel back to the browser
for (auto &kv : _request._cookies) {
    _response.setCookie(kv.first, kv.second);
}
```

That is the entire cookie feature. Compile with `make` (or `make re` if objects are already built) and test with `curl` as described in §7.