# cgihelp.md — Finish the CGI pipeline the professional way

This document is written for *you* (the networking/CGI owner). It explains, in the
order you have to *think* about it, exactly what is wrong today, why the file-based
design is the right one, and gives you the **complete rewritten code for `Cgi`** plus
the **complete code you must add to `Client`**. Every important line is explained so you
can re-write it in front of your evaluator with your eyes closed.

The buddy system (your friend owns `HttpResponse`, `RequestHandler`, `HttpRequest`):
**you do not need to touch his code at all**. The design below reuses your friend's
public API (`setStatusCode`, `setHeader`, `setFileBody`, `setBody`, `build`) and only
`Client` members to make it work. That is the whole point of choosing this design.

---

## 0. What you have today, and why you stopped

Your CGI code (`src/cgi/Cgi.cpp`, `include/cgi/Cgi.hpp`) does this:

1. `execute()` forks. The child's stdout is wired to a **pipe** (`write` end).
2. Every time the pipe is readable, epoll wakes up the `Client` and `readOutput()`
   does `read() → _buffer.append(...)`.
3. `_buffer` is a `std::string`. The whole CGI output lives in RAM.
4. When the pipe returns EOF the child is reaped and `_handleCgiEvent()` → `_buildCgiResponse()`
   would parse `_buffer` (you stopped right there — the loop body of `_buildCgiResponse`
   at `src/network/Client.cpp:226` is empty).

**The core flaw you already identified is real:** a script that outputs 2 GB of data
makes the server hold 2 GB in one `std::string`. That is a remote memory-exhaustion
attack vector against your own process (a DoS by *your own* server), and there is no
clean partial-send story.

Bugs in the current code you must also know about (detailed in section 6):

* `Cgi()` initializes `_body_fd(-1)` **instead of** the `body_fd` argument → POST bodies
  never reach the script, `CONTENT_LENGTH` is never set. This is a real bug.
* `_setEnv()` pushes `_request.getVersion()` as an env variable with no `=` → malformed
  singleton `"HTTP/1.1"`. It should be `SERVER_PROTOCOL=...`.
* `"SCRIPT_FAILENAME"` typo → should be `"SCRIPT_FILENAME"`.
* `Cgi.cpp` is **not in the Makefile** (line 16-26 has no `./src/cgi/Cgi.cpp`). The
  binary you have has no CGI code in it at all.
* `Client`'s constructor never initializes `_cgi` or `_cgi_start` → your code reads an
  uninitialized pointer.
* The pipe read end `_outputPipe[0]` is **never closed** → an fd leak per CGI request.

---

## 1. The new design, one sentence at a time

Instead of buffering the script's stdout in a `std::string`, the child's **stdout is
`dup2()`-ed directly onto an open temporary file**. When the child exits, the *kernel*
has already written the whole output into that file (zombies of your buffer are gone,
and the OS page-cache is the buffer). Then:

1. You wait for the child to die **using a tiny notification pipe** (a regular file
   cannot be watched by epoll — more on this below).
2. You **parse only the header block** of that file (bounded memory, a few KB) into a
   `std::map<std::string,std::string>` and compute the **status code**.
3. You remember **the file offset where the body starts** (`_cgi_body_offset`).
4. You hand the temp file to your `HttpResponse` (again through his public API:
   `setFileBody(path)`), overriding the `Content-Length` with your computed body size.
5. `_sendData()` already knows how to send headers then `sendfile()` — with the body
   offset as the starting file position. **Your `_sendData()` does not change.**
6. When the response is finished, the fd is closed and the temp file is gone (it was
   `unlink()`-ed the moment you no longer needed the pathname).

Constant memory. Kernel-to-kernel copy for the body. Fits perfectly in the epoll
state machine you already wrote.

```
Client socket                    script (execve'd)
     |                                  |
   startCgi() creates:                  |
      /tmp/webserv_cgi_XXXXXX  <--mkstemp--+   (their stdout dup2's onto this fd)
      notify pipe[2]                       |
     fork() ->-----------------------> child | dup2(out_fd, STDOUT)
                                          |   | dup2(body_fd, STDIN)
                                          |   | dup2(/dev/null, STDERR)
                                          |   | execve(interpreter, script, ...)
     parent keeps: _output_fd, notify[0];  when child exits:
     closes notify[1]                       write-end of notify closes => EOF
          |
   epoll watches notify[0] (EPOLLIN)  <---- EOF readable
          |
   readOutput(): read()==0  =>  waitpid(reap)  =>  DONE
          |
   _buildCgiResponse():
      pread(header block up to "\r\n\r\n") with a 64KB cap   [constant RAM]
      parse into map + status
      body_offset = file position right after the blank line
      _response.setStatusCode + setHeader(...) each cgi header
      _response.setFileBody(path)      -> his fd to the same file, size known
      _response.setHeader("Content-Length", body_size)   [we override his]
      _file_offset = _cgi_body_offset = body_offset
      _response.build()
   delete _cgi  =>  cgi dtor unlinks the tmp path + closes notify pipes + closes its fd
          |
   m_state = CSENDING_HEADERS; epoll -> EPOLLOUT on the client socket
          |
   _sendData(): send headers -> CSENDING_BODY:
      sendfile(sock, response.getFileFd(), &_file_offset, 8KB) from body_offset
      _file_offset == file_size  =>  CFINISHED
      keep-alive? _reset() closes the response fd (inode freed) -> EPOLLIN again
```

