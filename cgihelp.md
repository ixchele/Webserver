# cgihelp.md — From zero to “bytes on the wire”, boringly explained

> **Contract with you:** I touch ONLY this file. I never edit `src/cgi/Cgi.cpp:1`, `include/cgi/Cgi.hpp:1`, `src/network/Client.cpp:1` or `include/network/Client.hpp:1`.
> Everything below explains **your** code line-by-line, why every variable exists, how parsing works, why headers are given one way and taken another, and how to finish exactly where you stopped (`src/network/Client.cpp:350` your stub `_parseCgiHeaders` + the `if ()` hole).

How to read this file: code blocks are **tiny** (2-6 lines) followed immediately by **why** each line exists. If you skip the “why”, you will copy-paste without owning the code — don’t.

---

## 0. Where you actually stopped

On branch `Nameless` today you have:

* `include/cgi/Cgi.hpp:1` — complete, modern header (no `_buffer` anymore, notify pipe + file).
* `src/cgi/Cgi.cpp:1` — 95% complete. Compiles except two real bugs (see §14).
* `include/network/Client.hpp:1` — has `_cgi`, `_cgi_start`, `_cgi_body_off`, helper declarations.
* `src/network/Client.cpp:1` — `Client::Client` is fixed, `startCgi`, `_handleCgiEvent`, `_cgiTimeout`, `_buildCgiResponse` exist, **but**:
  * `src/network/Client.cpp:265` — `while ((n = pread(..., read_at) > 0))` has a precedence bug.
  * `src/network/Client.cpp:326` — `if (lname == "satatus" ...)` typo.
  * `src/network/Client.cpp:351` — `_parseCgiHeaders` is stubbed: `if ()` empty at line 373, function never returns, never parses anything. That is why `make` fails:
    ```
    src/network/Client.cpp:373:17: error: expected primary-expression before ')' token
    ```
  * `src/network/Client.cpp:23` — `_receiveData` currently closes the `body_fd` and never calls `startCgi()` — it just builds a dummy `501`. That is your “hook ready” placeholder. The real wiring is §8.

You did NOT misunderstand the architecture. You stopped inside the parser because the parser is the only place where 10 tiny rules collide at once. This file re-derives it from zero.

---

## 1. CGI in one boring paragraph

CGI (Common Gateway Interface, RFC 3875) is a contract: *the web server runs a program (the “script”) as a child process, feeds it the HTTP request meta-data through **environment variables** and the request body through **stdin**, and captures what the script prints to **stdout**.* The script’s stdout is **not** HTTP — it is a pseudo-HTTP block: `Header: value\r\n` lines, one blank line, then body bytes. Your server translates that block into a real `HTTP/1.1 200 OK\r\n...` response and `send()`/`sendfile()`s it.

```
browser --TCP--> Client socket --recv()--> HttpRequest --isCgi?--> fork+execve --> script
                                                                         stdout --> /tmp file --> parse --> HttpResponse --> send()/sendfile() --> browser
```

No magic. Just: create a file, fork, wire three file descriptors, wait, read a little of the file, remember an offset, reuse your normal `_sendData`.

---

## 2. Vocabulary you must own before reading code

| Word | What it really is |
|------|-------------------|
| `fd` | integer index into the kernel’s per-process file descriptor table. `0`=stdin, `1`=stdout, `2`=stderr. `open()`, `pipe()`, `mkstemp()` return a new `fd`. |
| `pipe(fd[2])` | kernel creates two fds that share a FIFO. `write(fd[1])` appears on `read(fd[0])`. If every `fd[1]` is closed, `read(fd[0])` returns `0` (EOF). |
| `dup2(old,new)` | makes `new` point to the same open file description as `old`, closing `new` first. After `dup2(out_fd, 1)`, `printf` writes into `out_fd`’s file. |
| `fork()` | clones the process. Parent gets child pid, child gets `0`. Both share open fds at that instant (same offsets). |
| `execve(path, argv, envp)` | replaces the child’s memory with a new program. Open fds survive unless `FD_CLOEXEC`. Memory, code, variables are gone. |
| `mkstemp(tmpl)` | creates `/tmp/xxxXXXXXX` atomically, mode `0600`, returns open `fd`, mutates `tmpl` to real name. Only race-free way. |
| `pread(fd,buf,n,off)` | read `n` bytes from absolute offset `off` without moving the shared file offset. Needed because child moved offset while writing. |
| `sendfile(out,in,&off,n)` | kernel copies `n` bytes from file `in` at `*off` to socket `out`, bumps `*off`. Zero user-space copy. |
| `waitpid(pid,&status,opts)` | reap a child. `WIFEXITED`, `WEXITSTATUS` macros decode `status`. |
| `epoll` | kernel waits for `EPOLLIN`/`EPOLLOUT` on many fds without polling. **Cannot watch regular files** (`EPERM`) — that is why we keep a pipe for notification. |
| `off_t` | signed type for file offsets (`lseek`, `stat.st_size`, `sendfile`). |
| `HttpResponse` | your friend’s class. You only use its public API: `setStatusCode`, `setHeader`, `setBody`, `setFileBody`, `build`, `getHeaderBuffer`, `getFileFd`, `getFileSize`, `hasFile`. |

---

## 3. The 9 steps that happen for one CGI request

1. `Client::_receiveData` finishes `HttpRequest::parse` -> `COMPLETE`.
2. `RequestHandler::isCgi()` returns true (checks `cgi_pass` map). You get `interpreter`, `script_path`, `body_fd` from the handler.
3. `Client::startCgi(interp, script, body_fd)` creates `new Cgi`, calls `Cgi::execute`, registers notify fd with epoll, removes client fd from epoll.
4. `Cgi::execute` does `pipe`, `mkstemp`, `fork`, in child `dup2`s, `chdir`, `execve`. In parent, closes notify write end and `body_fd`.
5. Script runs. Its stdout bytes land in the temp file’s page cache. When it exits, kernel closes its copy of the notify write end.
6. Epoll wakes `Client` with `EPOLLIN` on notify read end. `Cgi::readOutput` sees `read()==0` (EOF) and `waitpid` reaps.
7. `Client::_handleCgiEvent` calls `Client::_buildCgiResponse`: `fstat` size, `pread` a bounded window to find `\r\n\r\n` or `\n\n`, parse headers, decide status, call `HttpResponse::setFileBody` + override `Content-Length`, store `body_start` in `_cgi_body_off` and `_file_offset`, `build`.
8. `delete _cgi` — destructor unlinks temp path, closes notify fds and its own file fd. The response still holds an open fd to the same inode, so data lives.
9. `Client::_sendData` sends headers with `send`, then body with `sendfile` starting at `_file_offset`. `CFINISHED` -> keep-alive `_reset` closes response fd, inode freed, no temp litter.

If any step fails: status `502 Bad Gateway` (or `504` on timeout). If the child hangs: `CGI_TIMEOUT=16` kills it.

---

## 4. Every environment variable — why it is set that way

Below is your `Cgi::_setEnv` (`src/cgi/Cgi.cpp:33`) broken into single pushes.

```cpp
_env.push_back("GATEWAY_INTERFACE=CGI/1.1");
```
* **What:** `GATEWAY_INTERFACE` tells the script which CGI spec you speak.
* **Why literal:** Always `CGI/1.1`. Not derived from request. The script checks it to know it can trust the variables below.

