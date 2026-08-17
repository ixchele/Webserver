# CGI — the network side, explained end to end

This file is your personal study guide + code reference for the **CGI part of the
network layer**. Your teammate tells you "*this request is CGI*" (they detect the
extension / `cgi_pass` on the request side). Your job starts there:

1. spawn the interpreter as a **child process**,
2. give it the standard CGI **environment variables**,
3. **read whatever it writes to stdout** through a pipe, without ever blocking the server,
4. track it, **time it out** if it is slow or hangs,
5. **turn its raw bytes into a full HTTP response** and hand it to the normal send path.

Everything below respects your constraints: **no POST**, **no request-side CGI
detection**. It only covers *execution → output → response*.

---

## 1. The big picture

Your server is **single-threaded and event-driven** (one `epoll_wait` loop in
`Multiplexer::events_loop()`). A CGI script is a *separate process* that can run for
seconds. **You can never sit and wait for it**, or the whole server freezes and every
other client times out.

The trick: the CGI script is a **child process** talking to you through an **anonymous
pipe**. Its `stdout` is the pipe's *write end*. You put the pipe's *read end* into
`epoll` like any other fd. While the script runs, your event loop keeps serving everyone
else. When the script writes something, `epoll` wakes you up, you drain the bytes, and
go back to the loop.

```
your server (one process, one epoll loop)
   │
   │  fork()
   ▼
 +--------+   pipe(_outputPipe)   +---------------+
 | server |  _outputPipe[1]  ───►  child process   |
 | parent |       (child stdout)  | (python3, etc) |
 +--------+                       +---------------+
   │  ▲
   │  │  _outputPipe[0]  (read end)
   │  │  registered in epoll as EPOLLIN
   └──┘
```

> Mental rule: **a CGI request is just a client whose response source is a pipe
> instead of a file on disk.** Before CGI your `Client` read from a socket and built a
> response. Now it "reads from a process" and still builds the same response.
> Everything you already know about sending (`Client::_send_data`) is unchanged.

---

## 2. The syscalls you must truly understand

| syscall | what it does | why CGI needs it |
|---|---|---|
| `pipe(fds)` | creates two connected fds: `fds[1]` written → appears at `fds[0]` | child's stdout goes into the pipe, parent reads it from the other end |
| `fork()` | clones your process. Return value is `0` in the **child**, the child's PID in the **parent**, `-1` on failure | creates the CGI process |
| `dup2(a, b)` | copies fd `a` into fd `b` (closing `b` first) | make the script's `stdout` **be** the pipe write end |
| `execve(path, argv, envp)` | *replaces* the current program with another one. Returns only on failure | turns the cloned child into `python3` (or any interpreter) |
| `waitpid(pid, &st, flags)` | gets the child's exit status. `WNOHANG` returns `0` if still running | **reap zombies** and read exit code |
| `kill(pid, SIGKILL)` | hard-kills a process | CGI timeout: kill the script |
| `fcntl(fd, F_SETFL, O_NONBLOCK)` | makes reads on the fd non-blocking | never let a read stall the event loop |
| `_exit(code)` | exits without touching stdio buffers | child-side "exec failed" exit (see §8) |

### fork() — the memory picture

`fork()` copies the parent. Both processes continue running the **exact same code** from
the same line. The only difference at first is the return value:

```cpp
pid_t pid = fork();
if (pid == 0) {
    // I am the CHILD. pid == 0 here.
    // My job: turn into the interpreter and die when done.
} else if (pid > 0) {
    // I am the PARENT. pid == child's PID here.
    // My job: read the pipe until EOF, then waitpid().
} else {
    // fork failed. errno explains why.
}
```

> The child is a **copy** of the parent. That means it also inherits every fd the server
> has open (listen sockets, client sockets, the epoll fd...). You must close the fds the
> child is not allowed to touch or keep. This is covered in §5.

### The pipe contract

A pipe is a byte stream. There is **no message boundary** — bytes are concatenated.
**EOF is only delivered when `every` write end is closed.** This single fact drives the
whole design:

* the child must dup its stdout onto `_outputPipe[1]` and close its own copy of
  `_outputPipe[0]` and `_outputPipe[1]` so that when the child *exits*, the write end is
  gone → the read end returns `0` (EOF) → you know the script is done.
* the **parent** must close its copy of `_outputPipe[1]` **immediately** after fork, for
  the same reason — if the parent kept the write end open, EOF would never arrive.

