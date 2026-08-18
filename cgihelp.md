# CGI — the network side, explained end to end (v2)

You pushed back, and you were right. This version is rebuilt around **your** plan:

* the **Client owns the CGI** and is the only object epoll ever knows about per
  connection,
* the CGI is a **plain helper class — NOT an `AFd`**,
* the request **body** is a temp file your teammate gives you; you feed it to the
  child's stdin via `dup2`,
* the pipe read end is registered in `epoll` **pointing at the Client**,
* on `EPOLLOUT` on the client socket, whatever is in the response buffer is sent by
  your existing `_send_data()`.

Your "where do I start, what do they give me, how do I get a working CGI today" —
answered, function by function, in this file.

---

## 1. Verdict on your questions (be your own harshest critic)

| Your idea | Verdict | Why |
|---|---|---|
| "Why does the CGI inherit from `AFd` if the Client manages it?" | **You are right: drop it.** | The skeleton made `Cgi : public AFd` because a previous design wanted epoll to reference the `Cgi` directly. In **your** design epoll references the **Client** (pointer trick). The `Cgi` is just a data holder + process wrapper: it must not be an AFd. First proof: your `Multiplexer::events_loop()` deletes `fdObj` and calls `del_fd(get_fd())` on cleanup — a bare `Cgi` in that loop would need the Multiplexer to know *which Client owns it*. Making the Client own it avoids touching the Multiplexer at all. |
| "Add `cgi outputpipe[0]` in epoll with the object of the client" | **Correct. This is the trick.** `Epoll::add_fd(fd, ptr, events)` only stores `ptr` in `ev.data.ptr`. Nothing forces `fd` to belong to `ptr`. So the pipe event wakes up the **Client**, and `Client::handle_event()` routes it by `m_state`. |
| "CGI reads the client body from a file fd my friend gives me" | **Correct, and that is the professional way** (nginx does exactly this: request body → temp file → CGI stdin). You also need `lseek(fd, 0, SEEK_SET)` before `dup2` (your friend leaves the cursor at the end after writing), and for GET/DELETE with no body the fd is `-1` → stdin from `/dev/null` (immediate EOF). The subject also insists the **CGI must run in its own directory**, so the child `chdir()`s into the script's folder before `execve`. |
| "When `EPOLLOUT` fires on the client and the buffer has data, I send it" | **Correct, and you already wrote that code.** It's `Client::_send_data()`: `CSENDING_HEADERS` sends `_response.getHeaderBuffer()`. Your only job is to *build* that response from the CGI bytes and set `m_state = CSENDING_HEADERS`. Zero changes in the send path. |
| "The CGI timeout is handled inside the Client" | **Correct.** The Client does the work; the Multiplexer just needs 2 lines so a stale client in `CEXECUTING_CGI` is *dispatched* to the Client instead of being deleted (see §8). And you need one self-check inside `Client::handle_event` for scripts that keep producing output forever (§8.3). |

Your instinct is professional. Everything below is that plan, made correct against the
subject and hardened against the failure cases.

---

## 2. Rules from the subject that FORCE the design

I read `en.subject.pdf` — 4 rules shape everything:

1. **"Checking the value of errno to adjust the server behaviour is strictly forbidden
   after performing a read or write operation."**
   → After `read()` on the pipe you will **never** look at `errno` (no `EAGAIN` checks,
   no `EINTR` retries). This is possible because of rule 2.
2. **"I/O that can wait for data (sockets, pipes/FIFOs, etc.) must be non-blocking and
   driven by a single poll(). Calling read/recv or write/send ... without prior
   readiness will result in a grade of 0."**
   → The pipe is set `O_NONBLOCK`. But you **only call `read()` right after epoll said
   `EPOLLIN`**, and you read **exactly once per event**. With level-triggered epoll:
   * if data was there at `epoll_wait()` time, it is still there when you read it (only
     you can consume it), so `read()` returns `> 0` — no `EAGAIN` possible in practice;
   * if the child closed stdout, `read()` returns `0` (EOF) — no blocking;
   * if it ever returns `-1` anyway (rare race/signal), you treat it as error and give
     up that connection. You never inspect `errno`.

   That is exactly how your existing `Client::_receive_data()` already handles `recv()`:
   `bytes == -1 || bytes == 0 -> EERROR`, no `errno`. Match that style.
