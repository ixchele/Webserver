# PRE-SUBMIT AUDIT — read everything, fix before you push

> **You asked me to touch nothing.** This file is the only file I wrote.  
> It lists **every bug / violation / fragility** I found after reading every `.cpp`/`.hpp`/`.conf` in this repo, with `file:line` and the exact fix to apply.  
> Severity: **CRITICAL = crash or grade 0**, **MAJOR = wrong answer / leak / timeout**, **MINOR = robustness / style / evaluator nitpick**.

---

## 0. How to use this file

1. Work top to bottom: CRITICAL first (they fail the defense even if CGI works), then MAJOR, then MINOR.
2. For each row: open `file:line`, apply the **Fix** snippet (copy-paste, not retype), recompile with `make re 2>&1 | tail`, test with the **Verify** command.
3. Do not `git add` the binary `webserv` (in `.gitignore:1`). The evaluator builds with your `Makefile`.

---

## 1. CRITICAL — you will get 0 or instant KO

### C-1 README does not meet subject Chapter V
- **Where:** `Readme.md:1`
- **What:** Content is `# Webserver`. Subject requires:
  1. First line italic: `*This project has been created as part of the 42 curriculum by <login1>[, <login2>...].*`
  2. `## Description`, `## Instructions`, `## Resources` sections (English).
  3. Resources must describe how AI was used, which parts.
- **Why grade 0 risk:** Explicit `Readme Requirements` chapter. Evaluators check it first.
- **Fix:**
  ```md
  *This project has been created as part of the 42 curriculum by <your_login>, <friend_login>.*
  ## Description
  42 webserv — HTTP/1.1 server in C++98: static files, upload, CGI (Python), config file, one epoll loop.
  ## Instructions
  `make && ./webserv Configs/configTest.conf`  … etc.
  ## Resources
  RFC 7230, nginx docs, `man epoll`, `man mkstemp` … AI used for …
  ```

### C-2 Listening socket is blocking
- **Where:** `src/network/Server.cpp:59` `create_socket()`
- **What:** You call `socket()` then `bind()` then `listen()`, never set `O_NONBLOCK` on `m_fd`. You set it on accepted clients (`accept4(... SOCK_NONBLOCK)`) and on the CGI notify pipe, but not on the server fd itself. Subject: *"Your server must remain non-blocking at all times"*. A blocking `accept()` inside an `EPOLLIN` handler can stall the single event loop.
- **Fix:** in `create_socket()` after `socket()`:
  ```cpp
  int flags = fcntl(this->m_fd, F_GETFL, 0);
  fcntl(this->m_fd, F_SETFL, flags | O_NONBLOCK);
  ```
  Or create with `socket(...); fcntl(... O_NONBLOCK);`. Include `<fcntl.h>` (already included transitively).

### C-3 Config routing leaks CGI outside its location
- **Where:** `src/http/RequestHandler.cpp:226` `_isCgiExtension()`
- **What:**
  ```cpp
  for (size_t i = 0; i < _config.locations.size(); ++i) {
      const LocationConfig &loc = _config.locations[i];
      if (loc.cgi_pass.find(ext) != loc.cgi_pass.end()) return true;
  }
  ```
  You scan **every** location, not just the matched `*_route`. Request `GET /uploads/file.py` where `/uploads` has no `cgi_pass` but `location .py { cgi_pass .py /usr/bin/python3; }` exists will be flagged CGI even though it should be handled as a normal file/upload. Evaluator can upload a `.py` to `/uploads` and expect static storage but you exec it.
- **Fix:** Remove that loop. Only check:
  ```cpp
  if (_route && _route->cgi_pass.find(ext) != _route->cgi_pass.end()) return true;
  if (_config.cgi_pass.find(ext) != _config.cgi_pass.end()) return true;
  return false;
  ```

### C-4 `accept4` is Linux-only — evaluator on macOS
- **Where:** `src/network/Server.cpp:96`
- **What:** `accept4(..., SOCK_CLOEXEC|SOCK_NONBLOCK)` needs `_GNU_SOURCE` and recent glibc. On macOS it does not exist, compilation fails. Subject *allows* `fcntl` but for macOS only `F_SETFL, O_NONBLOCK, FD_CLOEXEC`. Defence may be on macOS.
- **Fix:** Wrap:
  ```cpp
  int clientFd = accept(this->m_fd, NULL, NULL);
  if (clientFd != -1) {
      fcntl(clientFd, F_SETFL, O_NONBLOCK);
      fcntl(clientFd, F_SETFD, FD_CLOEXEC);
  }
  ```
  Keep `accept4` under `#ifdef __linux__` if you want.