### waitpid / zombies

When a child exits it becomes a **zombie** (a placeholder in the process table) until
somebody `waitpid`s it. If you never do, zombies pile up. Worse, if the parent exits,
the child is *orphaned*. So:

* at EOF → `waitpid(pid, &st, 0)` (blocking is safe here, the child is about to die;
  the pipe already returned EOF, so there is no deadlock).
* on timeout → `kill(pid, SIGKILL)` then `waitpid(pid, &st, 0)`.

`WNOHANG` ("wait no hang") is for the case where you want to check without blocking:
`waitpid(pid, &st, WNOHANG)` returns `0` if the child is still alive.

---

## 3. The full journey of one CGI request (step by step)

1. Your teammate detects the request is CGI and calls your network-side function
   `Client::start_cgi(interpreter, script_path)`. (`interpreter` comes from
   `cgi_pass` in the config, e.g. `/usr/bin/python3`; `script_path` is the resolved
   path of the `.py` file on disk.)
2. `start_cgi` constructs a `Cgi` object. The constructor fills the **environment
   vector** (§6) and builds `argv = { "/usr/bin/python3", "/path/script.py", NULL }`.
3. `execute()`:
   - `pipe(_outputPipe)`,
   - `fork()`,
   - **child**: bind stdout→pipe write end, stdin→/dev/null, stderr→/dev/null, close
     everything unused, `execve(...)`; if it returns, `_exit(127)`,
   - **parent**: close write end, set `_outputPipe[0]` non-blocking, remember start
     time, register `_outputPipe[0]` in epoll (pointer = the **Client**, §7).
4. `Client` is now in state `CEXECUTING_CGI`. The event loop keeps running and serving
   every other client.
5. `epoll` fires `EPOLLIN` on the pipe read end → `Multiplexer` calls
   `Client::handle_event(...)` (the pointer-trick) → Client sees its state is
   `CEXECUTING_CGI` → calls `_cgi->handle_event(...)`:
   - bytes available → `read()` them, append to `_buffer`, return `ECONTINUE` (keep
     waiting for more),
   - `read()` returns `0` → EOF → `waitpid()` the child → return `EFINISHED`.
6. Client removes the pipe fd from epoll, asks the `Cgi` object for its exit status and
   its raw bytes, then builds the HTTP response from them (§7 "parsing").
7. `m_state = CSENDING_HEADERS`, re-arm the client socket for `EPOLLOUT`. From here on
   your existing `_send_data()` does everything (the body is already inside the
   response header buffer — `HttpResponse::build()` puts headers *and* body in one
   string).
8. If the script is alive and silent after `CGI_TIMEOUT` (16 s, already defined in
   `Multiplexer.hpp`), the server `kill`s it, `waitpid`s it, and answers `504 Gateway
   Timeout`.

---

## 4. Cgi.hpp — the interface

```cpp
#ifndef CGI_HPP
# define CGI_HPP

# include <HttpRequest.hpp>
# include <Epoll.hpp>
# include <AFd.hpp>
# include <string>
# include <vector>
# include <ctime>

# define PIPE_BUFFER_SIZE 65536  // how many bytes we read from the pipe per call

class Cgi : public AFd
{
public:
    Cgi(HttpRequest &request, const std::string &script_path,
        const std::string &interpreter);
    ~Cgi();

    int   execute();                  // 0 ok, -1 fail (fork/pipe failed)
    int   get_pid() const;

    bool  exited_cleanly() const;     // WIFEXITED && exit code 0
    time_t started_at() const;        // for the timeout logic

    const std::string &get_output() const;  // whatever the script wrote to stdout

    void  kill_child();               // SIGKILL + waitpid (called on timeout / abort)

    virtual Epoll::EventState handle_event(uint32_t event);

private:
    HttpRequest            &_request;      // must outlive the CGI (the Client owns it)
    std::string             _script_path;
    std::string             _interpreter;

    int                     _outputPipe[2];
    int                     _status;       // waitpid status
    pid_t                   _pid;
    bool                    _reaped;
    time_t                  _start;

    std::string             _buffer;               // accumulated stdout
    std::vector<std::string> _env;                 // owns every string
    std::vector<const char *> _cenv;               // points into _env strings, + NULL terminator
    std::vector<const char *> _cargv;              // { interpreter, script, NULL }

    void _set_environment();
    void _set_argv();
    void _reap();                                 // waitpid, marks _reaped
};

#endif
```