3. **"The full request and arguments provided by the client must be available to the
   CGI … CGI should be run in the correct directory … It should support at least one
   CGI (php-CGI, Python…)."**
   → Full environment (all headers → `HTTP_*` variables), and the child `chdir()`s to
   the script's directory before `execve`.
4. **"For chunked requests, your server needs to un-chunk them, the CGI will expect EOF
   … if no content_length is returned from the CGI, EOF will mark the end of the
   returned data."**
   → **Un-chunking is your teammate's job** (they already parse the request; the temp
   file they give you is the *unchunked* body). For the CGI output, you read until EOF;
   you never require a Content-Length from the script, you compute it yourself.

Other notes: `errno` **is** allowed after `pipe`, `fork`, `execve`, `waitpid`, `kill`
(it is literally in the subject's allowed functions list). Only `read`/`write`/`send`/
`recv` are off-limits. And `fcntl(F_SETFL, O_NONBLOCK)` is allowed.

---

## 3. Who owns what — the interface with your teammate

Your teammate (request side) does:

1. parses the whole request (headers + body, un-chunking if needed),
2. writes the body into a temp file and hands you a **readable** fd to it (`O_RDONLY`
   or `O_RDWR`), or `-1` when there is no body,
3. decides "this is CGI" and looks up the interpreter from `cgi_pass` (e.g.
   `/usr/bin/python3`),
4. calls **your** entry point:

```cpp
// somewhere in the client's flow, after the request is fully parsed:
client->start_cgi(interpreter, script_path, body_fd);
```

`interpreter`  = `_route->cgi_pass[extension]`  (from `cgi_pass .py /usr/bin/python3;`)

`script_path`  = the resolved path of the requested script on disk (your buddy already
                solves this; it is the `real_path` your `RequestHandler` computes)

`body_fd`      = fd of the body temp file, or `-1`

Your part (network side) starts at `start_cgi` and ends when the response reaches
`_send_data()`. Everything in this file is yours.

---

## 4. The Cgi class — a plain class, no AFd

### 4.1 Cgi.hpp

```cpp
#ifndef CGI_HPP
# define CGI_HPP

# include <HttpRequest.hpp>
# include <string>
# include <vector>
# include <ctime>

# define PIPE_BUFFER_SIZE 65536   // bytes read per pipe event

class Cgi
{
public:
    enum e_out { MORE, DONE, FAIL };   // what read_output() returns

    Cgi(HttpRequest &request, const std::string &script_path,
        const std::string &interpreter, int body_fd);
    ~Cgi();

    int   execute();              // fork + exec; 0 ok, -1 fail
    e_out read_output();          // drain the pipe; called on EPOLLIN only
    void  kill_child();           // SIGKILL + waitpid  (timeout / abort)

    int     get_pipe_fd() const;  // the fd your Client registers in epoll
    int     get_pid() const;
    bool    exited_cleanly() const;         // WIFEXITED && exit code == 0
    const std::string &get_output() const;  // the CGI's stdout, fully read

private:
    HttpRequest              &_request;    // reference, the Client owns the request
    std::string               _script_path;
    std::string               _interpreter;
    int                       _body_fd;    // -1 => no request body

    int                       _outputPipe[2];
    int                       _status;     // waitpid status
    pid_t                     _pid;
    bool                      _reaped;     // child already waitpid()ed ?
    time_t                    _start;      // when execution began (timeout)

    std::string               _buffer;     // accumulated stdout bytes
    std::vector<std::string>  _env;        // owns every string
    std::vector<const char *> _cenv;       // points into _env, ended with NULL
    std::vector<const char *> _cargv;      // { interpreter, script, NULL }

    void _set_argv();
    void _set_environment();
    void _reap();
};

#endif
```

> Very deliberate choices:
> * **No `AFd` base.** The Client is the epoll participant. `Cgi` is a value-like
>   wrapper: spawn, read, kill, report.
> * `_env` is `std::vector<std::string>` and `_cenv` is built **after** `_env` is fully
>   filled (see §4.4 for the pointer-lifetime bug that destroys this if built too early).
> * `Cgi` returns its own enum; it no longer needs to include `Epoll.hpp`.

### 4.2 Constructor + argv

```cpp
#include <Cgi.hpp>
#include <Logger.hpp>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

Cgi::Cgi(HttpRequest &request, const std::string &script_path,
         const std::string &interpreter, int body_fd)
    : _request(request),
      _script_path(script_path),
      _interpreter(interpreter),
      _body_fd(body_fd),
      _status(0),
      _pid(-1),
      _reaped(false),
      _start(0)
{
    _set_argv();
    _set_environment();
}

void Cgi::_set_argv()
{
    // config: cgi_pass .py /usr/bin/python3
    // so argv = { "/usr/bin/python3", "/www/script.py", NULL }
    _cargv.push_back(_interpreter.c_str());
    _cargv.push_back(_script_path.c_str());
    _cargv.push_back(NULL);
}
```

Why `argv[0]` is the interpreter, not the script: `execve` loads a *program*; the
program needs the script as its argument. You are running *python3*, not the script.
(The script's shebang line is ignored because python3 is the program.)

### 4.3 Environment (CGI/1.1)

```cpp
void Cgi::_set_environment()
{
    // --- fixed CGI/1.1 variables ---
    _env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    _env.push_back("SERVER_PROTOCOL=HTTP/1.1");
    _env.push_back("SERVER_SOFTWARE=webserv/1.0");

    std::string host = _request.getHeader("host");       // e.g. "localhost:8080"
    std::string name = host, port = "80";
    size_t colon = host.find(':');
    if (colon != std::string::npos)
    {
        name = host.substr(0, colon);
        port = host.substr(colon + 1);
    }
    _env.push_back("SERVER_NAME=" + name);
    _env.push_back("SERVER_PORT=" + port);

    _env.push_back("REQUEST_METHOD=" + _method_str(_request.getMethod()));
    _env.push_back("REQUEST_URI=" + _request.getUri().getOriginal());
    _env.push_back("SCRIPT_NAME=" + _request.getUri().getPath());
    _env.push_back("SCRIPT_FILENAME=" + _script_path);
    _env.push_back("QUERY_STRING=" + _request.getUri().getQuery());

    // --- the body: CONTENT_LENGTH from the temp file, CONTENT_TYPE from the request
    if (_body_fd != -1)
    {
        struct stat st;
        if (fstat(_body_fd, &st) == 0)
        {
            std::ostringstream oss;
            oss << st.st_size;
            _env.push_back("CONTENT_LENGTH=" + oss.str());
        }
    }
    if (_request.getHeader("content-type") != "")
        _env.push_back("CONTENT_TYPE=" + _request.getHeader("content-type"));

    // --- every remaining header becomes HTTP_* (dash -> underscore, UPPERCASE) ---
    std::map<std::string, std::string>::const_iterator it;
    for (it = _request.getHeaders().begin(); it != _request.getHeaders().end(); ++it)
    {
        const std::string &n = it->first;   // already lowercased by HttpRequest
        const std::string &v = it->second;

        if (n == "content-length" || n == "content-type")
            continue;                       // they are CONTENT_*, never HTTP_CONTENT_*

        std::string env = "HTTP_";
        for (size_t i = 0; i < n.size(); ++i)
        {
            char c = n[i];
            env += (c == '-') ? '_' : static_cast<char>(::toupper(c));
        }
        _env.push_back(env + "=" + v);
    }

    // build the char** table ONLY once _env will never grow again:
    for (size_t i = 0; i < _env.size(); ++i)
        _cenv.push_back(_env[i].c_str());
    _cenv.push_back(NULL);
}
```

And the tiny method-name helper:

```cpp
std::string Cgi::_method_str(HttpRequest::Method m)
{
    switch (m)
    {
        case HttpRequest::HTTP_GET:    return "GET";
        case HttpRequest::HTTP_POST:   return "POST";
        case HttpRequest::HTTP_DELETE: return "DELETE";
        default:                       return "GET";
    }
}
```

> Mini glossary for your defense: the CGI spec calls these "meta-variables". `SCRIPT_NAME`
> is the URL path, `SCRIPT_FILENAME` is the real path on disk (the one `execve`/`chdir`
> use), `QUERY_STRING` is everything after `?`, `REQUEST_URI` the whole original URI.
> Headers become `HTTP_<NAME>` so a CGI can read `Cookie`, `User-Agent`, `Accept`, etc.

### 4.4 execute() — fork, pipes, the child block

```cpp
int Cgi::execute()
{
    if (pipe(_outputPipe) == -1)
    {
        LOG_ERROR << "pipe() -> " << strerror(errno);
        return -1;
    }

    // Pipes must be non-blocking (subject). We will only read() right after
    // epoll reported EPOLLIN, so no EAGAIN in practice.
    if (fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK) == -1)
    {
        LOG_ERROR << "fcntl() -> " << strerror(errno);
        (void)close(_outputPipe[0]);
        (void)close(_outputPipe[1]);
        return -1;
    }

    _pid = fork();
    if (_pid == -1)
    {
        LOG_ERROR << "fork() -> " << strerror(errno);
        (void)close(_outputPipe[0]);
        (void)close(_outputPipe[1]);
        return -1;
    }

    if (_pid == 0)
    {
        /* ===================== CHILD ===================== */

        (void)close(_outputPipe[0]);   // read end unused in child: without this,
                                       // EOF would wait for a second read end.

        // stdin <- body file (your teammate's temp file). body_fd == -1 => /dev/null.
        int in = (_body_fd != -1) ? _body_fd : open("/dev/null", O_RDONLY);
        if (in != -1)
        {
            (void)lseek(in, 0, SEEK_SET);   // teammate left the cursor at the END
            (void)dup2(in, STDIN_FILENO);
            if (in != _body_fd)
                (void)close(in);
        }

        // stdout -> pipe write end: whatever the script prints flows to the server.
        (void)dup2(_outputPipe[1], STDOUT_FILENO);
        (void)close(_outputPipe[1]);

        // stderr -> /dev/null so a chatty script cannot backpressure the child.
        int dn = open("/dev/null", O_WRONLY);
        if (dn != -1)
        {
            (void)dup2(dn, STDERR_FILENO);
            (void)close(dn);
        }

        // The CGI must run in its own directory (relative file access, subject).
        std::string dir = _script_path;
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos)
            dir = ".";
        else if (slash == 0)
            dir = "/";
        else
            dir = dir.substr(0, slash);
        (void)chdir(dir.c_str());

        // Replace this process image with the interpreter.
        // execve() returns ONLY on failure (errno set, allowed here: not read/write).
        execve(_cargv[0], (char *const *)&_cargv[0], (char *const *)&_cenv[0]);

        LOG_ERROR << "execve(" << _interpreter << ") -> " << strerror(errno);
        _exit(127);     // 127 = "program not run". Never fall through.
    }

    /* ===================== PARENT ===================== */

    (void)close(_outputPipe[1]);   // CRITICAL: the ONLY real writer is the child.
                                   // If the parent keeps it open, EOF never comes.

    if (_body_fd != -1)
        (void)close(_body_fd);     // the child now has its own copy (dup2). Parent
    _body_fd = -1;                 // drops it; the temp file can even be unlinked now.

    _start = time(NULL);
    _reaped = false;
    return 0;
}
```

The child block, memorized as 6 rules:

1. close the pipe **read** end,
2. stdin ← body file (`lseek` to 0 first; `/dev/null` if no body),
3. stdout ← pipe **write** end via `dup2`, then close the original write end —
   otherwise TWO write ends exist and the parent would never see EOF,
4. stderr → `/dev/null`,
5. `chdir` into the script's directory,
6. `execve`; if it returns, `_exit(127)`.

> `_exit()` vs `exit()`: `exit()` would run C++ destructors and flush stdio buffers
> *of the copied parent* — the child could re-write buffered data or deadlock on
> inherited locks. `_exit()` skips all of it.

### 4.5 read_output() — drain the pipe, without touching errno

```cpp
Cgi::e_out Cgi::read_output()
{
    char chunk[PIPE_BUFFER_SIZE];

    // Called ONLY because epoll reported EPOLLIN on the pipe. Read once.
    // Level-triggered epoll fires again as long as data or EOF remains, so a
    // single bounded read per event is enough, and -1 cannot be EAGAIN here.
    ssize_t n = read(_outputPipe[0], chunk, sizeof(chunk));

    if (n > 0)
    {
        _buffer.append(chunk, static_cast<size_t>(n));
        return MORE;                       // more bytes may come; keep waiting
    }

    if (n == 0)
    {
        // EOF: the child closed stdout, i.e. it ended (or is about to end).
        int r;
        do { r = waitpid(_pid, &_status, WNOHANG); }
        while (r == -1 && errno == EINTR); // waitpid errno IS allowed

        if (r == 0)
        {
            // Child still alive although stdout is closed: a script that closed
            // its stdout early. It declared "done" - force it to finish, then
            // reap. (Block happens only AFTER SIGKILL, so it cannot hang.)
            (void)kill(_pid, SIGKILL);
            (void)waitpid(_pid, &_status, 0);
        }
        _reaped = true;
        return DONE;
    }

    // n < 0: treat as failure WITHOUT inspecting errno (forbidden after read).
    return FAIL;
}
```

> Why **one** read per event and not a drain-loop: a malicious script could flood the
> pipe; a drain-loop would monopolize the single-threaded loop and starve every other
> client. One bounded read → `ECONTINUE` → `epoll_wait` again → level-triggered event
> re-fires instantly → next chunk. Fair to everybody.

### 4.6 reap / kill / accessors / destructor

```cpp
void Cgi::kill_child()
{
    if (_pid > 0 && !_reaped)
    {
        (void)kill(_pid, SIGKILL);
        (void)waitpid(_pid, &_status, 0);
        _reaped = true;
    }
}
```

```cpp
bool Cgi::exited_cleanly() const
{
    return _reaped && WIFEXITED(_status) && WEXITSTATUS(_status) == 0;
}
```

```cpp
int Cgi::get_pipe_fd() const            { return _outputPipe[0]; }
int Cgi::get_pid() const                { return _pid; }
const std::string &Cgi::get_output() const { return _buffer; }

Cgi::~Cgi()
{
    kill_child();   // a Cgi that dies with a live child never orphans it
}
```

---

## 5. Client integration

### 5.1 New members in Client.hpp

```cpp
# include <Cgi.hpp>

class Client : public AFd
{
public:
    int  start_cgi(const std::string &interpreter,
                   const std::string &script_path, int body_fd);
    // ...
private:
    Cgi    *_cgi;             // NULL when no CGI is running
    time_t  _cgi_start;       // when the CGI started (timeout anchor)

    Epoll::EventState _handle_cgi_event();      // pipe event
    void              _cgi_timeout();           // kill + 504 + rearm
    void              _build_cgi_response();    // drain bytes -> HttpResponse
    void              _build_cgi_error(HttpStatus::Code code);
};
```

In `Client::Client(...)` member-init list add: `_cgi(NULL), _cgi_start(0)`.

### 5.2 start_cgi — the entry point

```cpp
int Client::start_cgi(const std::string &interpreter,
                      const std::string &script_path, int body_fd)
{
    _cgi = new Cgi(_request, script_path, interpreter, body_fd);
    _cgi_start = time(NULL);

    if (_cgi->execute() != 0)              // pipe/fork failed
    {
        delete _cgi; _cgi = NULL;
        _build_cgi_error(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);   // socket still registered
        return -1;
    }

    m_state = CEXECUTING_CGI;

    // THE POINTER TRICK: the PIPE fd points at the CLIENT object.
    if (_epoll.add_fd(_cgi->get_pipe_fd(), this, EPOLLIN) != 0)
    {
        delete _cgi; _cgi = NULL;
        m_state = CSENDING_HEADERS;
        _build_cgi_error(HttpStatus::InternalServerError);
        _epoll.edit_fd(m_fd, this, EPOLLOUT);   // still registered
        return -1;
    }

    // From now on the client socket is OUT of epoll: any event on this Client
    // object is unambiguously from the CGI pipe. Clean separation.
    _epoll.del_fd(m_fd);
    return 0;
}
```

Why remove the socket: while a CGI runs, `handle_event()` receives only `uint32_t
event` — **not** the fd. If both the socket and the pipe pointed at the Client, you
couldn't tell where an event came from. By keeping **one fd in epoll at a time**, every
event while `CEXECUTING_CGI` is the pipe. (Disconnects during a CGI are noticed later,
when `send()` fails — which is fine and handled.)

### 5.3 Client::handle_event — dispatch by state

```cpp
Epoll::EventState Client::handle_event(uint32_t event)
{
    if (m_state == CEXECUTING_CGI)
    {
        // EVERYTHING reaching here is the CGI pipe (socket is del'ed).
        // Self-check: a script that streams output forever never gets "stale"
        // in the LRU list, so it MUST be timed out from inside, on its own events.
        if (_cgi != NULL && time(NULL) - _cgi_start > CGI_TIMEOUT)
        {
            _cgi_timeout();
            return Epoll::ECONTINUE;
        }
        return _handle_cgi_event();
    }

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

### 5.4 Client::_handle_cgi_event — the pipeline event

```cpp
Epoll::EventState Client::_handle_cgi_event()
{
    Cgi::e_out result = _cgi->read_output();

    if (result == Cgi::MORE)
        return Epoll::ECONTINUE;          // keep waiting (for bytes or EOF)

    // pipe is finished - unregister it BEFORE anything else.
    _epoll.del_fd(_cgi->get_pipe_fd());

    if (result == Cgi::FAIL || !_cgi->exited_cleanly())
    {
        delete _cgi; _cgi = NULL;         // destructor kills/reaps if needed
        _build_cgi_error(HttpStatus::BadGateway);          // 502
    }
    else
    {
        _build_cgi_response();            // parse bytes -> HttpResponse
        delete _cgi; _cgi = NULL;
    }

    // The buffer now holds the whole CGI answer. Back to the normal path:
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);  // re-register socket (it was del'ed)
    return Epoll::ECONTINUE;              // NEVER EFINISHED: the client must be sent!
}
```

> **Why `ECONTINUE` and never `EFINISHED` here:** if you returned `EFINISHED`, the
> Multiplexer does `del_fd(fdObj->get_fd())` and `delete fdObj` — it would close and
> destroy the *client socket*. You want to keep the client and send its response.
> Return `ECONTINUE`; the socket is re-added for `EPOLLOUT`, epoll wakes it, and
> `_send_data()` does the rest.

### 5.5 Client::_cgi_timeout — 504

```cpp
void Client::_cgi_timeout()
{
    LOG_WARN << "CGI timed out on client fd " << m_fd;

    _epoll.del_fd(_cgi->get_pipe_fd());   // 1) stop watching the pipe
    delete _cgi; _cgi = NULL;             // 2) kill + reap the child
    _build_cgi_error(HttpStatus::GatewayTimeout);   // 3) 504
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);  // 4) socket back, arms EPOLLOUT
}
```

> Order is sacred: `del_fd` the pipe **before** `delete _cgi`, or epoll is left with a
> dangling pointer. (`Cgi::~Cgi()` → `kill_child()` guarantees the script dies and is
> reaped.)

### 5.6 Client::handle_timeout — the Multiplexer's dispatch now covers CGI

```cpp
void Client::handle_timeout()
{
    if (m_state == CEXECUTING_CGI && _cgi != NULL)
    {
        _cgi_timeout();
        return;
    }
    // ... your existing 408 logic unchanged ...
}
```

### 5.7 Client destructor — the safety net

```cpp
Client::~Client()
{
    if (_cgi)
    {
        _epoll.del_fd(_cgi->get_pipe_fd());
        delete _cgi;      // Cgi::~Cgi() kills + reaps the still-running script
        _cgi = NULL;
    }
}
```

This is what makes `Multiplexer::_handle_timeout()`'s "else → delete client" branch
non-leaking: even if the delete path runs while a CGI is in flight, the child is killed
and reaped, and the pipe is unregistered.

---

## 6. Multiplexer: the 2-line timeout change

In `Multiplexer::_handle_timeout()`, add `CEXECUTING_CGI` to the branch that *handles
then reschedules* (instead of the else-branch that deletes):

```cpp
if (now - client->m_lastActivity > timeout)
{
    if (client->m_state == Client::CRECEVING
        || client->m_state == Client::CEXECUTING_CGI)   // <-- added
    {
        client->handle_timeout();      // Client decides what to do by its state
        _clientsList.pop_front();
        client->m_lastActivity = time(NULL);
        _clientsList.push_back(client);
        client->m_it = --_clientsList.end();
    }
    else
    {
        _clientsList.pop_front();
        _epoll.del_fd(client->get_fd());
        LOG_INFO << "Client with fd " << client->get_fd() << " timed out";
        delete client;
    }
}
```

Two mechanisms now cover the two failure shapes:

| CGI behaviour | Who catches it | What happens |
|---|---|---|
| **silent** (hangs, no output) | `Multiplexer::_handle_timeout` → `client->handle_timeout()` → `_cgi_timeout()` | kill + `504 Gateway Timeout` |
| **never ending** (streams forever) | self-check inside `Client::handle_event()` on every pipe event | same `504` |

---

## 7. Building the HTTP response from the CGI bytes

The script's stdout looks like:

```
Content-Type: text/html\r\n
\r\n
<html><body>Hello</body></html>
```

Rules: header block ends at the first **empty line**; `Status:` provides the status
code; other lines are headers to forward; everything after the empty line is the body;
**you** recompute `Content-Length` from the bytes you actually received.

```cpp
void Client::_build_cgi_response()
{
    const std::string &raw = _cgi->get_output();

    HttpStatus::Code code = HttpStatus::OK;
    std::map<std::string, std::string> headers;
    std::string body;
    bool in_headers = true;

    size_t i = 0;
    while (i < raw.size())
    {
        size_t nl = raw.find('\n', i);
        std::string line = raw.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);          // normalize "\r\n" to "\n"
        i = (nl == std::string::npos) ? raw.size() : nl + 1;

        if (in_headers)
        {
            if (line.empty())                     // blank line: headers end here
            {
                in_headers = false;
                continue;
            }
            if (line.compare(0, 7, "Status:") == 0)     // "Status: 201 Created"
            {
                std::string rest = line.substr(7);
                size_t p = rest.find_first_not_of(" \t");
                if (p != std::string::npos)
                    code = static_cast<HttpStatus::Code>(std::atoi(rest.c_str() + p));
                continue;
            }
            if (line.compare(0, 5, "HTTP/") == 0)
                continue;                         // NPH script printed a status line: skip

            size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string name  = _trim(line.substr(0, colon));
            std::string value = _trim(line.substr(colon + 1));

            std::string lower = _lower(name);
            if (lower == "content-length" || lower == "connection"
                || lower == "transfer-encoding")
                continue;                        // we manage those ourselves

            headers[name] = value;
        }
        else
        {
            body += line;                        // body is stored verbatim
            if (nl != std::string::npos)
                body += "\n";
        }
    }

    _response.setStatusCode(code);
    _response.setHeader("Content-Type",
        headers.find("Content-Type") != headers.end() ? headers["Content-Type"]
                                                      : "text/plain");
    _response.setBody(body);     // computes Content-Length from ACTUAL bytes
    _response.build();           // status line + all headers + body into _header_buffer
}
```

(Add two 3-line helpers `_trim`/`_lower`, or inline the small loops — C++98 has no
`std::tolower` overload for `std::string`.)

`_build_cgi_error(code)` is your existing error-page logic (set status, `Content-Type:
text/html`, small body, `build()`) — adapt/reuse the one from `RequestHandler`.

---

## 8. "And then when EPOLLOUT fires, I send the buffer" — yes

Your mental model is exactly the code you already have:

```
step 1  EPOLLIN on pipe     -> read_output()           -> _buffer grows
step 2  EOF on pipe         -> _build_cgi_response()   -> _header_buffer ready
                            -> m_state = CSENDING_HEADERS
                            -> add_fd(socket, EPOLLOUT)