---

## 2. MAJOR — wrong answer, leak, DoS, or broken CGI

### M-1 `SCRIPT_FILEENAME` typo
- **Where:** `src/cgi/Cgi.cpp:53`
- **What:** `SCRIPT_FILEENAME` (missing N). PHP/ Python wrappers read `SCRIPT_FILENAME`. You send the wrong name → script cannot find itself, returns 500/502. Previous `cgihelp.md` flagged it; it is still there in your tree.
- **Fix:** `SCRIPT_FILENAME`.

### M-2 Missing `#include <sstream>` in Cgi.cpp
- **Where:** `src/cgi/Cgi.cpp:1`
- **What:** You use `std::ostringstream` at `Cgi.cpp:61` and `Client.cpp:343` but never include `<sstream>` there. It compiles today only because some header transitively includes it. Add the include to be safe with `g++ -std=c++98 -Wall -Wextra -Werror`.
- **Fix:** Add `#include <sstream>` in `src/cgi/Cgi.cpp` (and `src/network/Client.cpp` already has via `<iostream>` but add explicitly).

### M-3 `readOutput()` treats `EAGAIN/EINTR` as `FAIL` → false 502
- **Where:** `src/cgi/Cgi.cpp:176` `readOutput()`
- **What:**
  ```cpp
  ssize_t bytes = read(_notify[0], junk, sizeof(junk));
  if (bytes > 0) return MORE;
  if (bytes == 0) { ... return DONE; }
  return FAIL;
  ```
  The pipe is `O_NONBLOCK`. A spurious `EPOLLIN` can cause `read()` to return `-1` with `EAGAIN`. You return `FAIL` → `Client::_handleCgiEvent:204` does `_buildError(502)` even though child is healthy. Also `EINTR` should be retried.
- **Fix:**
  ```cpp
  ssize_t bytes = read(_notify[0], junk, sizeof(junk));
  if (bytes > 0) return MORE;
  if (bytes == 0) { /* waitpid ... */ return DONE; }
  if (errno == EAGAIN || errno == EINTR) return MORE;
  return FAIL;
  ```

### M-4 `_buildCgiResponse` logs wrong error, hides real errno
- **Where:** `src/network/Client.cpp:295`
- **What:** `LOG_ERROR << "pread(cgi_output) -> " << -1;` logs literal `-1`, not `strerror(errno)`. Debugging becomes impossible.
- **Fix:** `LOG_ERROR << "pread(cgi_output) -> " << strerror(errno);`

### M-5 `Cgi::readOutput` leaks fd on `bytes==0` + `res==-1` (ECHILD)
- **Where:** `src/cgi/Cgi.cpp:186`
- **What:** If `waitpid` returns `-1` with `ECHILD` (already reaped elsewhere), you set nothing and still `_reaped=true; return DONE;` → `exitedCleanly()` returns false (because `_status==0`, `WIFEXITED` false) → 502. Not catastrophic but hides the real “lost child” case.
- **Fix:** On `res==-1` set `_status=0; _reaped=true;` (you do) but also set a flag or log. Your current code already does that — keep it, just add log.

### M-6 `RequestHandler::_handleCGI` opens `body_fd` and leaks it
- **Where:** `src/http/RequestHandler.cpp:347` `_handleCGI()`
- **What:** `LOG_DEBUG << ... << " body_fd=" << getBodyFd()` — `getBodyFd()` calls `HttpRequest::openBodyFile()` which `::open`s a new fd every call and never closes it. Each CGI hit leaks one fd, plus the `body_fd` ownership is confusing because `Client::_receiveData` already opened one. File-descriptor exhaustion after ~1000 requests → accept fails → server appears hung.
- **Fix:** In `_handleCGI` do not call `getBodyFd()` for logging. If you must log, log `getBodyFilePath()` only, or:
  ```cpp
  int tmp = getBodyFd(); LOG_DEBUG << tmp; if(tmp!=-1) ::close(tmp);
  ```

### M-7 `HttpResponse::setFileBody` leaves old `ifstream` open
- **Where:** `src/http/HttpResponse.cpp:37`
- **What:** If `_file_fd != -1` you `close(_file_fd)` but you never `close()` the previous `_file_stream` before `open()` the new one. `_file_stream.open()` will then fail if already open. Consequence: second CGI on keep-alive could fail to reopen the temp file → 502.
- **Fix:**
  ```cpp
  if (_file_stream.is_open()) _file_stream.close();
  if (_file_fd != -1) { close(_file_fd); _file_fd = -1; }
  // then open new
  ```