```cpp
_env.push_back("SERVER_PROTOCOL=" + _request.getVersion());
```
* **What:** `SERVER_PROTOCOL` is the HTTP version string, e.g. `HTTP/1.1`.
* **Why `getVersion()`:** It returns exactly what the client sent on the request line (`HTTP/1.1`). Your old bug was `push_back(_request.getVersion())` with no `=` — the child would see a bare `HTTP/1.1` string with no name, which `execve` ignores or mangles. Fixed at `src/cgi/Cgi.cpp:35`.
* **Why not `HTTP_VERSION`:** CGI spec name is `SERVER_PROTOCOL`, not `HTTP_VERSION`.

```cpp
_env.push_back("SERVER_SOFTWARE=webserv/1.0");
```
* **What:** Your server’s name. Some scripts log it, some ignore it.
* **Why literal:** It never changes. Do not derive from config.

```cpp
std::string host = _request.getHeader("host"); // e.g. "localhost:8080" or "example.com"
size_t colon = host.find(':');
if (colon != std::string::npos) { name = host.substr(0, colon); port = host.substr(colon+1); }
_env.push_back("SERVER_NAME=" + name);
_env.push_back("SERVER_PORT=" + port);
```
* **What:** `SERVER_NAME` = hostname without port, `SERVER_PORT` = port without hostname.
* **Why split:** The script may build absolute URLs. `Host` header may or may not contain `:port`. Your code at `src/cgi/Cgi.cpp:38` does exactly this. If `Host` is missing, both become empty — that is correct per spec, the script gets empty values rather than garbage.

```cpp
_env.push_back("REQUEST_METHOD=" + _request.getMethodStr()); // "GET" / "POST" / "DELETE"
```
* **What:** The HTTP verb. The script’s `if (REQUEST_METHOD=="POST")` branch depends on this.
* **Why `getMethodStr()` not `getMethod()`:** The latter returns enum `HTTP_GET=0`, not a string.

```cpp
_env.push_back("REQUEST_URI=" + _request.getUri().getOriginal());
_env.push_back("SCRIPT_NAME=" + _request.getUri().getPath());
```
* **What:** `REQUEST_URI` = full original URI including `?query` (`/cgi/hello.py?x=1`), `SCRIPT_NAME` = path part (`/cgi/hello.py`).
* **Why both:** Scripts sometimes need the query, sometimes just the routed path. Both come from your `Uri` object. `getOriginal()` preserves exactly what the client sent; `getPath()` is already decoded/normalized.

```cpp
_env.push_back("SCRIPT_FILENAME=" + _script_path);
```
* **What:** Absolute filesystem path to the script (`/home/.../www/cgi/hello.py`). The interpreter uses it.
* **Why `SCRIPT_FILENAME` not `SCRIPT_FILEENAME`:** Your file at `src/cgi/Cgi.cpp:53` currently says `SCRIPT_FILEENAME` — missing `N`. That is a typo. Scripts that check `SCRIPT_FILENAME` (PHP, Python wrappers) will see nothing and fail. Fix one letter: `SCRIPT_FILENAME`.

```cpp
_env.push_back("QUERY_STRING=" + _request.getUri().getQuery());
```
* **What:** The part after `?`. For `GET /a?x=1&y=2`, this is `x=1&y=2`. Empty string if no `?`.
* **Why from Uri:** Already parsed, no need to re-split.

```cpp
if (_body_fd != -1) {
    struct stat st;
    if (fstat(_body_fd, &st)==0) { oss << st.st_size; _env.push_back("CONTENT_LENGTH=" + oss.str()); }
}
```
* **What:** `CONTENT_LENGTH` = byte count of request body the script will read on stdin.
* **Why `fstat(_body_fd)` not `getContentLength()`:** `_body_fd` is the fd to the temp body file HttpRequest created (`/tmp/webserv_body_XXXXXX`). Its `st_size` is truth — the bytes on disk — not the header value which could be lying or chunked. This is why the constructor at `src/cgi/Cgi.cpp:17` must be `_body_fd(body_fd)` not `_body_fd(-1)` — your old code threw the fd away and always produced no `CONTENT_LENGTH`. You already fixed this.
* **Why only if `!= -1`:** `GET` has no body (`openBodyFile()` returns `-1`), so no `CONTENT_LENGTH` is correct.

```cpp
std::string content_type = _request.getHeader("content-type");
if (content_type != "") _env.push_back("CONTENT_TYPE=" + content_type);
```
* **What:** `CONTENT_TYPE` = e.g. `application/x-www-form-urlencoded`. Only set if the client sent it.
* **Why without `HTTP_` prefix:** Spec says `CONTENT_TYPE` and `CONTENT_LENGTH` are NOT `HTTP_` prefixed (they are not “HTTP_” meta-variables). Every other request header is.

```cpp
for (it = _request.getHeaders().begin(); it != _request.getHeaders().end(); ++it) {
    if (nm == "content-length" || nm == "content-type") continue;
    std::string env = "HTTP_";
    for (i...) env += (nm[i]=='-') ? '_' : toupper(nm[i]);
    _env.push_back(env + "=" + vl);
}
```
* **What:** Every other header becomes `HTTP_<UPPERCASE_WITH_UNDERSCORES>`. Example: `User-Agent: curl` -> `HTTP_USER_AGENT=curl`, `X-Custom: hi` -> `HTTP_X_CUSTOM=hi`.
* **Why loop:** So the script can read any header without you hardcoding names.
* **Why skip those two:** Because we already handled them as `CONTENT_*` and the spec says not to duplicate them as `HTTP_CONTENT_*`.
* **Why `toupper` and `-`→`_`:** CGI spec requires it. Shell env vars cannot contain `-`, and historically they are uppercase.
* **Why `getHeaders()` already lowercases keys:** HttpRequest at `src/http/HttpRequest.cpp:134` lowercases header names on parse, so comparison with `content-length` works case-insensitively.

```cpp
for (i) _cenv.push_back(_env[i].c_str());
_cenv.push_back(NULL);
```
* **What:** `_cenv` is `vector<const char*>` pointing at the c_str of each `_env` string, terminated by `NULL`. `execve` wants `char*const envp[]` null-terminated.
* **Why safe:** `_env` owns the strings, `_cenv` points inside them, both live as long as `Cgi` lives, and we call `execve` before `Cgi` dies. Do not let `_env` reallocate after this — we call `_setEnv` only once in the constructor (`src/cgi/Cgi.cpp:24`).

---

## 5. Members of `Cgi` — every variable and what it is for

`include/cgi/Cgi.hpp:35`:

```cpp
HttpRequest &_request;
```
* Reference to the client’s parsed request. We read method, URI, version, headers, and nothing else. Not owned, not copied — must outlive `Cgi` (it does, `Client::_request` lives as long as connection).

```cpp
std::string _interpreter;  // e.g. "/usr/bin/python3"
std::string _script_path;  // e.g. "/home/.../www/cgi/hello.py"
int _body_fd;              // fd to request body temp file, or -1
```
* `_interpreter` is `argv[0]` for execve. `_script_path` is `argv[1]`. `_body_fd` is what we `dup2` onto stdin in the child. If you lose it (old bug), `CONTENT_LENGTH` is never set and stdin is empty — POST CGI gets zero bytes.

```cpp
int _notify[2]; // [0]=read end parent watches, [1]=write end child keeps
int _output_fd; // fd from mkstemp, child’s stdout points here
std::string _output_path; // "/tmp/_cgi_XXXXXX" mutated by mkstemp
```
* `_notify` exists SOLELY because `epoll` cannot watch a regular file. Its EOF = “child no longer holds stdout”. No data flows — we `read()` into `junk[64]` and discard.
* `_output_fd` is where the script’s prints go. No buffering in RAM. The kernel’s page cache is the buffer.
* `_output_path` is needed once: to re-open the file via `HttpResponse::setFileBody(path)` in `Client::_buildCgiResponse`. After that, the path is unlinked but the inode lives while any fd is open.