step 3  EPOLLOUT on socket  -> Client::_send_data()    -> sends _header_buffer
```

For a CGI response `_response.hasFile()` is `false`, and `build()` already glued headers
**and** body into `_header_buffer`, so `_send_data()` sends the whole answer and lands
on `CFINISHED` — keep-alive handling included. **No change to `_send_data()`.**

---

## 9. Checklist — every file you touch today

| File | Change |
|---|---|
| `include/cgi/Cgi.hpp` | rewrite: plain class, no AFd, `e_out` enum, `body_fd` ctor param |
| `src/cgi/Cgi.cpp` | rewrite: env, argv, `execute`, `read_output`, `kill_child` (above) |
| `include/network/Client.hpp` | `#include <Cgi.hpp>`; `start_cgi`; members `_cgi`, `_cgi_start`; private helpers |
| `src/network/Client.cpp` | ctor init list; `start_cgi`; `handle_event` dispatch; `_handle_cgi_event`; `_cgi_timeout`; `handle_timeout` branch; `_build_cgi_response`/`_build_cgi_error`; destructor |
| `src/network/Multiplexer.cpp` | `_handle_timeout`: add `CEXECUTING_CGI` to the handle/reschedule branch |
| `Makefile` | add `./src/cgi/Cgi.cpp` to `SRC` (it is not compiled today!) |