Notes:

* `Cgi` **is an `AFd`**. It inherits `m_fd`, `get_fd()`, `get_type()` (which must be
  `AFd::CGI` — the enum already exists) and the virtual `handle_event`.
* `HttpRequest &_request` is a **reference** because the Client already owns it and it
  must survive the CGI. Do **not** copy the request.
* `_cenv` (the `char **envp` array) must be terminated by `NULL`. `execve` reads it
  until it finds `NULL`.

---

## 5. Cgi.cpp — the implementation (study this carefully)

### 5.1 Constructor + argv + env

```cpp
#include <Cgi.hpp>
#include <Logger.hpp>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

Cgi::Cgi(HttpRequest &request, const std::string &script_path,
         const std::string &interpreter)
    : AFd(-1, AFd::CGI),
      _request(request),
      _script_path(script_path),
      _interpreter(interpreter),
      _status(0),
      _pid(-1),
      _reaped(false),
      _start(0)
{
    _set_argv();
    _set_environment();
}
```

```cpp
void Cgi::_set_argv()
{
    // execve wants: argv[0]=the program you run, argv[1..]=its arguments.
    // For "python3 script.py" that means { "/usr/bin/python3", "script.py", NULL }.
    _cargv.push_back(_interpreter.c_str());
    _cargv.push_back(_script_path.c_str());
    _cargv.push_back(NULL);
}
```

> **Why `argv[0]` is the interpreter, not the script.** `cgi_pass .py /usr/bin/python3`
> means "execute `.py` files with `/usr/bin/python3`". So `execve` runs
> `/usr/bin/python3` and passes the script as its first argument. You could also rely on
> the script's shebang line, but then the interpreter comes from the file itself, which
> is fragile. Passing it explicitly (what nginx/Apache do) is cleaner and matches your
> config model.

### 5.2 Environment variables (CGI/1.1)

CGI/1.1 defines a fixed list of variables a server must export. The rule for headers:
`Content-Type:` becomes `CONTENT_TYPE`, `Host:` becomes `HTTP_HOST`, every `-` becomes
`_` and letters are uppercased. **Request-specific CGI variables always win** over
`HTTP_CONTENT_TYPE` style duplicates, so those two are special-cased.

```cpp
void Cgi::_set_environment()
{
    // --- variables required by CGI/1.1 (GET only — no POST handling here) ---
    _env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    _env.push_back("SERVER_PROTOCOL=HTTP/1.1");
    _env.push_back("SERVER_SOFTWARE=webserv/1.0");
    _env.push_back("SERVER_NAME=" + _request.getHeader("host")); // from Host: header
    _env.push_back("SERVER_PORT=80");                            // from config, or parse Host:
    _env.push_back("REQUEST_METHOD=" + _method_to_string(_request.getMethod()));
    _env.push_back("REQUEST_URI=" + _request.getUri().getOriginal());
    _env.push_back("SCRIPT_NAME=" + _request.getUri().getPath());
    _env.push_back("SCRIPT_FILENAME=" + _script_path);
    _env.push_back("QUERY_STRING=" + _request.getUri().getQuery());

    // content-length / content-type are *CGI* variables, not HTTP_* ones.
    // We forward them only if the request actually carried them.
    if (_request.getHeader("content-length") != "")
        _env.push_back("CONTENT_LENGTH=" + _request.getHeader("content-length"));
    if (_request.getHeader("content-type") != "")
        _env.push_back("CONTENT_TYPE=" + _request.getHeader("content-type"));

    // --- every other request header becomes an HTTP_* variable ---
    std::map<std::string, std::string>::const_iterator it;
    for (it = _request.getHeaders().begin(); it != _request.getHeaders().end(); ++it)
    {
        const std::string &name  = it->first;
        const std::string &value = it->second;

        if (name == "content-length" || name == "content-type")
            continue; // already set above, and CGI forbids HTTP_CONTENT_* duplicates

        std::string var = "HTTP_";
        for (size_t i = 0; i < name.size(); ++i)
        {
            char c = name[i];
            var += (c == '-') ? '_' : static_cast<char>(::toupper(c));
        }
        _env.push_back(var + "=" + value);
    }

    // --- THEN build the char** table. NEVER build it while you are still
    //     pushing to _env (see §8: the dangling-pointer trap) ---
    for (size_t i = 0; i < _env.size(); ++i)
        _cenv.push_back(_env[i].c_str());
    _cenv.push_back(NULL);
}
```