---

## 2. Concepts you must truly own (read this before the code)

### 2.1 What a CGI response actually looks like on the wire

The script writes to **stdout**:

```
Content-Type: text/html\r\n
Location: /somewhere\r\n        (optional)
Status: 404 Not Found\r\n        (optional, CGI-only directive)
Some-Header: value\r\n
\r\n
<body bytes…>
```

- The header/body separator is an **empty line**: `\r\n\r\n` (some sloppy scripts write
  `\n\n`). The body is *everything* after that blank line.
- `Status: <code> <reason>` is **not a real HTTP header** — it is a CGI directive that
  tells the server which status line to emit. Real HTTP servers never forward `Status`.
- If the script sends `Location:` but no `Status:`, CGI/1.1 says the server answers
  **302 Found**.
- Some ancient/NPH-style scripts print a full `HTTP/1.1 200 OK` first line. We accept
  that defensively: parse it as a status line and skip it.
- The line ending is `\r\n` normally, plain `\n` sometimes. We accept both by trimming
  one trailing `\r`.

### 2.2 Pipe EOF = "all writers are gone"

A pipe gives EOF on the read end **when every copy of the write end is closed** — not
when the process exits. That property is exactly what we exploit:

- the child inherits the notify-pipe write end through `execve` (so it stays open while
  the script lives),
- when the script (and all its children that inherited the fd) exits, the last write end
  closes → our read end sees `read() == 0`.

The notification pipe **carries no data**. It is purely a "the child is done" signal
that epoll can watch.

### 2.3 Why the output cannot be a pipe full of data → the file

The old design *streamed* the output through the pipe into `std::string`. Two problems:

1. RAM. Unbounded.
2. A pipe is a *kernel FIFO*; if you do not read it, the child blocks. If you read it,
   you must store it somewhere. Storage in RAM is the flaw.

By pointing the child's stdout at a **regular file** (via `dup2` onto the `mkstemp`
fd) the kernel writes decode directly into the page cache — no user-space copying, no
RAM blowup, the file can be arbitrarily large. Then `sendfile()` sends page-cache →
socket in kernel space.

### 2.4 You cannot `epoll` a regular file

`epoll_ctl(EPOLL_CTL_ADD, <regular-file-fd>, ...)` does not work on Linux (returns
`EPERM`). That is the *reason* we keep a real pipe around: the pipe is what epoll
watches, and its EOF is our "file is complete" signal. This is a classic Unix pattern,
and it is why "just poll the fd" is not an option in an event loop.

### 2.5 `mkstemp` + `unlink` while open

- `mkstemp("/tmp/aaaXXXXXX")` creates a file with mode `0600`, unique name, and returns
  an open fd. It modifies its input buffer to contain the real name. It is the only
  race-free way to make temp files.
- You may `unlink()` the path immediately while the fd stays open. The inode survives
  until the **last fd is closed**, then the disk space is reclaimed automatically. That
  is how you guarantee **no garbage temp files** even if the server crashes.
  We don't unlink immediately here because we re-open by path once with
  `setFileBody(path)`; but the important property is: **after that re-open, deleting the
  path is harmless**.

### 2.6 `pread` vs `read`, and file offsets

- `pread(fd, buf, n, off)` reads *without* moving the shared file offset. Since the
  child moved the shared offset while writing (both share the same open file
  description), you must use `pread` to read from an absolute position — and reading
  with `read()` when the offset sits at the end would return nothing.
- `sendfile(sock, in_fd, &off, count)` reads `count` bytes from `in_fd` **starting at
  `*off`** and writes them to `sock`, then updates `*off` by the number of bytes moved.
  If it partially sends (socket buffer full), it returns the bytes moved and you call it
  again with the same `off`. **This is exactly your existing `_sendData()` body loop, so
  by planting `_file_offset = body_offset` you reuse all of it.**
- `sendfile`'s `off` parameter is a *pointer* — use it on the file, not on the socket.

### 2.7 CRLF injection (why we validate headers)

Headers end at a blank line. If a script emits
`X-Evil: 2\r\nContent-Length: 0` you have, on the wire, a *second* header line. If you
just moved the whole thing into your header block, you would send the injected
`Content-Length`/`Connection`/whatever. Defense: after you split lines, any **line whose
only `\r` is not the (single, last) trim char is rejected** and any header **name that
contains anything other than `A-Z a-z 0-9 - _` is rejected**. Bounded, strict, safe.

---

## 3. File 1 — `include/cgi/Cgi.hpp` (complete rewrite)

Changes from today: `_buffer` is gone, `getOutput()` is gone, `_outputPipe` becomes
`_notify` (data pipe → notify pipe), add `_output_fd`, `_output_path`, the accessors
`getOutputFd()`/`getOutputPath()`, and drop the never-implemented `_reap()`.