Compile flags reminder: `-std=c++98 -Wall -Wextra -Werror`. No `nullptr`, no `auto`, no
range-for, no lambdas. Cast ignored syscall results with `(void)`.

---

## 10. Testing — a minimal CGI

`www/hello.py` (headers must use CRLF):

```python
#!/usr/bin/python3
import sys
sys.stdout.write("Content-Type: text/html\r\n")
sys.stdout.write("\r\n")
sys.stdout.write("<html><body><h1>Hello from CGI</h1></body></html>")
sys.stdout.flush()
```

Serve with a config that maps `.py` → `/usr/bin/python3` (`Configs/cgiTest.conf` does),
then:

```
curl 'http://127.0.0.1:80/hello.py?foo=bar'
```

Watch the log: CGI fd registered, one event per chunk, EOF, then the response goes out
on the client's `EPOLLOUT`. To test the timeout, make a script with
`import time; time.sleep(60)` — you should get `504` after `CGI_TIMEOUT` and the process
should be gone (`pgrep python3` returns nothing).

---

## Summary of the pieces you own

```
        friend's request side                    YOUR network side (this file)
  ┌───────────────────────────┐          ┌───────────────────────────────────────┐
  │ parse request + body      │          │ Client::start_cgi(...)                │
  │ un-chunk, write body file │  gave:   │   Cgi::execute()  (pipe, fork, exec)  │
  │ detect "is CGI"           │  interpr.│   add_fd(pipe, this, EPOLLIN)         │
  │ resolve script path       │  script  │   del_fd(socket)                      │
  └───────────────────────────┘  body_fd └─────────────────┬─────────────────────┘
                                                          │ pipe events
                                                            ▼
                                              Client::_handle_cgi_event()
                                              read_output() until EOF
                                              _build_cgi_response()
                                              add_fd(socket, EPOLLOUT)
                                              _send_data()   <-- you already had it
```

The one object that exists in epoll is the **Client**. The `Cgi` is its private
assistant: **spawn → feed body → collect output → report**. Timeouts are a Client
matter (`_cgi_timeout`), triggered both by its own events and by the Multiplexer's
generic dispatch. Clean, small, and — crucially — fully explainable at the evaluation.