And the tiny helper:

```cpp
std::string Cgi::_method_to_string(HttpRequest::Method m)
{
    switch (m) {
        case HttpRequest::HTTP_GET:    return "GET";
        case HttpRequest::HTTP_POST:   return "POST";
        case HttpRequest::HTTP_DELETE: return "DELETE";
        default:                       return "GET";
    }
}
```

> `HttpRequest::getHeaders()` already stores **lowercased** keys, so no case conversion
> is needed on the input, only `-` → `_`.

### 5.3 execute() — the heart of it

```cpp
int Cgi::execute()
{
    if (pipe(_outputPipe) == -1)
    {
        LOG_ERROR << "pipe() in Cgi::execute() -> " << strerror(errno);
        return -1;
    }

    _pid = fork();
    if (_pid == -1)
    {
        LOG_ERROR << "fork() in Cgi::execute() -> " << strerror(errno);
        (void)close(_outputPipe[0]);
        (void)close(_outputPipe[1]);
        return -1;
    }

    if (_pid == 0)
    {
        /* ---------------- CHILD ---------------- */

        // We never read from the pipe: close the read end so that when this
        // process dies, the write end is the only end left -> parent sees EOF.
        (void)close(_outputPipe[0]);

        // stdout -> pipe write end. Now "print()" in python flows to the server.
        (void)dup2(_outputPipe[1], STDOUT_FILENO);
        (void)close(_outputPipe[1]);

        // stdin <- /dev/null (GET has no body to forward in your scope)
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1)
        {
            (void)dup2(devnull, STDIN_FILENO);
            (void)close(devnull);
        }

        // stderr -> /dev/null so a chatty script can't block on a full pipe.
        // (During development, redirect it to a log file instead.)
        devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1)
        {
            (void)dup2(devnull, STDERR_FILENO);
            (void)close(devnull);
        }

        // Replace this process image with the interpreter.
        // execve() returns ONLY on failure (with errno set).
        execve(_cargv[0], (char *const *)&_cargv[0], (char *const *)&_cenv[0]);

        LOG_ERROR << "execve() of " << _interpreter << " failed: " << strerror(errno);
        _exit(127);   // 127 = "command not found", the script never ran
    }

    /* ---------------- PARENT ---------------- */

    // CRITICAL: close the write end NOW, or the read end will never see EOF.
    (void)close(_outputPipe[1]);

    // Non-blocking reads so the event loop is never stalled by a slow script.
    if (fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK) == -1)
    {
        LOG_ERROR << "fcntl() in Cgi::execute() -> " << strerror(errno);
        kill_child();
        return -1;
    }

    m_fd = _outputPipe[0];      // AFd's fd is now the pipe read end
    _start = time(NULL);
    _reaped = false;
    return 0;
}
```

The 5 tiny rules of the child block, memorized:

1. close the pipe **read** end in the child,
2. dup2 the pipe **write** end over **stdout**,
3. close the original pipe write end (otherwise two writers → EOF never comes),
4. get sensible stdin/stderr,
5. if `execve` returns → `_exit(127)`. (Never fall through: after exec failure the
   child would keep "running" the server's code and corrupt shared things.)

### 5.4 handle_event() — reading the output

```cpp
Epoll::EventState Cgi::handle_event(uint32_t event)
{
    (void)event;   // epoll only calls us because EPOLLIN happened

    char chunk[PIPE_BUFFER_SIZE];
    int reads = 0;

    while (true)
    {
        ssize_t n = read(m_fd, chunk, sizeof(chunk));

        if (n > 0)
        {
            _buffer.append(chunk, static_cast<size_t>(n));
            // Cap the number of reads per event so a flooding script cannot
            // starve the rest of the server (fairness for the single loop).
            if (++reads >= 64)
                return Epoll::ECONTINUE;  // more data pending, come back later
            continue;
        }

        if (n == 0)
        {
            // EOF: the child closed its stdout (i.e. it exited, or will any instant).
            _reap();
            return Epoll::EFINISHED;      // buffer is complete, reaped, all done
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return Epoll::ECONTINUE;      // no data right now, keep waiting

        if (errno == EINTR)
            continue;                     // interrupted by a signal, just retry

        LOG_ERROR << "read() on cgi fd " << m_fd << " -> " << strerror(errno);
        return Epoll::EERROR;
    }
}
```

> **Use `read()`, not `recv()`.** The existing `src/cgi/Cgi.cpp` calls `recv()` on a
> pipe — `recv()` only works on *sockets* and returns `ENOTSOCK`. This is a bug in the
> current skeleton. Always `read()` the pipe.

```cpp
void Cgi::_reap()
{
    if (_pid <= 0 || _reaped)
        return;
    int ret;
    do
    {
        ret = waitpid(_pid, &_status, 0);   // blocking; safe here: EOF already happened
    } while (ret == -1 && errno == EINTR);
    _reaped = true;
    (void)ret;
}
```

```cpp
bool Cgi::exited_cleanly() const
{
    return _reaped && WIFEXITED(_status) && WEXITSTATUS(_status) == 0;
}
```

```cpp
void Cgi::kill_child()
{
    if (_pid > 0 && !_reaped)
    {
        (void)kill(_pid, SIGKILL);
        _reap();
    }
}
```

```cpp
int Cgi::get_pid() const          { return _pid; }
time_t Cgi::started_at() const    { return _start; }
const std::string &Cgi::get_output() const { return _buffer; }

Cgi::~Cgi()
{
    // If something went wrong while the child was still alive (e.g. the client
    // disconnected mid-CGI), never leave an orphan running.
    kill_child();
}
```

> The `Cgi` destructor calling `kill_child()` is your safety net: **whichever path
> deletes the `Cgi`, the child is killed and reaped.** No zombie, no orphan process.

---

## 6. `Client::start_cgi()` — the seam your teammate calls

This is the public entry point of the network side. Your teammate's code (whoever
detects "is CGI") calls this with the interpreter and the resolved script path.

Add to `Client.hpp`:

```cpp
// public:
int  start_cgi(const std::string &interpreter, const std::string &script_path);

// private:
Cgi                  *_cgi;      // NULL while no CGI is running
time_t                _cgi_start;
Epoll::EventState     _handle_cgi_event(uint32_t event);
void                  _build_cgi_response();   // parse raw bytes -> HttpResponse
void                  _build_cgi_error(HttpStatus::Code code);
void                  _cleanup_cgi();          // delete _cgi, set _cgi = NULL
```

```cpp
int Client::start_cgi(const std::string &interpreter, const std::string &script_path)
{
    _cgi = new Cgi(_request, script_path, interpreter);
    _cgi_start = time(NULL);

    if (_cgi->execute() != 0)
    {
        delete _cgi;
        _cgi = NULL;
        _build_cgi_error(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
        return -1;
    }

    m_state = CEXECUTING_CGI;

    // THE POINTER TRICK: register the PIPE fd, but store "this" (the Client)
    // as the epoll data pointer. No change needed in the Multiplexer dispatch:
    // events on the pipe simply call Client::handle_event(), which routes by state.
    if (_epoll.add_fd(_cgi->get_fd(), this, EPOLLIN) != 0)
    {
        _cgi->kill_child();
        delete _cgi;
        _cgi = NULL;
        _build_cgi_error(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
        return -1;
    }
    return 0;
}
```

Why this works: `Epoll::add_fd(int fd, AFd *ptr, int events)` just stores `ptr` in
`ev.data.ptr`. The kernel does **not** care that `fd` and `ptr` are unrelated — you are
free to point a pipe fd at a Client object. The Multiplexer's existing line
`fdObj = static_cast<AFd *>(events[i].data.ptr); fdObj->handle_event(...)` continues to
"just work" for CGI events, and the CLIENT-type cleanup (list tracking, activity refresh,
timeout bookkeeping) applies to this Client as well. One epoll entry, one AFd pointer,
zero Multiplexer dispatch changes.

### Client::handle_event dispatches by state

```cpp
Epoll::EventState Client::handle_event(uint32_t event)
{
    if (m_state == CEXECUTING_CGI)
        return _handle_cgi_event(event);   // events from the PIPE land here

    if (event & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        return Epoll::EERROR;

    if (event & EPOLLIN)
    {
        m_state = CRECEVING;
        return _receive_data();
    }

    if (event & EPOLLOUT)
        return _send_data();

    return Epoll::EFINISHED;
}
```

### Client::_handle_cgi_event