```cpp
#ifndef CGI_HPP
#define CGI_HPP

#include <HttpRequest.hpp>
#include <vector>
#include <string>
#include <ctime>

// PIPE_BUFFER_SIZE is now dead (we no longer stream script data through a pipe).
// You may delete it; it is kept only to minimize the diff.

class Cgi
{
public:
  enum e_out
  {
    MORE,   // keep waiting: no event or not finished yet
    DONE,   // child exited: the output file is complete and reaped
    FAIL    // something broke
  };

  Cgi(HttpRequest &request, const std::string &interpreter,
      const std::string &script_path, int body_fd);
  ~Cgi();

  int   execute();          // fork + redirect stdio onto the temp file
  e_out readOutput();       // poll the *notify* pipe until EOF, then reap
  void  killChild();

  int              getReadEnd() const;    // notify pipe read end (the epoll fd)
  int              getOutputFd() const;   // fd of the temp file (child's stdout)
  const std::string &getOutputPath() const; // pathname while it still exists
  pid_t            getPid() const;
  bool             exitedCleanly() const;

private:
  HttpRequest &_request;
  std::string  _interpreter;
  std::string  _script_path;
  int          _body_fd;

  int      _notify[2];     // pipe: [0]=epoll read end (EOF when child dies), [1]=held by child
  int      _output_fd;     // temp file the script writes into (mkstemp)
  std::string _output_path;// temp pathname, unlinked in the destructor
  int      _status;        // waitpid() status
  pid_t    _pid;
  bool     _reaped;
  time_t   _start;

  std::vector<std::string>  _env;
  std::vector<const char *> _cenv;
  std::vector<const char *> _cargv;

  void _setArgv();
  void _setEnv();
};

#endif
```

Why a **fd**, not an `std::ofstream`, for the script output? Because `dup2` and
`sendfile` need a raw file descriptor, and the child's stdout is a raw fd. (You said
"fstream" — you *do* parse through a stream-like abstraction below; the file I/O layer
is the fd. Handling `ofstream` also means dealing with buffering/flush timing on the
child side, which is exactly where scripts "lose" the last bytes. Raw fd is honest.)

---

## 4. File 2 — `src/cgi/Cgi.cpp` (complete rewrite)

Read it top to bottom, then read the "why" notes under each block.

```cpp
#include <Logger.hpp>
#include <Cgi.hpp>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <sstream>
#include <string>
#include <map>

Cgi::Cgi(HttpRequest &request, const std::string &interpreter,
     const std::string &script_path, int body_fd)
  : _request(request), _interpreter(interpreter),
    _script_path(script_path),
    _body_fd(body_fd),             // FIX: the old code had _body_fd(-1) here
    _output_fd(-1), _status(0),
    _pid(-1), _reaped(false), _start(0)
{
  _notify[0] = -1;      // nothing exists yet: every cleanup path checks -1
  _notify[1] = -1;
  _setArgv();
  _setEnv();
}
```

- `_body_fd(body_fd)` — the constructor now *keeps* the body fd that the friend passes.
  Everything downstream (`_setEnv`'s `CONTENT_LENGTH`, and `dup2` of stdin in
  `execute`) depends on it being right. Before your change the bug silently made every
  CGI think it had no request body.
- `_notify[0] = _notify[1] = -1` : the destructor and every error path in `execute()`
  call `close(fd)` guarded by `!= -1`, so "not created yet" must be a sentinel.

```cpp
void Cgi::_setArgv() {
  _cargv.push_back(_interpreter.c_str());
  _cargv.push_back(_script_path.c_str());
  _cargv.push_back(NULL);
}
```

`execve(argv[0], argv, envp)`: first arg is the **interpreter** (e.g. `/usr/bin/python3`),
second is the script path. `c_str()` is safe here because both strings live for the whole
life of the object — they are members.

```cpp
void Cgi::_setEnv() {
  _env.push_back("GATEWAY_INTERFACE=CGI/1.1");
  _env.push_back("SERVER_PROTOCOL=" + _request.getVersion());   // FIX: was getVersion() alone
  _env.push_back("SERVER_SOFTWARE=webserv/1.0");

  std::string host = _request.getHeader("host");
  std::string name, port;
  size_t colon = host.find(':');
  if (colon != std::string::npos) {
    name = host.substr(0, colon);
    port = host.substr(colon + 1);
  }
  _env.push_back("SERVER_NAME=" + name);
  _env.push_back("SERVER_PORT=" + port);

  _env.push_back("REQUEST_METHOD=" + _request.getMethodStr());
  _env.push_back("REQUEST_URI=" + _request.getUri().getOriginal());
  _env.push_back("SCRIPT_NAME=" + _request.getUri().getPath());
  _env.push_back("SCRIPT_FILENAME=" + _script_path);            // FIX: was SCRIPT_FAILENAME
  _env.push_back("QUERY_STRING=" + _request.getUri().getQuery());

  if (_body_fd != -1) {
    struct stat st;
    if (fstat(_body_fd, &st) == 0) {
      std::ostringstream oss;
      oss << st.st_size;
      _env.push_back("CONTENT_LENGTH=" + oss.str());
    }
  }
  std::string content_type = _request.getHeader("content-type");
  if (content_type != "")
    _env.push_back("CONTENT_TYPE=" + content_type);

  std::map<std::string, std::string>::const_iterator it;
  for (it = _request.getHeaders().begin(); it != _request.getHeaders().end(); ++it) {
    const std::string &nm = it->first;
    const std::string &vl = it->second;

    if (nm == "content-length" || nm == "content-type")
      continue;

    std::string env = "HTTP_";
    for (size_t i = 0; i < nm.size(); ++i)
      env += (nm[i] == '-') ? '_' : static_cast<char>(::toupper(nm[i]));
    _env.push_back(env + "=" + vl);
  }

  for (size_t i = 0; i < _env.size(); ++i)
    _cenv.push_back(_env[i].c_str());
  _cenv.push_back(NULL);
}
```