```cpp
int _status;   // raw waitpid status
pid_t _pid;    // child pid from fork, -1 before fork
bool _reaped;  // did waitpid succeed
time_t _start; // time of execute, maybe used for timeout (you use Client::_cgi_start instead)
```
* `_status` together with `WIFEXITED`/`WEXITSTATUS` tells `exitedCleanly()` whether to trust the output. Non-zero exit -> `502`.

```cpp
std::vector<std::string> _env;        // owns "NAME=value" strings
std::vector<const char*> _cenv;       // pointers + NULL for execve
std::vector<const char*> _cargv;      // [interpreter, script, NULL]
```
* Three vectors cooperate: `_env` owns memory, the other two are views for `execve`. See §4 for why.

---

## 6. `Cgi::execute()` — every line, every syscall, every reason

`src/cgi/Cgi.cpp:92`:

```cpp
if (pipe(_notify)==-1) { LOG_ERROR ... return -1; }
```
* Creates the empty notification pipe. On failure, nothing else was opened, so just log and fail. Parent will turn this into `500` then `501` via `startCgi` error path.

```cpp
if (fcntl(_notify[0], F_SETFL, O_NONBLOCK)==-1) { close both; return -1; }
```
* Makes the read end non-blocking. Not strictly required (EOF returns `0` even non-blocking), but if epoll ever wakes spuriously, `read` should not block the event loop. If this fails we must close both ends — otherwise fds leak.

```cpp
char tmpl[] = "/tmp/_cgi_XXXXXX";
_output_fd = mkstemp(tmpl);
if (_output_fd==-1) { close pipe; return -1; }
_output_path = tmpl;
```
* `mkstemp` atomically creates and opens a file with `0600`. `tmpl` is a mutable `char[]` — `mkstemp` overwrites the `X`s with a unique suffix. Your template at `src/cgi/Cgi.cpp:106` is `/tmp/_cgi_XXXXXX`; any prefix is fine, but keep it under `/tmp` and keep 6 `X`s. Store the mutated string and the fd separately. On failure, clean the pipe first.

```cpp
_pid = fork();
if (_pid==-1) { close pipe, close _output_fd, unlink path, return -1; }
```
* `fork` clones. On `-1` every resource we created must be undone. Note `unlink` here removes the file we just created — no litter.

```cpp
if (_pid==0) { // CHILD
    (void)close(_notify[0]);
```
* Child closes the read end — only parent needs it. The write end (`_notify[1]`) stays open and survives `execve` (no `FD_CLOEXEC`). This is crucial: if it had `CLOEXEC`, the write end would vanish at `execve` and EOF would fire instantly — wrong. The EOF must fire when the **script process** dies, not when the `exec` happens.

```cpp
    (void)dup2(_output_fd, STDOUT_FILENO);
    (void)close(_output_fd);
```
* Child’s stdout *becomes* the temp file. `dup2` copies `_output_fd` onto fd `1`. The original fd is now redundant — close it so only fd `1` holds the file after exec. From now on `print("hello")` in Python writes into the kernel page cache for that file.

```cpp
    int input = (_body_fd != -1) ? _body_fd : open("/dev/null", O_RDONLY);
    if (input != -1) {
        (void)lseek(input, 0, SEEK_SET);
        (void)dup2(input, STDIN_FILENO);
        (void)close(input);
    }
```
* Child’s stdin becomes the request body. Three cases:
  * POST with body: `_body_fd` is a valid `O_RDONLY` fd to `/tmp/webserv_body_XXXXXX`. We `lseek` to `0` because the parent may have written and left offset at EOF; the script must read from start.
  * GET/no body: `_body_fd==-1`, so we `open("/dev/null")` — `read(stdin)` returns `0` (EOF) instantly, script does not hang.
  * Either way, `dup2(input, 0)` makes fd `0` point to it, then close the extra copy.

```cpp
    int blackhole = open("/dev/null", O_WRONLY);
    if (blackhole != -1) { (void)dup2(blackhole, STDERR_FILENO); (void)close(blackhole); }
```
* Child’s stderr goes to `/dev/null`. You at `src/cgi/Cgi.cpp:142` left a comment `I must remove this if I want to see script errors` — correct observation. Today stderr is silenced so error messages don’t corrupt stdout file — if the script traceback went to stdout, it would become part of the HTTP body and hide the real error. For debugging, change `O_WRONLY` to a log file or temporarily duplicate stderr to a file; but never leave stderr pointing at stdout.

```cpp
    std::string dir; ... (void)chdir(dir.c_str());
```
* `chdir` to the script’s directory (`/www/cgi`). Some scripts do `open("data.txt")` relative to their own directory. Note `dir` is `substr(0, slash)` — for `/a/b/c.py` it is `/a/b`, for `script.py` it is `.`, for `/script.py` it is `/`. This `chdir` is “best effort” — `(void)chdir` ignores failure on purpose; if it fails, `execve` will still try with the old cwd.

```cpp
    execve(_cargv[0], (char*const*)&_cargv[0], (char*const*)&_cenv[0]);
    LOG_ERROR << "execve(" << _interpreter << ") -> " << strerror(errno);
    _exit(127);
}
```
* Replaces child with the interpreter. On success, this line never returns. On failure (bad interpreter path, permission), we log and `_exit(127)` — not `exit()`, not `return`. `127` is conventional “exec failed”. `WEXITSTATUS==127` will be treated as `exitedCleanly()==false` -> `502`. No need to clean fds — process dies.

```cpp
(void)close(_notify[1]); _notify[1] = -1;
if (_body_fd != -1) { (void)close(_body_fd); _body_fd = -1; }
_start = time(NULL);
return 0;
```
* **Parent after fork:**
  * Closes the write end — parent must not hold it, otherwise `read()==0` never happens (parent would be a writer keeping EOF away). Set to `-1` so destructor does not double-close.
  * Closes `body_fd` ownership moves to child — parent no longer needs it, and it must not keep the underlying file alive after request resets.
  * Records start time (your `Client::_cgi_start` is the one actually checked by `handleTimeout`, but `_start` is fine to keep).

That is `execute()` from zero to hero. The only kernel-to-user copies are the `fork` and `pipe`; the CGI body never enters user RAM.

---

## 7. `readOutput()` / `killChild()` / destructor — tiny but lethal if wrong

`src/cgi/Cgi.cpp:176`:

```cpp
Cgi::e_out Cgi::readOutput() {
    char junk[64];
    ssize_t bytes = read(_notify[0], junk, sizeof(junk));
    if (bytes > 0) return MORE;
    if (bytes == 0) { // EOF
        int res;
        do { res = waitpid(_pid, &_status, WNOHANG); } while(res==-1 && errno==EINTR);
        if (res==0) { // child alive but writers closed — rare, be defensive
            (void)kill(_pid, SIGKILL);
            while(waitpid(_pid,&_status,0)==-1 && errno==EINTR) {}
        }
        _reaped = true;
        return DONE;
    }
    return FAIL;
}
```
* **`bytes>0`:** Should never happen — we never write. But if somehow junk arrives (some future code writes), stay in `MORE`.
* **`bytes==0`:** EOF. That means every write end (the child and any grandchild that inherited it) is gone. Now the stdout file is frozen. `waitpid(WNOHANG)` reaps if the child already exited. If `res==0`, the child still runs but closed stdout — rare but deadly (a script that `close(1)` and loops forever). We `SIGKILL` it and do a blocking `waitpid`. Without this, your server would hang forever in `CEXECUTING_CGI`.
* **`bytes==-1`:** Original file lacked `EAGAIN`/`EINTR` handling. You do `return FAIL;` for any error. That is safe but slightly less robust than:
  ```cpp
  if (errno==EAGAIN||errno==EINTR) return MORE; // spurious wake, keep waiting
  return FAIL;
  ```
  The subtlety: `EAGAIN` can happen on a non-blocking pipe that is *readable* but not yet EOF, if epoll spurious-wakes. Returning `FAIL` would incorrectly answer `502`. Consider adding that two-line guard.