```cpp
Epoll::EventState Client::_handle_cgi_event(uint32_t event)
{
    if (event & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
    {
        // Pipe died abnormally. Clean up and deliver 500.
        _epoll.del_fd(_cgi->get_fd());
        _cgi->kill_child();
        delete _cgi;
        _cgi = NULL;
        _build_cgi_error(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
        return Epoll::ECONTINUE;
    }

    Epoll::EventState cgi_state = _cgi->handle_event(event);

    if (cgi_state == Epoll::ECONTINUE)
        return Epoll::ECONTINUE;          // still running / more data pending

    // EFINISHED or EERROR: the pipe is finished. Unlink it from epoll first,
    // otherwise epoll keeps a pointer to a soon-deleted object.
    _epoll.del_fd(_cgi->get_fd());

    if (cgi_state == Epoll::EERROR)
        _build_cgi_error(HttpStatus::InternalServerError);
    else if (_cgi->exited_cleanly())
        _build_cgi_response();            // parse bytes -> HttpResponse
    else
        _build_cgi_error(HttpStatus::BadGateway);   // script crashed / exit != 0

    _cgi->kill_child();   // no-op if already reaped; safety
    delete _cgi;
    _cgi = NULL;

    m_state = CSENDING_HEADERS;
    _epoll.edit_fd(m_fd, this, EPOLLOUT);   // back to the normal send path
    return Epoll::ECONTINUE;                // IMPORTANT: not EFINISHED, the
                                            // client itself still has to send!
}
```

> **Why the final `return ECONTINUE` is mandatory.** If you returned `EFINISHED`, the
> Multiplexer would `del_fd(get_fd())` and `delete fdObj` — i.e. it would close and
> destroy the **client socket** too. But you want to *keep* that client and send the CGI
> response back. The pipe fd was already unregistered and its object freed; the client
> lives on to respond.

### Cleanup / destructor

Client destructor currently is empty. Add the CGI safety net:

```cpp
Client::~Client()
{
    if (_cgi)   // client disconnected or deleted while CGI was running
    {
        _epoll.del_fd(_cgi->get_fd());
        delete _cgi;     // Cgi::~Cgi() kills + reaps the child
        _cgi = NULL;
    }
}
```

---

## 7. Building the HTTP response from CGI output

The script writes to stdout. With a proper CGI script, that output looks like this:

```
Content-Type: text/html\r\n
\r\n
<html><body>Hello</body></html>
```

Rules you implement:

* the header section ends at the first **empty line** (`\r\n\r\n` or `\n\n`),
* a line starting with `Status: ` contains the HTTP status code,
* any other `Name: value` line is a header you forward,
* the rest (after the empty line) is the body,
* **you** recompute `Content-Length` from the bytes you actually received — never
  trust/keep the script's own `Content-Length` (it may be wrong or absent).

```cpp
void Client::_build_cgi_response()
{
    const std::string &raw = _cgi->get_output();

    HttpStatus::Code       code = HttpStatus::OK;
    std::map<std::string, std::string> headers;
    std::string body;
    bool has_status = false;

    // Locate the end of the header block.
    size_t sep = raw.find("\r\n\r\n");
    if (sep == std::string::npos)
        sep = raw.find("\n\n");

    std::string head, tail;
    if (sep == std::string::npos)
    {
        head = raw;          // no headers at all: everything is body
    }
    else
    {
        head = raw.substr(0, sep);
        size_t body_start = sep + (raw[sep] == '\r' ? 4 : 2);
        body = raw.substr(body_start);
    }

    size_t start = 0;
    while (start < head.size())
    {
        size_t end = head.find("\r\n", start);
        if (end == std::string::npos)
            end = head.size();
        std::string line = head.substr(start, end - start - (end != head.size() ? 2 : 0));
        start = (end == head.size()) ? head.size() : end + 2;

        if (line.empty())
            continue;                        // reached the end of headers

        if (line.compare(0, 7, "Status:") == 0)
        {
            // "Status: 404 Not Found" -> extract the code
            std::string rest = line.substr(7);
            size_t code_start = rest.find_first_not_of(" \t");
            size_t space = rest.find(' ', code_start);
            std::string num = rest.substr(code_start, (space == std::string::npos
                                                      ? std::string::npos : space - code_start));
            code = static_cast<HttpStatus::Code>(std::atoi(num.c_str()));
            has_status = true;
            continue;
        }
        if (line.compare(0, 5, "HTTP/") == 0)
            continue;   // NPH-style script that printed its own status line: skip it

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name  = Utils::trim(line.substr(0, colon));
        std::string value = Utils::trim(line.substr(colon + 1));

        std::string lower = name;
        for (size_t i = 0; i < lower.size(); ++i)
            lower[i] = static_cast<char>(::tolower(lower[i]));

        // We manage these ourselves: never copy them from the script.
        if (lower == "content-length" || lower == "connection"
            || lower == "transfer-encoding")
            continue;

        headers[name] = value;
    }

    _response.setStatusCode(code);
    _response.setHeader("Content-Type", headers.count("Content-Type")
                                        ? headers["Content-Type"] : "text/plain");
    _response.setBody(body);   // <- this sets Content-Length automatically
    _response.build();
}
```