- Every value we hand the child is an `"NAME=value"` byte string. Any entry without an
  `=` (your previous `HTTP/1.1` singleton) is skipped by `execve` at best, undefined at
  worst.
- `CONTENT_LENGTH` comes from `fstat` on the body fd — that is why the constructor fix
  in section 4 matters.

Now the function that changed architecture completely:

```cpp
int Cgi::execute() {
  if (pipe(_notify) == -1) {
    LOG_ERROR << "pipe() -> " << strerror(errno);
    return -1;
  }
  if (fcntl(_notify[0], F_SETFL, O_NONBLOCK) == -1) {
    LOG_ERROR << "fcntl() -> " << strerror(errno);
    (void)close(_notify[0]); _notify[0] = -1;
    (void)close(_notify[1]); _notify[1] = -1;
    return -1;
  }

  char tmpl[] = "/tmp/webserv_cgi_XXXXXX";
  _output_fd = mkstemp(tmpl);
  if (_output_fd == -1) {
    LOG_ERROR << "mkstemp() -> " << strerror(errno);
    (void)close(_notify[0]); _notify[0] = -1;
    (void)close(_notify[1]); _notify[1] = -1;
    return -1;
  }
  _output_path = tmpl;

  _pid = fork();
  if (_pid == -1) {
    LOG_ERROR << "fork() -> " << strerror(errno);
    (void)close(_notify[0]); _notify[0] = -1;
    (void)close(_notify[1]); _notify[1] = -1;
    (void)close(_output_fd); _output_fd = -1;
    unlink(_output_path.c_str()); _output_path.clear();
    return -1;
  }

  if (_pid == 0) {
    (void)close(_notify[0]);          // read end belongs to the parent only

    (void)dup2(_output_fd, STDOUT_FILENO);  // script writes straight into the file
    (void)close(_output_fd);               // drop the extra copy

    int input = (_body_fd != -1) ? _body_fd : open("/dev/null", O_RDONLY);
    if (input != -1) {
      (void)lseek(input, 0, SEEK_SET);
      (void)dup2(input, STDIN_FILENO);
      (void)close(input);
    }

    int blackhole = open("/dev/null", O_WRONLY);
    if (blackhole != -1) {
      (void)dup2(blackhole, STDERR_FILENO);
      (void)close(blackhole);
    }

    std::string dir;
    size_t slash = _script_path.find_last_of('/');
    if (slash == std::string::npos) dir = ".";
    else if (slash == 0)             dir = "/";
    else                             dir = _script_path.substr(0, slash);
    (void)chdir(dir.c_str());

    execve(_cargv[0], (char *const *)&_cargv[0], (char *const *)&_cenv[0]);
    LOG_ERROR << "execve(" << _interpreter << ") -> " << strerror(errno);
    _exit(127);
  }

  (void)close(_notify[1]);            // parent keeps only the read end
  _notify[1] = -1;

  if (_body_fd != -1) {               // body consumed by the child via stdin
    (void)close(_body_fd);
    _body_fd = -1;
  }

  _start = time(NULL);
  return 0;
}
```

Why this is correct:

- **`mkstemp` is done before `fork`.** The fd it returns is shared by both. In the child
  we `dup2` it onto fd 1. From that moment the script's `print`/`write` lands in the
  file, in kernel space. Zero user-space copies.
- **The notify pipe write end is deliberately *not* closed in the child and is *not*
  `CLOEXEC`.** It survives `execve` and dies with the script. When the script (and any
  of its children that inherited it) exits, the last write end closes → EOF on
  `_notify[0]`. If we made it `FD_CLOEXEC`, the pipe would close at `execve` time — the
  EOF would arrive *before* the script even ran. Wrong.
- `O_NONBLOCK` on `_notify[0]`: non-blocking reads on an EOF'd pipe still return 0
  (not `EAGAIN`), but a spurious `EAGAIN` is safe to translate to `MORE`.
- Error paths are total: every fd we opened is either closed or handed to the guard in
  the destructor (`!= -1`), and the temp file is unlinked on the spot.

```cpp
Cgi::e_out Cgi::readOutput() {
  char junk[64];
  ssize_t bytes = read(_notify[0], junk, sizeof(junk));

  if (bytes > 0)
    return MORE;                       // nobody ever writes here; keep waiting
  if (bytes == 0) {                    // EOF -> the child's stdout is final
    int res;
    do { res = waitpid(_pid, &_status, WNOHANG); }
    while (res == -1 && errno == EINTR);
    if (res == 0) {                    // child alive but stdout closed: kill
      (void)kill(_pid, SIGKILL);
      do { res = waitpid(_pid, &_status, 0); }
      while (res == -1 && errno == EINTR);
    } else if (res == -1) {
      _status = 0;                     // ECHILD: treat as "not clean"
    }
    _reaped = true;
    return DONE;
  }
  if (errno == EAGAIN || errno == EINTR)
    return MORE;
  return FAIL;
}
```

- The pipe is **empty and tiny** — the old chunk-reading loop is gone. The data never
  touches user RAM.
- EOF ⇒ writers are gone ⇒ the file is frozen ⇒ *now* we `waitpid`. We use `WNOHANG`
  first and fall back to `SIGKILL`+blocking if the child is pathological (prevents a
  script that closed stdout from hanging the whole event loop). The kill-then-wait
  pattern is exactly what your old code did on `bytes == 0`.