* **No `EINTR` loop on `read` itself:** `read` can be interrupted by signal; if you want perfection, loop on `EINTR` too.

```cpp
void Cgi::killChild() {
    if (_pid>0 && !_reaped) { (void)kill(_pid, SIGKILL); while(waitpid(_pid,&_status,0)==-1 && errno==EINTR) {} }
}
```
* Called from `Cgi::~Cgi` and from `Client::_cgiTimeout`. `kill` on an already-dead pid is harmless (`ESRCH`), and `waitpid` after `kill` ensures no zombie. Never call `kill(-1, ...)` — guard `_pid>0` prevents that.

```cpp
Cgi::~Cgi() {
    killChild();
    if (_notify[0]!=-1) (void)close(_notify[0]);
    if (_notify[1]!=-1) (void)close(_notify[1]);
    if (_output_fd!=-1) (void)close(_output_fd);
    if (!_output_path.empty()) unlink(_output_path.c_str());
}
```
* Destructor is your leak-proof net. Three traps in your current code at `src/cgi/Cgi.cpp:232`:
  1. `if (_output_path.empty()) unlink` — **inverted logic**. It says “if the path IS empty, unlink an empty string” — nonsense. You meant `if (!_output_path.empty())`. Today every successful CGI **leaks** its `/tmp/_cgi_*` file because the non-empty path is never unlinked. Fix the `!`.
  2. `unlink` runs even while `HttpResponse` holds an fd to the same inode — that is **intended**. Unlinking removes the directory entry; the data survives until the last fd closes (`Client::_reset`/`HttpResponse::reset`). That is exactly how you get no-litter temp files.
  3. Closing order does not matter, but guarding with `!=-1` does — otherwise double-close via `close(-1)` would silently close some unrelated fd if you ever reuse `-1`. You do this correctly.

`getReadEnd()`, `getOutputFd()`, `getOutputPath()`, `getPid()`, `exitedCleanly()` are trivial getters. `exitedCleanly()` at `src/cgi/Cgi.cpp:207` checks `_reaped && WIFEXITED && WEXITSTATUS==0` — a crash (`WIFSIGNALED`) or `exit(127)` returns false -> `502`.

---

## 8. Client-side wiring — what you wrote and what the placeholder means

`include/network/Client.hpp:46`:

```cpp
Cgi *_cgi;          // NULL when no CGI running, owned pointer when CGI running
time_t _cgi_start;  // time execute was called — for CGI_TIMEOUT
off_t _cgi_body_off;// absolute file offset where CGI body starts
off_t _file_offset; // also used for static files — for sendfile’s *off
```
* `_cgi` is `NULL` most of the time. One `Client` = one CGI at most; epoll serializes events, so no concurrency on the same socket.
* `_cgi_body_off` appears redundant with `_file_offset`, but you need both: `_file_offset` is mutated by `sendfile` as it streams; `_cgi_body_off` is the *initial* offset you can log or reset. Your `Client::Client` at `src/network/Client.cpp:19` already initializes all three correctly: `_cgi(NULL), _cgi_start(0), _cgi_body_off(0)` — good.
* `_file_offset` at `src/network/Client.cpp:11` is used for both CGI body and static files. For CGI you **seed** it with `body_start`; for static files it starts `0`.

`src/network/Client.cpp:167` `startCgi`:

```cpp
int Client::startCgi(const std::string& interpreter, const std::string& script_path, int body_fd) {
    _cgi = new Cgi(_request, interpreter, script_path, body_fd);
    _cgi_start = time(NULL);
    if (_cgi->execute()!=0) { delete _cgi; _cgi=NULL; _buildError(500); m_state=CSENDING_HEADERS; _epoll.edit_fd(m_fd,this,EPOLLOUT); return -1; }
    m_state = CEXECUTING_CGI;
    if (_epoll.add_fd(_cgi->getReadEnd(), this, EPOLLIN)!=0) { delete _cgi; _cgi=NULL; _buildError(500); m_state=CSENDING_HEADERS; _epoll.edit_fd(m_fd,this,EPOLLOUT); return -1; }
    _epoll.del_fd(m_fd);
    return 0;
}
```
* Why `new Cgi` here: `Cgi` holds vectors that must outlive the call, so heap allocated.
* Two error paths both do `delete` + `_buildError(500)` + `EPOLLOUT` — so the client still gets a response rather than hanging.
* `m_state = CEXECUTING_CGI` and `add_fd(notify_read, EPOLLIN)`: now epoll watches the notify pipe, not the socket.
* `del_fd(m_fd)`: stop watching the client socket. If you kept it in `EPOLLIN` while CGI runs, a client that disconnects would fire `EPOLLRDHUP` interleaved — you still handle that via `handleTimeout` but the cleanest state is “only CGI event matters now”.

`src/network/Client.cpp:194` `_handleCgiEvent`:

```cpp
Epoll::EventState Client::_handleCgiEvent() {
    Cgi::e_out result = _cgi->readOutput();
    if (result==Cgi::MORE) return Epoll::ECONTINUE;
    _epoll.del_fd(_cgi->getReadEnd());
    if (result==Cgi::FAIL || !_cgi->exitedCleanly()) { delete _cgi; _cgi=NULL; _buildError(BadGateway);}
    else { _buildCgiResponse(); delete _cgi; _cgi=NULL; }
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);
    return Epoll::ECONTINUE;
}
```
* Order matters more than it looks:
  1. `del_fd(notify)` before any delete — otherwise you watch a closed fd.
  2. `_buildCgiResponse()` runs **while `_cgi` is still alive** — it needs `getOutputFd()` and `getOutputPath()`. If you did `delete _cgi` first, `getOutputPath()` would be destroyed and `setFileBody` would open a non-existent path.
  3. `delete _cgi` unlinks the temp path (after `setFileBody` already opened its own fd).
  4. `add_fd(m_fd, EPOLLOUT)` — re-arm the client socket for sending.
* Why `exitedCleanly()==false` -> `502`: a script that `exit(1)`, segfaults, or had `execve` fail should not be shown as `200`. The gateway failed.

`src/network/Client.cpp:238` `_cgiTimeout` and `src/network/Client.cpp:216` `handleTimeout`:

```cpp
void Client::_cgiTimeout() {
    LOG_WARN << "CGI timed out on client with fd " << m_fd;
    _epoll.del_fd(_cgi->getReadEnd());
    delete _cgi; _cgi=NULL;          // destructor SIGKILLs child
    _buildError(GatewayTimeout);      // 504
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);
}
```
* Called when `time(NULL)-_cgi_start > CGI_TIMEOUT` (16s) at `src/network/Client.cpp:148`. The two places that check are `handle_event` (early `EPOLLIN` path) and `handleTimeout` (global Multiplexer timer). Either can fire.
* `delete _cgi` inside `_cgiTimeout` will `killChild` → `SIGKILL` + `waitpid`. No zombie.
* `_buildError(504)` is the correct CGI timeout code (distinct from `408` client idle timeout at `src/network/Client.cpp:223`).

Your current `src/network/Client.cpp:40-63` `_receiveData` placeholder:

```cpp
RequestHandler rqst_handler(_request,_response,conf);
rqst_handler.handle();
if (rqst_handler.isCgi()) {
    int body_fd = rqst_handler.getBodyFd();
    std::string script = rqst_handler.getCgiScriptPath();
    std::string interp = rqst_handler.getCgiInterpreter();
    LOG_DEBUG << "CGI hook: ..." << body_fd;
    if (body_fd != -1) ::close(body_fd); // <-- you close it here today!
    if (_response.getHeaderBuffer().empty()) { _response.setStatusCode(501); ... _response.build(); }
}
m_state = CSENDING_HEADERS;
_epoll.edit_fd(m_fd,this,EPOLLOUT);
```
* You log well and you **close** `body_fd` immediately. When you actually launch CGI, you must **NOT** close it — ownership moves to `Cgi::execute`, which closes it after fork. If you close it prematurely, the child’s stdin gets `EBADF` and `duplicates /dev/null`, so `POST` bodies become empty. The correct wiring (your next edit, not this file) is:

```cpp
if (rqst_handler.isCgi()) {
    std::string script = rqst_handler.getCgiScriptPath();
    std::string interp = rqst_handler.getCgiInterpreter(script);
    int body_fd = rqst_handler.getBodyFd(); // -1 or fresh fd, ownership transfers
    if (script.empty() || interp.empty()) { if(body_fd!=-1) ::close(body_fd); _buildError(HttpStatus::InternalServerError); }
    else if (startCgi(interp, script, body_fd)!=0) { /* startCgi already errored */ }
    else return Epoll::ECONTINUE; // do NOT fall through to m_state=CSENDING_HEADERS
}
// non-CGI path below
```

Do NOT apply it now — this file documents it, your hand writes it later.

---

## 9. `_buildCgiResponse` — the hole you fell into, rebuilt line-by-line

This is `src/network/Client.cpp:247`. Your current code has two bugs plus an incomplete stub, so here is the intended logic in smallest explainable slices.

### 9.1 Goal

Turn the temp file `[_output_fd, path]` whose bytes are:

```
Header: value\r\n
Status: 200 OK\r\n   (optional, CGI directive)
Location: /x\r\n     (optional)
\r\n
<body bytes...>
```

into a `HttpResponse` that `Client::_sendData` can emit verbatim.

We must: find where headers end, parse headers into a map + status, decide the real status, re-open the file as the response body *minus* the header prefix, override `Content-Length` with the true body size, and seed the `sendfile` offset.

### 9.2 Slice 1 — know the file size and guard the fd

```cpp
static const size_t MAX_CGI_HEADERS = 64 * 1024; // 64 KiB

struct stat st;
if (_cgi->getOutputFd()==-1 || fstat(_cgi->getOutputFd(), &st)!=0) {
    LOG_ERROR << "cannot fstat cgi output fd";
    _buildError(HttpStatus::BadGateway);
    return;
}
const off_t file_size = st.st_size;
```
* `MAX_CGI_HEADERS` caps how many bytes we even look at for headers. A header block larger than this is `502`. Without a cap, a script that prints `A` forever without a blank line would make `window.append` grow until OOM — same flaw the old `std::string _buffer` had.
* `fstat` on the fd gives `file_size`. We use the fd, not the path, so it works even after we unlink.
* `st.st_size` is `off_t` (may be 64-bit). `file_size` includes headers + body.

### 9.3 Slice 2 — find the blank line with bounded `pread`

```cpp
std::string window;
char chunk[4096];
off_t read_at = 0;
size_t body_start = std::string::npos;
ssize_t n;

while ((n = pread(_cgi->getOutputFd(), chunk, sizeof(chunk), read_at)) > 0) {
    window.append(chunk, static_cast<size_t>(n));
    read_at += n;

    size_t crlf = window.find("\r\n\r\n");
    size_t lf   = window.find("\n\n");
    if (crlf != std::string::npos && (lf==std::string::npos || crlf < lf)) {
        body_start = crlf + 4; // skip "\r\n\r\n"
        break;
    }
    if (lf != std::string::npos) {
        body_start = lf + 2;   // skip "\n\n"
        break;
    }
    if (window.size() > MAX_CGI_HEADERS) {
        LOG_WARN << "CGI response headers exceed the limit";
        _buildError(HttpStatus::BadGateway);
        return;
    }
}
if (n==-1) { LOG_ERROR<<strerror(errno); _buildError(BadGateway); return; }
if (body_start==std::string::npos) { LOG_WARN<<"no header terminator"; _buildError(BadGateway); return; }
```
* **Why `pread` not `read`:** After the child wrote, the shared open file description’s offset sits at EOF. A `read` would return `0` instantly (nothing at EOF). `pread(fd,buf,n,read_at)` reads from absolute `read_at` without moving that EOF offset, so we can scan from byte `0` forward.
* **Why `window`:** We need a contiguous byte string to search for `\r\n\r\n` across chunk boundaries. `chunk` is 4 KiB; we append up to 64 KiB total. If headers straddle chunk 1 and chunk 2, `window.find` still finds them — that is why we cannot just inspect each chunk in isolation.
* **Why two terminators:** Spec says `\r\n\r\n`, but many scripts (Python `print()` with default `\n`) emit `\n\n`. Accepting both is required to not break simple scripts.
* **Why `crlf+4` vs `lf+2`:** The blank line itself is not part of the body. `body_start` is the absolute byte offset of the **first body byte**. Both `find` offsets are from byte `0` of the file, so `body_start` is also the distance from file start to body.
* **Your bug at `src/network/Client.cpp:265`:** You wrote `while ((n = pread(..., read_at) > 0))` — missing a `)`. Because `>` binds tighter than `=`, this parses as `n = (pread(...) > 0)` which stores `0` or `1` into `n`, never the byte count, never `>0` for large reads, and `if (n==-1)` never fires. The correct form is `while ((n = pread(_cgi->getOutputFd(), chunk, sizeof(chunk), read_at)) > 0)`. One parenthesis fixes the entire loop.
* **Why `window.size()>MAX` check inside loop:** So we error as soon as headers exceed the cap, without reading the rest of a 500 MB body into RAM.
* **Why `n==-1` and `npos` both `502`:** No terminator means the script did not speak the CGI header protocol — we cannot guess where body starts, so gateway error.

### 9.4 Slice 3 — parse only the header slice

```cpp
HttpStatus::Code status = HttpStatus::OK;
bool explicit_status = false;
bool has_location = false;
std::map<std::string, std::string> headers;

if (!_parseCgiHeaders(window.substr(0, body_start), status, explicit_status, headers, has_location)) {
    LOG_WARN << "CGI output contains malformed headers";
    _buildError(HttpStatus::BadGateway);
    return;
}
if (!explicit_status && has_location)
    status = HttpStatus::Found; // CGI/1.1: Location without Status → 302
const off_t body_size = file_size - static_cast<off_t>(body_start);
LOG_DEBUG << "CGI -> status " << status << ", body " << body_size << " bytes";
```
* `window.substr(0, body_start)` slices off exactly the header block including its blank line. Body bytes are not parsed — they are never even loaded into RAM beyond the first `pread`.
* `status`, `explicit_status`, `has_location`, `headers` are outputs of the parser (§10).
* `(!explicit_status && has_location)` -> `302`: spec rule. If a script wants to redirect and says only `Location: /foo`, your server must answer `302 Found`. If it says `Status: 301` + `Location`, that explicit status wins.
* `body_size` is truth: `file_size - body_start`. Not `atoi(Content-Length from script)`. A lying script is ignored; we ship exactly the bytes that exist.

### 9.5 Slice 4 — pour the result into your friend’s `HttpResponse`

`include/http/HttpResponse.hpp:7` and `src/http/HttpResponse.cpp:1`:

```cpp
_response.setStatusCode(status);
for (it=headers.begin(); it!=headers.end(); ++it) {
    const std::string lname = _lower(it->first);
    if (lname=="status" || lname=="content-length") continue;
    _response.setHeader(it->first, it->second);
}
```
* `setStatusCode` stores `status` so `_generateStatusLine()` will emit `HTTP/1.1 <code> <message>`.
* Loop copies each parsed CGI header into the response, except `Status` (a CGI directive, not a real header; clients must not see `Status: 404`) and `Content-Length` (script’s value may be wrong; we compute the real one next).
* Keep original case for header names (`it->first`) — `build()` emits exactly what you store.
* Your code at `src/network/Client.cpp:326` has `if (lname=="satatus" ...)` — typo. That lets `Status` leak to the client as a real header `Status: 404 Not Found`, which is spec-violating. Fix one `t`.

```cpp
if (body_size==0) {
    _response.setBody(""); // Content-Length: 0, _has_file=false
} else {
    if (!_response.setFileBody(_cgi->getOutputPath())) {
        LOG_ERROR << "cannot reopen cgi output file";
        _buildError(HttpStatus::BadGateway);
        return;
    }
    std::ostringstream oss; oss << body_size;
    _response.setHeader("Content-Length", oss.str());

    _cgi_body_off = static_cast<off_t>(body_start);
    _file_offset  = _cgi_body_off;
}
_response.build();
```
* **Empty body:** No file needed. `setBody("")` sets `Content-Length: 0` and leaves `_has_file=false`, so `_sendData` sends only headers and finishes.
* **Non-empty body:** `setFileBody(path)` reopens the temp file. Look at what it does at `src/http/HttpResponse.cpp:37`:
  ```cpp
  int fd = open(filepath.c_str(), O_RDONLY);
  fstat(fd, &file_stat);
  _file_fd = fd;
  _file_stream.open(filepath.c_str(), std::ios::binary);
  _file_size = file_stat.st_size; // == file_size (full file, headers included)
  setHeader("Content-Length", _intToString(_file_size)); // we override next
  ```
  It `stat`s the file and derives `Content-Length` as the **full** file size (headers included) — wrong for CGI body. That is why the very next line `setHeader("Content-Length", body_size)` **overrides** his value with the sliced size. Override must be after `setFileBody`, not before.
* **`_cgi_body_off` and `_file_offset`:** Both seeded with `body_start`. `setFileBody` leaves `_file_offset` at `0`; without seeding, `sendfile` would start at byte `0` and resend CGI headers as body. Seeding at `body_start` makes the first `sendfile` start at the body. See §13 how `_sendData` uses `&_file_offset`.
* **`build()`** at `src/http/HttpResponse.cpp:70` concatenates: `<status line>\r\n<headers>\r\n\r\n[+body if _has_file==false]`. For CGI with file body, headers are `getHeaderBuffer()`, body will be pulled later by `sendfile`, not copied now.

Why the order `setFileBody` -> override -> seed -> `build` matters, and why `delete _cgi` must come after this (as in `_handleCgiEvent`): `setFileBody` opens by path while the path still exists. `delete _cgi` unlinks the path. After delete, the inode survives only because `HttpResponse::_file_fd` and `_file_stream` still hold it. If you delete first, `open` fails.

---

## 10. `_parseCgiHeaders` — every line inside the parser

You stopped at `src/network/Client.cpp:351`:

```cpp
bool Client::_parseCgiHeaders(..., ...) {
    size_t start=0;
    while(start < block.size()) {
        size_t nl = block.find('\n', start);
        if (nl==std::string::npos) nl=block.size();
        std::string line = block.substr(start, nl-start);
        start = nl+1;

        if (!line.empty() && line[line.size()-1]=='\r') line.erase(line.size()-1);

        if (line.empty()) break;
        if (line.find('\r')!=std::string::npos) return false;

        if (line.compare(0,5,"HTTP/")==0) {
            if ()   // <-- you stopped here, line 373
        }
    }
}
```

### 10.0 What `block` is

`block` is `window.substr(0, body_start)` — the header block *including the final `\r\n`*. Example `block` value:

```
"Content-Type: text/html\r\nStatus: 404 Not Found\r\nLocation: /x\r\n\r\n"
```

Every iteration consumes one line. The `"\r\n"` vs `"\n"` duality matters here too, so the loop is built around `\n` and then trims an optional `\r`.

### 10.1 Line-by-line

```cpp
size_t start = 0;
while (start < block.size()) {
    size_t nl = block.find('\n', start);
    if (nl==std::string::npos) nl = block.size();
```
* `start` is the byte offset of the next line start inside `block`.
* `find('\n', start)` looks for the next LF. If none is found (malformed last line without `\n`), we treat `block.size()` as line end so the last line is still processed.

```cpp
    std::string line = block.substr(start, nl-start);
    start = nl + 1;
```
* `line` is the bytes **before** the `\n`, not including it. `start` advances past the `\n` for next iteration. If `nl==block.size()` (no NL), the next `start` becomes `block.size()+1` and the `while` exits.

```cpp
    if (!line.empty() && line[line.size()-1]=='\r') line.erase(line.size()-1);
```
* Strip the optional `\r` from a `CRLF` line. After this, `line` no longer contains any `\r` or `\n` — it is the bare header text. Example: `"Content-Type: text/html\r"` -> `"Content-Type: text/html"`.

```cpp
    if (line.empty()) break;
```
* An empty line is the header/body separator. Stop parsing headers. No error — this is expected. Any bytes after this in `block` would be the blank line’s `\r\n` pair, but we `substr`’d before that, so this typically exits on the last empty string.

```cpp
    if (line.find('\r')!=std::string::npos) return false;
```
* CRLF-injection defense (in §2.7). After trimming the single trailing `\r`, any remaining `\r` inside the line means the script embedded a carriage return mid-value, e.g. `X: a\r\nContent-Length: 0`. Without this check, you would later emit that injected header verbatim. Return `false` -> caller answers `502`.

```cpp
    if (line.compare(0,5,"HTTP/")==0) {
        if (!_extractStatusLine(line, status)) return false;
        explicit_status = true;
        continue;
    }
```
* **NPH (Non-Parsed Header) scripts** may emit a full status line first: `HTTP/1.1 200 OK`. The server must recognize it as a status and not store it as a header. `_extractStatusLine` (see §11) parses the number after the first space. `explicit_status=true` records that the script chose its own status — so the “Location implies 302” rule in §9.4 must not override it.

```cpp
    if (_lower(line).compare(0,7,"status:")==0) {
        if (!_parseStatusNumber(line.substr(7), status)) return false;
        explicit_status = true;
        continue;
    }
```
* `Status:` is the CGI way to set the response status without an NPH line. Case-insensitive (`_lower` check), value is `line.substr(7)` = everything after `Status:`. `_parseStatusNumber` trims OWS, reads a decimal, validates `100..999`. On success it sets `status`. Not stored as a header either — again, not a wire header.

```cpp
    size_t colon = line.find(':');
    if (colon==std::string::npos || colon==0) return false;
```
* Every real header must have `Name: value`. No colon or empty name is malformed -> `502`. This catches `BadLine` and `: value`.

```cpp
    std::string name  = line.substr(0, colon);
    std::string value = line.substr(colon+1);
```
* `name` is before the colon, `value` after. For `"Content-Type: text/html"` -> `name="Content-Type"`, `value=" text/html"` (note leading space still present — trimmed next).

```cpp
    for (size_t i=0;i<name.size();++i) {
        char c=name[i];
        bool ok = (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';
        if (!ok) return false;
    }
```
* Strict token validation. Header names may only contain alphanumerics, `-`, `_`. This blocks injection via a name like `X\r` or `X Evil` or `Content:Length` (colon inside name). Spec (RFC 7230 token) actually also allows `!#$%&'*+.^`|`~` but your server chose this strict subset — perfectly defensible and far safer for an evaluator to explain as injection defense. Any violation -> `502`.