### M-8 Keep-alive does not clean temp files on reused connection
- **Where:** `src/network/Client.cpp:471` `_reset()`
- **What:** Only resets `_request`, `_response`, `_bytes_sent`, `_file_offset`. It leaves `_cgi_body_off` stale (minor) but more importantly does not unlink anything — however CGI temp file already unlinked by `Cgi::~Cgi`, so data lives only via `HttpResponse::_file_fd`. `_response.reset()` closes that fd → inode freed. So not a leak. **But** the request body temp file (`HttpRequest::_temp_filename`) is *not* cleaned if CGI never consumed it? `HttpRequest::reset()` does unlink. Since `_reset()` calls `_request.reset()`, it is cleaned. Okay — just set `_cgi_body_off=0;` for clarity.

### M-9 `Client::handle_event` ignores `EPOLLRDHUP` half-close
- **Where:** `src/network/Client.cpp:142`
- **What:** You return `EERROR` on `EPOLLERR|HUP|RDHUP`. That closes the connection immediately. If client used `shutdown(SHUT_WR)` after POST body (valid HTTP), you drop the body before parsing the terminator → lost upload.
- **Fix:** For now keep the early return, but before returning, try to drain: call `_receiveData` even on `RDHUP` if `EPOLLIN` is also set. Your current order checks `EPOLLERR...` first, so `RDHUP+IN` never reaches `_receiveData`. Change to:
  ```cpp
  if (event & (EPOLLERR|EPOLLHUP)) return EERROR;
  // then handle EPOLLIN/RDHUP
  ```

### M-10 `ConfigParser::clientBodyDir` unit parsing accepts garbage
- **Where:** `src/ConfigFileParser/ConfigParser.cpp:289`
- **What:** Input `"10xyz"` after reading `10`, `>> unit` reads `'x'` (in `KMGkmg`) → passes, then `unit='x'` → switch default does nothing → size remains uninitialized? Actually `size=10`, `unit='x'` → `find("KMGkmg")` contains? No, `'x'` not in, throws. But `"1024"` without unit reads `unit='k'` (default) and silently converts to `1024*1024` (1M) instead of 1024 bytes. Subject expects raw bytes or `k/m/g` suffix.
- **Fix:** If no unit character remains, treat as bytes:
  ```cpp
  size_t size; std::string token = this->currentContent();
  // parse trailing K/M/G manually
  ```

### M-11 Duplicate `location /google` in example config
- **Where:** `Configs/configTest.conf:38` and `:42`
- **What:** Two identical `location /google { return 301 ... }` blocks. Parser will add two entries with same path; `matchRoute` returns the first longest match (both length 7), so second is unreachable. Evaluator may think you support duplicate detection.
- **Fix:** Delete one.

---

## 3. MINOR — robust but evaluator will poke

### m-1 `Multiplexer` missing `waitpid` vs zombies if CGI leaks
- **Where:** `src/cgi/Cgi.cpp:199` `killChild` is only called from destructor. If you ever `delete _cgi` while child still running (timeout path), you send `SIGKILL` and `waitpid` — good. No global zombie.

### m-2 `HttpRequest` percent-decode blocks `2F` (encoded slash)
- **Where:** `src/http/Uri.cpp:91`
- **What:** You return `false` if `hex=="2F"||"2f"||"00"`. That rejects `/%2Fpath` correctly (directory traversal defense). Good — keep it.

### m-3 `Uri` not rejecting `%` at end
- **Where:** `src/http/Uri.cpp:88`
- **What:** If string is `"/path%"` or `"/path%2"`, `i+2 < length` fails, you fall through to `decoded+=str[i]` and keep the `%`. Should return `false`. Evaluators use `curl --path-as-is`.
- **Fix:**
  ```cpp
  if (str[i]=='%' ) {
      if (i+2 >= str.length()) return false;
      // then handle hex
  }
  ```

### m-4 `Tokenizer` column tracking off after comments
- **Where:** `src/ConfigFileParser/Tokenizer/tokenizer.cpp:28`
- **What:** When encountering `#`, you skip to `\n` but never update `col`. Error messages point to wrong column. Harmless.