`setBody()` already sets `Content-Length` from the actual body size and `build()`
concatenates status line + headers + body into `_header_buffer`. That means for a CGI
response, `hasFile()` stays `false` — your existing `Client::_send_data()` sends the
whole thing (headers + body) from the header buffer and goes straight to `CFINISHED`.
**No change to the send path needed.** (If a CGI output a `Status: 3xx` + `Location`,
the empty body works too.)

> The error version is trivial: `_build_cgi_error(code)` just calls
> `_buildErrorResponse`-style logic — set status, `Content-Type: text/html`, small body,
> `build()`. (Reuse/adapt the error builder from `RequestHandler` so both sides agree on
> formatting.)

---

## 8. Timeout — wiring it into the Multiplexer

`CGI_TIMEOUT` (16) already exists in `Multiplexer.hpp`. The problem: the existing
`Multiplexer::_handle_timeout()` pops the *front* of the LRU list and treats any stale
client that is not `CRECEVING` by **deleting it outright**. For a client running a CGI
that would mean: socket closed, child process leaked, pipe left in epoll → crash.

Add a dedicated pass *before* the existing loop that scans **every** client for an
overdue CGI. It is O(n) per loop iteration and independent of the LRU ordering (a
script that constantly writes output refreshes `m_lastActivity`, so it would never reach
the front of the LRU list — only a real start-time check catches it).

```cpp
void Multiplexer::_handle_timeout()
{
    time_t now = time(NULL);

    // --- CGI hard timeout: scan ALL clients, not just the LRU front ---
    std::list<Client *>::iterator it = _clientsList.begin();
    while (it != _clientsList.end())
    {
        Client *client = *it;
        if (client->m_state == Client::CEXECUTING_CGI
            && now - client->_cgi_start > CGI_TIMEOUT)
        {
            client->handle_cgi_timeout();          // kills script, builds 504,
                                                   // rearms EPOLLOUT
            std::list<Client *>::iterator cur = it++;
            _clientsList.splice(_clientsList.end(), _clientsList, cur); // to back
            client->m_lastActivity = now;
        }
        else
        {
            ++it;
        }
    }

    // ... your existing front-pop timeout loop stays unchanged below this ...
}
```

And on the Client side:

```cpp
void Client::handle_cgi_timeout()
{
    LOG_WARN << "CGI timeout on client fd " << m_fd;

    _epoll.del_fd(_cgi->get_fd());   // stop watching the pipe FIRST
    delete _cgi;                     // Cgi::~Cgi() kills + reaps the script
    _cgi = NULL;

    _build_cgi_error(HttpStatus::GatewayTimeout);   // 504
    m_state = CSENDING_HEADERS;
    _epoll.edit_fd(m_fd, this, EPOLLOUT);
}
```

> Order of operations is sacred: **`del_fd` (epoll) → delete the `Cgi` (which kills the
> child) → re-arm the client socket.** If you delete the `Cgi` while its pipe fd is
> still registered, the next `epoll_wait` wakes up with a dangling `data.ptr` → crash.

---

## 9. The seven bugs to avoid (a few are already in the skeleton!)

1. **`recv()` on a pipe** — `src/cgi/Cgi.cpp:77` calls `recv()`. Pipes are not sockets;
   `recv()` fails with `ENOTSOCK`. Use `read()`.
2. **Dangling `c_str()` pointers** — `src/cgi/Cgi.cpp:115` stores `var.c_str()` where
   `var` is a local that dies at the end of the loop, *and* pushes to `_env` while
   storing pointers into it (`std::vector` reallocation invalidates the strings'
   buffers). Fix: fill `_env` completely, *then* build `_cenv`.
3. **`exit()` vs `_exit()` in the child** — after a failed `execve` you must use
   `_exit(127)`. `exit()` runs destructors and flushes stdio buffers *of the copy*,
   which can double-write data or hang on locks inherited from the parent.