```cpp
    size_t b = value.find_first_not_of(" \t");
    if (b==std::string::npos) value.clear();
    else {
        size_t e = value.find_last_not_of(" \t");
        value = value.substr(b, e-b+1);
    }
    for (size_t i=0;i<value.size();++i)
        if ((unsigned char)value[i] < 32 && value[i]!='\t') return false;
```
* OWS trimming + control-byte rejection. `value` may have leading/trailing spaces/tabs (`" text/html "`). HTTP says to ignore them. Interior control bytes (`0x00..0x1F` except `\t`) like an embedded `\r` or `\x00` are rejected — another injection fence.
* After this, `value` is the clean bytes the script intended, e.g. `"text/html"`.

```cpp
    if (_lower(name)=="location") has_location=true;
    headers[name]=value;
}
return true;
```
* Track whether `Location:` was present (for the 302 default). Store the header with **original case** preserved (`Content-Type` not `content-type`) — aesthetics but also spec-friendly, some clients inspect `Content-Type` exactly. Map `std::map` deduplicates by exact name; last value wins if script duplicates a header — acceptable.

That is the entire parser. Your missing half at `src/network/Client.cpp:371-375` is exactly the block above. Copy it in small pieces, not one blob, and compile after each piece.

---

## 11. Helpers — why each exists and every line inside

`include/network/Client.hpp:56` declares:

```cpp
bool _extractStatusLine(const std::string &line, HttpStatus::Code &status);
bool _parseStatusNumber(const std::string &s, HttpStatus::Code &status);
std::string _lower(const std::string &s) const;
```

### 11.1 `_extractStatusLine`

```cpp
bool Client::_extractStatusLine(const std::string &line, HttpStatus::Code &status) {
    size_t sp = line.find(' ');
    if (sp==std::string::npos) return false;
    return _parseStatusNumber(line.substr(sp+1), status);
}
```
* Input: `"HTTP/1.1 404 Not Found"`. `find(' ')` locates the space after `HTTP/1.1`. Everything after the space (`"404 Not Found"`) is passed to `_parseStatusNumber`, which ignores the reason phrase `Not Found` after reading the number. If there is no space (`"HTTP/1.1"`) it is malformed -> `false` -> `502`.

### 11.2 `_parseStatusNumber`

```cpp
bool Client::_parseStatusNumber(const std::string &s, HttpStatus::Code &status) {
    size_t i = s.find_first_not_of(" \t");
    if (i==std::string::npos) return false;
    int code=0;
    for(; i<s.size() && isdigit((unsigned char)s[i]); ++i) {
        code = code*10 + (s[i]-'0');
        if (code>999) return false;
    }
    if (code<100) return false;
    status = static_cast<HttpStatus::Code>(code);
    return true;
}
```
* `s` is the bytes after `Status:` or after `HTTP/1.1 ` — e.g. `" 404 Not Found"`.
* Trim leading OWS (` \t`). If nothing left, no number -> `false`.
* Read consecutive digits. `isdigit` must receive `unsigned char` — passing a signed `char` with value `0xFF` is UB in C++98. You do `static_cast<unsigned char>` at `src/cgi/Cgi.cpp:83` already — keep it here too.
* `code>999` rejects overflow / too-many digits.
* Loop stops at first non-digit (space before `Not Found`), so `"404 Not Found"` correctly yields `404`.
* `code<100` rejects `99` or `050` (leading-zero 50 truncated but still <100). Valid status are `100..999`.
* `static_cast<HttpStatus::Code>` stores the integer as the enum type your `setStatusCode` expects. `HttpStatus::Code` at `include/http/HttpStatus.hpp:7` enumerates all valid codes.

### 11.3 `_lower`

```cpp
std::string Client::_lower(const std::string &s) const {
    std::string out=s;
    for(size_t i=0;i<out.size();++i) out[i]=static_cast<char>(tolower((unsigned char)out[i]));
    return out;
}
```
* Returns a lowercased copy for case-insensitive comparison (so `Status:`, `STATUS:`, `status:` are all recognized). `tolower` likewise needs `unsigned char`. `const` method promises not to mutate the `Client`.

---

## 12. Headers: given one way, taken another — why

Three header “directions”:

**Direction A — Client → Server → CGI env (you *give* env vars to the script):**
* You *create* `HTTP_*`, `CONTENT_*`, `REQUEST_METHOD`, etc. from the client request. Purpose: the script never parses HTTP — you do. You hand it friendly variables.
* Why uppercase + `HTTP_` prefix: so the script can do `getenv("HTTP_USER_AGENT")` without parsing.

**Direction B — Script → Server (CGI stdout headers, you *take* from the script):**
* The script prints `Content-Type: text/html\r\nStatus: 404\r\n\r\n<body>`. You must `pread` and `_parseCgiHeaders` them. Purpose: the script tells *you* what kind of body it produced.
* Why not reuse `HttpRequest::parse`: that parser is for client requests (request line, `Host` required, chunked). CGI headers have a different grammar (`Status:` directive, NPH line, no request line, `Location` magic). Reusing it would mis-classify.

**Direction C — Server → Client (you *give* final HTTP headers to the browser):**
* You `setHeader("Content-Type", valueFromCGI)` or whatever else the script emitted, then `build()` emits `HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 42\r\n\r\n`.

Confusion comes from `Content-Length`: it appears **twice** (Direction A as `CONTENT_LENGTH` from `fstat(body_fd)` to script; Direction B as maybe `Content-Length` inside CGI output **ignored**; Direction C as the true `body_size` you override). The truth always comes from the filesystem, never from a header’s self-reported number.

---

## 13. How the CGI response becomes bytes on the wire (`_sendData` unchanged)

`src/network/Client.cpp:78` `_sendData`:

```cpp
if (m_state==CSENDING_HEADERS || m_state==CTIMEDOUT) {
    std::string headers = _response.getHeaderBuffer();
    ssize_t headers_bytes_sent = send(m_fd, headers.c_str()+_bytes_sent, headers.size()-_bytes_sent, 0);
    if (headers_bytes_sent==-1||headers_bytes_sent==0) return Epoll::EERROR;
    _bytes_sent += headers_bytes_sent;
    if (static_cast<size_t>(_bytes_sent)==headers.size()) {
        if (_response.hasFile()) { _bytes_sent=0; m_state=CSENDING_BODY; return Epoll::ECONTINUE; }
        else m_state=CFINISHED;
    } else return Epoll::ECONTINUE;
}
if (m_state==CSENDING_BODY) {
    ssize_t body_bytes_sent = sendfile(m_fd, _response.getFileFd(), &_file_offset, APP_BUFFER_SIZE);
    if (body_bytes_sent==0 && _file_offset==_response.getFileSize()) m_state=CFINISHED;
    else if (body_bytes_sent==-1||body_bytes_sent==0) return Epoll::EERROR;
    if (_file_offset==_response.getFileSize()) m_state=CFINISHED;
    else return Epoll::ECONTINUE;
}
if (m_state==CFINISHED && _request.getHeader("connection")=="keep-alive") {
    m_state=CKEEPT_ALIVE;
    if (_epoll.edit_fd(m_fd,this,EPOLLIN)) return Epoll::EERROR;
    _reset(); // <- this closes _response fd and frees the inode
    return Epoll::ECONTINUE;
}
return Epoll::EFINISHED;
```
* **Headers:** `getHeaderBuffer()` is what `HttpResponse::build()` produced — status line + all CGI headers + `Content-Length: <body_size>` + blank line. `send()` may partially write (socket buffer full) — `headers.c_str()+_bytes_sent` retries from where you left off. `_bytes_sent` is reset to `0` only when headers are fully sent.
* **Body:** `sendfile(m_fd, file_fd, &_file_offset, 8192)` copies at most 8 KiB per `EPOLLOUT` event. `&_file_offset` is the crucial pointer `sendfile` bumps. You seeded it at `body_start` (see §9.5), so first byte it copies is the body’s first byte, not byte `0` of the file. After each `EPOLLOUT`, you are woken again, loop, and stream until `_file_offset==getFileSize()`. If the CGI was `body_size==0` you never enter `CSENDING_BODY` — header phase already marked `CFINISHED`.
* **Why `hasFile()` after CGI:** After `_buildCgiResponse` with a non-empty body, `_response` has a file, so you must `sendfile` even though `build()` did not embed the body. For empty body, `_has_file==false` and you are done.
* **Keep-alive:** `_reset()` at `src/network/Client.cpp:395` does `_response.reset()` which closes the file fd — **that** is when the unlinked inode’s disk space is reclaimed. Next request can reuse the same `Client` object.