```cpp
void Cgi::killChild() {
  if (_pid > 0 && !_reaped) {
    (void)kill(_pid, SIGKILL);
    while (waitpid(_pid, &_status, 0) == -1 && errno == EINTR) {}
  }
}

bool Cgi::exitedCleanly() const {
  return (_reaped && WIFEXITED(_status) && WEXITSTATUS(_status) == EXIT_SUCCESS);
}

int Cgi::getReadEnd() const {
  return _notify[0];
}

int Cgi::getOutputFd() const {
  return _output_fd;
}

const std::string &Cgi::getOutputPath() const {
  return _output_path;
}

pid_t Cgi::getPid() const {
  return _pid;
}

Cgi::~Cgi() {
  killChild();
  if (_notify[0] != -1) (void)close(_notify[0]);
  if (_notify[1] != -1) (void)close(_notify[1]);
  if (_output_fd != -1) (void)close(_output_fd);
  if (!_output_path.empty()) unlink(_output_path.c_str());
}
```

The destructor is the **only** place pipe fds are finally closed (that fd leak fix) and
the temp path is `unlink()`ed. Because the body fd has already been `dup2`'d by the
response by then, the unlink only removes the *name*; the data lives until the last
fd closes.

---

## 5. File 3 — `include/network/Client.hpp` (additions only)

Three additions: initialize two members you forgot, and add one `off_t`.

```cpp
  // in the class body, near the other members:
  Cgi *_cgi;
  time_t _cgi_start;
  off_t _cgi_body_offset;      // <-- NEW: absolute file offset where the CGI body starts

  // private helpers (NEW):
  bool _parseCgiHeaders(const std::string &block,
                        HttpStatus::Code &status, bool &explicit_status,
                        std::map<std::string, std::string> &headers,
                        bool &has_location);
  bool _extractStatusLine(const std::string &line, HttpStatus::Code &status);
  bool _parseStatusNumber(const std::string &s, HttpStatus::Code &status);
  std::string _lower(const std::string &s) const;
```

Also `#include <sys/types.h>` at the top if `off_t` is not transitively visible.

---

## 6. File 4 — `src/network/Client.cpp` (the meat of your new job)

### 6.1 Constructor — fix the zombie pointer

```cpp
Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd, AFd::CLIENT), m_lastActivity(time(NULL)), m_state(CRECEVING),
      m_configs(configs), _epoll(epoll), _request(fd), _bytes_sent(0),
      _file_offset(0), _cgi(NULL), _cgi_start(0), _cgi_body_offset(0)
{
}
```

Uninitialized `_cgi` means `_handleCgiEvent()` (which tests `_cgi != NULL`) and
`~Client()` read random stack junk before your first CGI ever runs. This is a
show-stopper bug; fix it.

### 6.2 New includes

At the top of `src/network/Client.cpp` add:

```cpp
#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <sys/stat.h>
```

### 6.3 `_buildCgiResponse()` — the part you stopped at, now complete

Replace the empty function at `src/network/Client.cpp:226` with this. Read the numbered
notes under it before memorizing.

```cpp
void Client::_buildCgiResponse() {
    static const size_t MAX_CGI_HEADERS = 64 * 1024;

    struct stat st;
    if (_cgi->getOutputFd() == -1 || fstat(_cgi->getOutputFd(), &st) != 0) {
        LOG_ERROR << "cannot fstat cgi output fd";
        _buildError(HttpStatus::BadGateway);
        return;
    }
    const off_t file_size = st.st_size;

    // (1) read the header block with a hard size cap, never the whole file
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

        if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf)) {
            body_start = crlf + 4;
            break;
        }
        if (lf != std::string::npos) {
            body_start = lf + 2;
            break;
        }
        if (window.size() > MAX_CGI_HEADERS) {
            LOG_WARN << "CGI response headers exceed the limit";
            _buildError(HttpStatus::BadGateway);
            return;
        }
    }
    if (n == -1) {
        LOG_ERROR << "pread() failed on cgi output: " << strerror(errno);
        _buildError(HttpStatus::BadGateway);
        return;
    }
    if (body_start == std::string::npos) {   // no blank line at all -> garbage
        LOG_WARN << "CGI output has no header terminator";
        _buildError(HttpStatus::BadGateway);
        return;
    }

    // (2) turn the raw header block into a map + a status code
    HttpStatus::Code status = HttpStatus::OK;
    bool explicit_status = false;
    bool has_location = false;
    std::map<std::string, std::string> headers;

    if (!_parseCgiHeaders(window.substr(0, body_start), status, explicit_status,
                          headers, has_location)) {
        LOG_WARN << "CGI output contains malformed headers";
        _buildError(HttpStatus::BadGateway);
        return;
    }

    if (!explicit_status && has_location)
        status = HttpStatus::Found;          // CGI/1.1: Location without Status => 302

    const off_t body_size = file_size - static_cast<off_t>(body_start);
    LOG_DEBUG << "CGI -> status " << status << ", body " << body_size << " bytes";

    // (3) fill the friend's response object with OUR data
    _response.setStatusCode(status);

    std::map<std::string, std::string>::const_iterator it;
    for (it = headers.begin(); it != headers.end(); ++it) {
        const std::string lname = _lower(it->first);
        if (lname == "status" || lname == "content-length")
            continue;                        // we control both ourselves
        _response.setHeader(it->first, it->second);
    }

    if (body_size == 0) {
        _response.setBody("");               // Content-Length: 0, nothing to sendfile
    } else {
        if (!_response.setFileBody(_cgi->getOutputPath())) {
            LOG_ERROR << "cannot reopen cgi output file";
            _buildError(HttpStatus::BadGateway);
            return;
        }
        std::ostringstream oss;
        oss << body_size;
        _response.setHeader("Content-Length", oss.str());

        _cgi_body_offset = static_cast<off_t>(body_start);   // (4)
        _file_offset = _cgi_body_offset;                     // (5)
    }

    _response.build();
}
```