4. **Zombies** — a child that exited but was never `waitpid`ed stays a zombie. Always
   pair `kill()` with `waitpid()` (the `Cgi` destructor does it).
5. **EOF never arrives** — if the parent keeps the pipe's write end open (or the child
   never closes its original write-end copy), `read()` returns 0 only after `CGI_TIMEOUT`
   kills it. Keep one writer: **parent closes `_outputPipe[1]` right after fork**;
   child dup2's then closes it.
6. **Returning `EFINISHED` from the CGI branch** — deletes the client socket too. Return
   `ECONTINUE` and re-arm `EPOLLOUT` so the response can be sent.
7. **The Multiplexer deletes without CGI cleanup** — the time-out `else`-branch of
   `_handle_timeout()` would `delete client` while the child still runs. The
   `Client::~Client()` CGI safety net (§6) is what saves you there; make sure the
   `handle_cgi_timeout` pass runs before that branch can trigger.

---

## 10. Makefile — add the CGI source (it is currently not compiled!)

`src/cgi/Cgi.cpp` exists but is **missing from the Makefile**'s `SRC` list, so nothing
compiles it. Add:

```makefile
SRC = ./src/ConfigFileParser/Tokenizer/tokenizer.cpp \
	  ./main.cpp \
	  ./src/Logger/Logger.cpp  \
	  ./src/ConfigFileParser/ConfigStructures/CommonConfig.cpp \
	  ./src/ConfigFileParser/ConfigStructures/LocationConfig.cpp \
	  ./src/ConfigFileParser/ConfigStructures/ServerConfig.cpp \
	  ./src/ConfigFileParser/ConfigParser.cpp \
	  ./src/network/AFd.cpp ./src/network/Server.cpp ./src/network/Client.cpp \
	  ./src/network/Multiplexer.cpp ./src/network/Epoll.cpp \
	  ./src/http/HttpRequest.cpp ./src/http/RequestHandler.cpp ./src/http/Uri.cpp \
	  ./src/http/HttpResponse.cpp ./src/http/HttpStatus.cpp \
	  ./src/cgi/Cgi.cpp
```

Watch out: everything is compiled with `-std=c++98 -Wall -Wextra -Werror`. No `nullptr`,
no `auto`, no range-for, no lambdas.

---

## 11. Testing — a tiny CGI script

Create `www/hello.py`:

```python
#!/usr/bin/python3
import sys
sys.stdout.write("Content-Type: text/html\r\n")
sys.stdout.write("\r\n")
sys.stdout.write("<html><body><h1>Hello from CGI!</h1></body></html>")
sys.stdout.flush()
```

`sys.stdout.write` with explicit `\r\n` matters because CGI headers must use CRLF.
Then run the server with a config that maps `.py` → `/usr/bin/python3`
(`Configs/cgiTest.conf` already does) and request the script. `telnet`/`curl` both work:

```
curl 'http://127.0.0.1:80/hello.py?name=me'
```

Watch the server logs: you should see the CGI fd being registered, and one drain event
per chunk.

---

## 12. Summary — the pieces you own now

| Piece | Where | What it does |
|---|---|---|
| `Cgi` class | `src/cgi/Cgi.cpp` + `include/cgi/Cgi.hpp` | env, argv, `pipe`, `fork`, `execve`, read output, `waitpid`, kill |
| `Client::start_cgi` | `src/network/Client.cpp` | entry point called when a request is judged CGI; registers pipe in epoll, sets `CEXECUTING_CGI` |
| `Client::handle_event` dispatch | `src/network/Client.cpp` | routes pipe events into `_handle_cgi_event` by state |
| `Client::_handle_cgi_event` | `src/network/Client.cpp` | drains, waits, builds the response, hands off to `_send_data()` |
| `Client::handle_cgi_timeout` | `src/network/Client.cpp` | kills script, answers `504` |
| `Multiplexer::_handle_timeout` | `src/network/Multiplexer.cpp` | new CGI scan using `CGI_TIMEOUT` |
| `Client::~Client` | `src/network/Client.cpp` | cleanup safety net when a client vanishes mid-CGI |
| `Makefile` | `Makefile` | add `./src/cgi/Cgi.cpp` |

After you write it: every request is either **static** (current path) or **CGI**
(your new path), and both converge on `CSENDING_HEADERS` → `_send_data()` → done.