No change to `_sendData` is needed. The single seed `_file_offset = body_start` is the bridge.

---

## 14. Bugs still in your tree — exact file:line to fix

| # | File:line | Bug | What to type |
|---|-----------|-----|--------------|
| 1 | `src/cgi/Cgi.cpp:53` | `SCRIPT_FILEENAME` (missing `N`) | `SCRIPT_FILENAME` |
| 2 | `src/cgi/Cgi.cpp:6` missing include | `ostringstream` used without `<sstream>` (works via transitive include today, breaks on some compilers) | add `#include <sstream>` |
| 3 | `src/cgi/Cgi.cpp:238` | `if (_output_path.empty()) unlink` — inverted, leaks `/tmp/_cgi_*` forever | `if (!_output_path.empty())` |
| 4 | `src/network/Client.cpp:265` | `while ((n = pread(..., read_at) > 0))` — precedence, `n` gets `0/1` | `while ((n = pread(_cgi->getOutputFd(), chunk, sizeof(chunk), read_at)) > 0)` |
| 5 | `src/network/Client.cpp:326` | `if (lname=="satatus" ...)` typo leaks `Status:` as wire header | `"status"` |
| 6 | `src/network/Client.cpp:351-376` | `_parseCgiHeaders` stub with `if ()` — does not compile, never returns correctly | paste the full body from §10.1 line-for-line, compile, then add helpers §11 |
| 7 | `src/network/Client.cpp:23` placeholder | `if(body_fd != -1) ::close(body_fd);` before starting CGI destroys POST bodies | when you hook real `startCgi`, **do not close** — transfer ownership |
| 8 | `src/cgi/Cgi.cpp:176` `readOutput` | No `EAGAIN`/`EINTR` guard — spurious `FAIL` possible | add `if(errno==EAGAIN||errno==EINTR) return MORE;` before `return FAIL;` |

Items 1–4 break behavior even if `make` succeeds. Item 5 breaks spec. Item 6 blocks compilation. Item 7 blocks POST. Item 8 is robustness.

---

## 15. Finish checklist — order matters, compile after each

1. Fix `src/cgi/Cgi.cpp:53` `SCRIPT_FILENAME`, add `#include <sstream>`, fix `src/cgi/Cgi.cpp:238` `!empty`. `make -j4` — should still compile (Client still broken).
2. Fix `src/network/Client.cpp:265` parenthesis. Add `#include <sstream>` to `Client.cpp` if not transitively visible. `make` — still fails on `if()`.
3. Replace `src/network/Client.cpp:350-376` `_parseCgiHeaders` with the full body from §10.1. Keep your existing signature, do not rename `_cgi_body_off` (your member) vs the doc’s historic `_cgi_body_offset` — pick one and stay consistent. `make` — headers parse, now only missing helpers.
4. Append helpers `_extractStatusLine`, `_parseStatusNumber`, `_lower` after `_parseCgiHeaders` (§11). Declare them already in `include/network/Client.hpp:50` — so only `Client.cpp` needs body. Add `#include <cctype>` for `isdigit`/`toupper`/`tolower`.
5. Fix `src/network/Client.cpp:326` `satatus` → `status`. `make re` — binary builds.
6. Wire `src/network/Client.cpp:23` `_receiveData` to actually call `startCgi` (see snippet in §8 bottom). Until you do, CGI never executes — the hook log is harmless. Do this last so you can test non-CGI paths while finishing the parser.
7. `make re && ./webserv configuraionFiles/webserv.conf` — test plan below.
8. Verify fd leaks: `ls -l /proc/$(pgrep webserv)/fd | wc -l` should stay flat across 100 `curl`s.
9. Verify litter: `ls /tmp/_cgi_* /tmp/webserv_cgi_*` after load — empty.

---

## 16. How to test before your evaluator does

| # | Script | `curl -i http://localhost:8080/cgi/...` expects |
|---|--------|--------------------------------------------------|
| 1 | `www/cgi/hello.py` with `print("Content-Type: text/html\r\n\r\n<h1>hi</h1>")` | `200 OK`, `Content-Type: text/html`, correct `Content-Length`, body `hi` |
| 2 | Body prints `Status: 404 Not Found\r\nContent-Type: text/plain\r\n\r\nnope` | `404 Not Found` status line, body `nope` |
| 3 | Body prints `Location: /index.html\r\n\r\n` | `302 Found` + `Location: /index.html` |
| 4 | Body prints with `\n` only (`print("Content-Type: text/plain\n\nbody")`) | `200` — LF-only terminator accepted |
| 5 | Body prints headers with no blank line (`print("X: 1\r\nX: 2")`) | `502 Bad Gateway` quickly (no unbounded read) |
| 6 | Body prints `X: a\r\nContent-Length: 999\r\n\r\n` (embedded `\r`) | `502` — injection rejected |
| 7 | `sys.stdout.write("A"*20_000_000)` | flat server RSS (`ps -o rss`), browser receives 20M, no OOM |
| 8 | `while True: pass` | `504 Gateway Timeout` after 16s, server stays alive |

---

## 17. Prove you own it (say these aloud before you touch the keyboard)

1. Why must CGI output be a file, not a pipe buffer, when the body can be 2GB? (RAM: one `std::string` holding everything is a DoS vector.)
2. Where does the stdout land before you parse? (Kernel page cache of the mkstemp file — zero user copy.)
3. A regular file cannot be `epoll`ed. How do you know “done”? (Notify pipe EOF == every writer closed == file frozen.)
4. Why must the notify write end survive `execve`? (Otherwise EOF fires at exec, before script runs.)
5. What is `body_start` in bytes? (Offset of first body byte from byte 0 = headers end + 4 or 2.) Why is it also the seed for `_file_offset`? (So `sendfile` starts at body.)
6. Why do you override the script’s `Content-Length`? (Script may lie; file size is truth.)
7. Why does `setFileBody` open by path while `Cgi::~Cgi` unlinks it right after? (Inode lives while any fd open; unlink only removes name.)
8. After keep-alive, where does the temp space go? (Unlinked earlier, closed by `_reset` → inode freed → no litter.)

---

## 18. One-sentence evaluator answer

> “CGI stdout is `dup2`ed onto a `mkstemp` file, a non-`CLOEXEC` notify pipe tells epoll when the child exits, I `pread` only the first 64 KB to find `\r\n\r\n` or `\n\n`, parse status/`Location`/headers with strict CRLF-injection checks, remember the body offset, override `Content-Length` with `file_size - body_start`, and stream the body with `sendfile` from that offset reusing the same `_sendData` that serves static files — constant RAM, kernel-to-kernel copy.”