Notes, numbered to the code:

1. **The RAM-is-dangerous fix, applied to the parser too.** `window` holds *at most*
   `MAX_CGI_HEADERS` bytes, because the loop breaks the instant the terminator is found
   and errors out if headers exceed the cap. Even a script that prints 10 GB without a
   blank line cannot OOM you: the cap fires at 64 KB of headers. (If you instead did
   `getline` over an `ifstream`, a single header line of 5 GB would still OOM you —
   that is the trap to avoid; bounded scanning is the safe way.)
2. `body_start` counts from byte 0 of the file and *includes* the terminator, so it is
   both the end of the header block *and* the absolute offset where the body begins.
3. We reuse your friend's public API verbatim. `setFileBody(path)` opens the same inode,
   `fstat`s it (so `getFileSize()` = full file size), and sets its own (wrong, for us)
   `Content-Length` — which we **override** right after. We never trust the script's
   `Content-Length`; we ship exactly the bytes that exist (`file_size - body_start`).
   That also saves you from a script that lies (too-short promise ⇒ client hangs; too-long
   promise ⇒ protocol desync).
4. `_cgi_body_offset` is where the body actually starts. **This is the offset you
   "remember"** — the friend's `_sendData()` sends headers first, then
   `sendfile(sock, fd, &_file_offset, …)`. Seeding `_file_offset` here means the very
   first body chunk skips the headers.
5. `_file_offset` is also pre-seeded so the header-sending phase (which does not use it)
   never overwrites it. `_sendData()` needs **zero changes**.
6. Empty body ⇒ `setBody("")` ⇒ `_has_file == false` ⇒ `_sendData()` ends right after
   headers (the existing path). No `sendfile` of zero bytes needed.

### 6.4 Header parser helpers (new private functions in Client.cpp)

```cpp
bool Client::_parseCgiHeaders(const std::string &block,
                              HttpStatus::Code &status, bool &explicit_status,
                              std::map<std::string, std::string> &headers,
                              bool &has_location) {
    size_t start = 0;
    while (start < block.size()) {
        size_t nl = block.find('\n', start);
        if (nl == std::string::npos)
            nl = block.size();

        std::string line = block.substr(start, nl - start);
        start = nl + 1;

        if (!line.empty() && line[line.size() - 1] == '\r')   // CRLF -> drop the CR
            line.erase(line.size() - 1);

        if (line.empty())            // blank line = end of the CGI header block
            break;

        if (line.find('\r') != std::string::npos)    // interior CR = injection
            return false;

        if (line.compare(0, 5, "HTTP/") == 0) {      // NPH-style full status line
            if (!_extractStatusLine(line, status))
                return false;
            explicit_status = true;
            continue;
        }

        if (_lower(line).compare(0, 7, "status:") == 0) {
            if (!_parseStatusNumber(line.substr(7), status))
                return false;
            explicit_status = true;
            continue;
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0)
            return false;                            // missing ':' or empty name

        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        for (size_t i = 0; i < name.size(); ++i) {   // strict token validation
            char c = name[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!ok)
                return false;
        }

        size_t b = value.find_first_not_of(" \t");
        if (b == std::string::npos)
            value.clear();
        else {
            size_t e = value.find_last_not_of(" \t");
            value = value.substr(b, e - b + 1);
        }
        for (size_t i = 0; i < value.size(); ++i)    // no embedded control bytes
            if ((unsigned char)value[i] < 32 && value[i] != '\t')
                return false;

        if (_lower(name) == "location")
            has_location = true;
        headers[name] = value;                       // store with original case
    }
    return true;
}

bool Client::_extractStatusLine(const std::string &line, HttpStatus::Code &status) {
    size_t sp = line.find(' ');
    if (sp == std::string::npos)
        return false;
    return _parseStatusNumber(line.substr(sp + 1), status);
}

bool Client::_parseStatusNumber(const std::string &s, HttpStatus::Code &status) {
    size_t i = s.find_first_not_of(" \t");
    if (i == std::string::npos)
        return false;
    int code = 0;
    for (; i < s.size() && std::isdigit((unsigned char)s[i]); ++i) {
        code = code * 10 + (s[i] - '0');
        if (code > 999)
            return false;
    }
    if (code < 100)
        return false;
    status = static_cast<HttpStatus::Code>(code);
    return true;
}

std::string Client::_lower(const std::string &s) const {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(::tolower((unsigned char)out[i]));
    return out;
}
```

Why each guard exists:

- The `line.find('\r')` check comes **after** the trailing-CR trim, so a legit CRLF line
  passes while `value\r\nX: y` fails — that is your CRLF-injection fence (2.7).
- Header-names are validated against a tiny token alphabet. Anything else (spaces,
  control bytes, extra colons) means an attacker/wrong script → `BadGateway`.