### m-5 `Server::craft_key` uses `&buffer[0]` trick
- **Where:** `src/network/Server.cpp:147`
- **What:** `ssKey << &buffer[0];` streams a `char*` until `\0`. Works because `inet_ntop` null-terminates, but `ssKey << buffer` is clearer.
- **Fix:** `ssKey << buffer;`

### m-6 `Logger` not thread-safe but you are single-threaded — fine.

---

## 4. Subject compliance checklist (what evaluator will run)

| Requirement (subject §IV.1) | Where to prove | Status |
|------------------------------|----------------|--------|
| Config file arg | `main.cpp:18` | OK |
| Not execve another web server | `Cgi.cpp:160` only `execve(interpreter, script...)` | OK |
| One poll for all I/O | `Multiplexer::events_loop:69` single `epoll` object | OK |
| Never read/write without poll | `recv` only on `EPOLLIN` (§Client:26), `send/sendfile` only on `EPOLLOUT` (§Client:85) | OK — except Cgi fix M-3 |
| Do not check errno to adjust behaviour after read/write | You don’t — you log only | OK |
| Never hangs | Timeout in `Multiplexer::_handle_timeout:106` (16s) + CGI timeout (§Client:148) | OK |
| Browser compatible | Test `curl -i` + Chrome | Manual |
| Accurate status codes | `HttpStatus::codeMessage` + `setStatusCode` | OK |
| Default error pages | `RequestHandler::_buildErrorResponse:117` fallback HTML | OK, but add `error_pages/500.html` etc. |
| `fork` only for CGI | `grep -rn fork` shows only `Cgi.cpp:117` | OK |
| Static website | `RequestHandler::_handleGet:165` | OK |
| Upload | `RequestHandler::_handlePost:436` rename + EXDEV copy | OK — test `curl -X POST --data-binary @file` |
| GET/POST/DELETE | Present | OK |
| Listen multiple ports | `Multiplexer` loop over `v_configs[].listen` | OK — test with two `listen` in config |
| CGI env, chdir, EOF, content_length | Fix M-1 and ensure `chdir` to script dir at `Cgi.cpp:158` | OK after fix |

---

## 5. Tests they will run — reproduce now

```bash
make re && ./webserv Configs/configTest.conf &
WS=$!; sleep 1

# 1. Static
curl -si http://localhost:8080/ | head -n 5        # 200

# 2. 404 default page
curl -si http://localhost:8080/nope | head -n 5    # 404

# 3. CGI GET — LF-only header script (your www/cgi/hello.py)
curl -si http://localhost:8080/cgi/hello.py        # 200Content-Type text/html, body <h1>CGI OK

# 4. CGI POST with body
curl -si -X POST -d "a=1&b=2" http://localhost:8080/cgi/hello.py

# 5. Upload
curl -si -X POST --data-binary @www/big.bin http://localhost:8080/uploads/test1.txt
ls -l www/uploads/storage/                         # file appears

# 6. DELETE
curl -si -X DELETE http://localhost:8080/uploads/storage/test1.txt  # 204

# 7. Keep-alive stress
for i in $(seq 1 200); do curl -s http://localhost:8080/ >/dev/null & done; wait

# 8. Fd leak check
ls -l /proc/$WS/fd | wc -l
# run 200 CGI requests, wc -l must not grow

# 9. Temp litter
ls /tmp/_cgi_* /tmp/webserv_body_* 2>&1 | head

kill $WS
```

If test 3 fails with empty body: you hit `SCRIPT_FILEENAME` typo (M-1).

---

## 6. Fix order (copy-paste ready)

1. `Readme.md` → rewrite per C-1.
2. `src/cgi/Cgi.cpp:53` → `SCRIPT_FILENAME`.
3. `src/cgi/Cgi.cpp:1` → `#include <sstream>`.
4. `src/cgi/Cgi.cpp:176` → add EAGAIN guard (M-3).
5. `src/network/Client.cpp:295` → `strerror(errno)`.
6. `src/network/Server.cpp:59` → set `O_NONBLOCK` on listening socket (C-2).
7. `src/http/RequestHandler.cpp:226` → remove global scan loop (C-3).
8. `src/http/RequestHandler.cpp:347` → do not open body_fd for logging (M-6).
9. `src/http/HttpResponse.cpp:37` → close old stream before open (M-7).
10. `Configs/configTest.conf:42` → delete duplicate location.
11. `make re` until zero warnings (`-Werror` is on).

Do not commit `webserv` binary, `/tmp` files, or `obj/`.

---

*End of audit. Fix C-1 + C-2 + M-1 first — they are the only ones an evaluator can 0 you for without even reaching CGI.*