- `Status:` is consumed as a directive, not stored. `Location:` is stored too (it is a
  real header to forward) but also flips `has_location` so the default 302 applies.
- Values are trimmed of *surrounding* whitespace (HTTP trims OWS) but are otherwise byte
  verbatim — a browser gets exactly what the script meant.

### 6.5 The two functions that already call into these (unchanged but worth re-reading)

`_handleCgiEvent()` (Client.cpp:173) already does the right thing:

```cpp
Epoll::EventState Client::_handleCgiEvent() {
    Cgi::e_out result = _cgi->readOutput();
    if (result == Cgi::MORE)
        return Epoll::ECONTINUE;

    _epoll.del_fd(_cgi->getReadEnd());
    if (result == Cgi::FAIL || !_cgi->exitedCleanly()) {
        delete _cgi; _cgi = NULL;
        _buildError(HttpStatus::BadGateway);
    } else {
        _buildCgiResponse();     // now complete
        delete _cgi; _cgi = NULL;
    }
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);
    return Epoll::ECONTINUE;
}
```

Note the ordering subtlety: `_buildCgiResponse()` re-opens the temp file *by path* into
`HttpResponse` **before** `delete _cgi` unlinks the path. Two moments matter:

- While `setFileBody()` runs, the path must still exist → it does, `_cgi` still alive.
- After `delete`, the path is gone but the *inode* survives because the response holds an
  open fd → `sendfile()` still works, and when `_reset()`/destructor closes that fd the
  disk space is reclaimed. Nothing leaks, nothing lingers.

`startCgi()` and `_cgiTimeout()` and `handleTimeout()` and `_sendData()` are unchanged.

### 6.6 `_sendData()` — prove to yourself it needs no edits

Trace the CGI case through the existing code (`src/network/Client.cpp:57`):

1. `CSENDING_HEADERS`: `send()` the `_response.getHeaderBuffer()` bytes (the response now
   carries the CGI status line + headers + `Content-Length` we computed + blank line).
2. Headers done, `hasFile()` is true (we called `setFileBody`) → `_file_offset` was already
   seeded at `body_start`, `m_state = CSENDING_BODY`.
3. `CSENDING_BODY`: `sendfile(m_fd, getFileFd(), &_file_offset, APP_BUFFER_SIZE)` starts at
   the body offset, walks forward across partial sends, and finishes when
   `_file_offset == getFileSize()`.
4. `CFINISHED` → keep-alive resets everything (`_response.reset()` closes the temp fd →
   inode freed), or connection ends.

The only subtraction you did was: **you planted `_file_offset` instead of leaving it 0.**
That single seed is the bridge between "headers were bytes 0..N" and "body is bytes
N..end".

---

## 7. The bugs you were carrying (fix them while you're in here)

| # | Where | Bug | Fix |
|---|-------|-----|-----|
| 1 | `Cgi` ctor | `_body_fd(-1)` ignores the argument | `_body_fd(body_fd)` |
| 2 | `_setEnv` | `getVersion()` pushed as a bare `"HTTP/1.1"` env entry | `"SERVER_PROTOCOL=" + getVersion()` |
| 3 | `_setEnv` | `SCRIPT_FAILENAME` | `SCRIPT_FILENAME` |
| 4 | Makefile | `Cgi.cpp` not compiled | add `./src/cgi/Cgi.cpp \` to `SRC` |
| 5 | `Client` ctor | `_cgi`, `_cgi_start` uninitialized | init to `NULL` / `0` |
| 6 | `Cgi` | `_outputPipe[0]` never closed (fd leak/request) | close both fds in dtor |
| 7 | `Cgi.hpp` | `_reap()` declared, never implemented | remove the declaration |
| 8 | `Cgi` | `readOutput()` grew `_buffer` (RAM) | file-based, bounded parse |

---

## 8. Integration contract with your friend (read, then send him this)

Your friend's promised handoff: **"request is CGI" + `interpreter` + `script_path` +
`body_fd`.** Concretely, he must:

1. In the parsing/config layer, implement "is this URI a CGI hit" using
   `_route->cgi_pass`: the extension of `real_path` is a key in
   `_route->cgi_pass` → value is the interpreter. Return `interpreter`, `script_path`
   (`_resolvePath()` output), and `body_fd` (an open `O_RDONLY` fd to the request body,
   or `-1` when there is no body; he must persist the body to a temp file because
   `execve`'s stdin needs a real fd).
2. Hand those three values over to the event loop **instead of calling `handle()`** and
   building a normal response. The seam is already sketched in your `_receiveData()`:

```cpp
    if (_request.getState() == HttpRequest::COMPLETE || _request.getState() == HttpRequest::ERROR) {
        const ServerConfig &conf = *_getConfig(_request.getHeader("host"));
        // friend's detection:
        //   if (cgi-hit) {
        //       return startCgi(interpreter, script_path, body_fd) == 0
        //              ? Epoll::ECONTINUE : handle-the-error;
        //   }
        RequestHandler rqst_handler(_request, _response, conf);
        rqst_handler.handle();
        ...
    }
```

3. He must NOT `close()` `body_fd` — ownership moves to `Cgi`, which closes it in
   `execute()` after the fork.
4. He must pass an **absolute** `script_path` (your child `chdir()`s to the script's
   directory before `execve`, so a relative path would resolve against the script dir —
   fragile and a small security smell).

---

## 9. Makefile (one line)

In `Makefile`'s `SRC`, after the network files, add:

```
./src/cgi/Cgi.cpp \
```

Rebuild with `make re`. (Your objects are stale anyway — this file was never compiled.)

---

## 10. Edge cases — your checklist

| Situation | What your code does |
|-----------|---------------------|
| Empty stdout / no blank line | 502 (header cap or missing terminator). |
| Huge body (100 MB binary) | streamed to file by the kernel; body sent by `sendfile`, constant RAM. |
| Huge/Never-ending headers | 502 at the 64 KB cap; no OOM. |
| Script exits 1 / SEGV / exec fails | `waitpid` status ≠ clean → 502. |
| Script hangs / writes nothing / never exits | setTimeout kills via `_cgiTimeout` → 504. |
| Script outputs LF-only (`\n\n`) | terminator matcher accepts both; parser trims trailing `\r`. |
| `Status: 404 Not Found` | 404 status line, header not forwarded. |
| `Location:` only | 302 Found (CGI/1.1 default). |
| Script supplies `Content-Length` | ignored; we send the true `file_size - body_offset`. |
| Script lies (too long/short body) | we send exactly the bytes that exist. |
| CRLF injection in a header value | rejected → 502. |
| Empty body | `setBody("")` → `Content-Length: 0`, headers-only response. |
| Client disconnects mid-body | sendfile error → existing error path; temp inode freed on `_reset`/dtor. |
| Keep-alive after a CGI | `_reset()` closes the temp fd → disk reclaimed; temp path already unlinked. |
| Two concurrent CGI on same Client | impossible: one Client = one `_cgi`; epoll is level-triggered, events serialized. |
| Script closes stdout but keeps running | notify-pipe EOF → WNOHANG misses → SIGKILL → reap (old behavior, preserved). |
| Grandchild inherits stdout | correct: file stays "in progress" until the last writer exits; timeout saves you. |

---

## 11. Prove you own it (answer these out loud before you type a line)

1. Why can we not just keep growing `_buffer`? What is the failure mode if a tester asks
   for a 2 GB CGI output? (Answer: one `std::string` holding the whole output ⇒ RAM
   exhaustion, no partial-progress, kill by OOM / MallocSafety.)
2. Where does the script's output *actually* land before you read it? (Page cache of the
   tmp file, written by the kernel from the child — no user-space copy.)
3. A regular file can't be `epoll`-ed. How do you know "the child is done" then? (Notify
   pipe EOF == every write-end holder died == stdout file frozen.)
4. Why did the notify-pipe write end have to survive `execve`? What would happen with
   `FD_CLOEXEC`? (EOF would fire at exec ⇒ we'd think the script finished before it ran.)
5. What is `body_start` exactly, in bytes? Why is it both a header-end *and* a body-offset?
   (Number of bytes consumed through the blank line, byte 0 = file start.)
6. How does `_sendData()` know to start the body at an offset? (You seeded `_file_offset`
   in `_buildCgiResponse`; `sendfile`'s `&_file_offset` starts there.)
7. Why do you override the CGI's `Content-Length`? (Trust no one: the file is the truth.
   A lying header guarantees a hang or a desync.)
8. Why can `setFileBody` open the file even though `Cgi::~Cgi` unlinks it right after?
   (Inode stays alive while any fd is open; `unlink` only removes the name.)
9. After the response finishes (keep-alive), where does the temp file go? (Unlinked, fd
   closed by `_response.reset()` → inode freed → no litter.)
10. Two scripts run at once on two clients — what RAM does each use? (The 64 KB header
    window + whatever the kernel's page cache holds. Not the body.)

---

## 12. Test plan (prove it works, then prove it scales)

1. `hello.py` as-is → browser shows `<h1>Hello from CGI</h1>`, `curl -i` shows
   `HTTP/1.1 200 OK`, `Content-Type: text/html`, correct `Content-Length`.
2. Make a script that prints `Status: 404 Not Found` + a body → `curl -i` shows 404.
3. Make a script that prints only `Location: /index.html` → status 302, header present.
4. Make a script that prints `\n` style headers only → still parsed (LF terminator).
5. Make a script that spams headers with no blank line → your server answers 502 fast.
6. Make a script that spams `X-Evil: 1\r\nContent-Length: 999999` inside a value → 502.
7. **Scale**: `sys.stdout.write("A" * 500_000_000); (can also be binary)` → watch
   `RSS` of your server with `cat /proc/<pid>/status` / `/usr/bin/time -v ./webserv conf`
   — it must stay flat while `curl` downloads the whole payload.
8. **Fd leaks**: `ls -l /proc/<server-pid>/fd | wc -l` across many requests — must not
   grow per CGI (the dtor now closes the notify pipe and the tmp fd).
9. **Temp litter**: `ls /tmp/webserv_cgi_*` after a busy run, including a
   mid-response disconnect → empty.
10. Timeout: `while True: pass` in the script → 504 Gateway Timeout after ~16 s, and the
    server stays alive.

---

## 13. The one-line summary you should tell your evaluator

> *CGI output never touches server RAM: the child's stdout is `dup2`-ed onto a `mkstemp`
> file, a notification pipe tells epoll when the child is done, I parse only the bounded
> header block into a map, remember the body offset, override `Content-Length` with the
> real file-derived size, and stream the body with `sendfile` from that exact offset —
> reusing the same partial-send loop that serves static files.*