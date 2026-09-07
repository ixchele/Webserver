# webserv — `network/` and `cgi/` — Extremely Boring, Line-By-Line Master Guide

> **Scope:** Only `network/` and `cgi/`. Nothing else is edited. This document explains every line as if we were writing it from scratch, bottom-up, low-level (syscalls, bytes, fd lifecycle) and high-level (architecture, ownership, state machine).
>
> **File list covered:**
> ```
> include/network/AFd.hpp          src/network/AFd.cpp
> include/network/timeout.hpp
> include/network/Epoll.hpp        src/network/Epoll.cpp
> include/network/Server.hpp       src/network/Server.cpp
> include/network/Client.hpp       src/network/Client.cpp
> include/network/Multiplexer.hpp  src/network/Multiplexer.cpp
> include/cgi/Cgi.hpp              src/cgi/Cgi.cpp
> ```
> **Read order for mastery:** `AFd` → `timeout.hpp` → `Epoll` → `Server` → `Client` → `Multiplexer` → `Cgi`. That is the order this document follows. Each chapter is self-contained but builds on the previous.

---

## Table of Contents

1. [Big Picture — What This Program Is](#1-big-picture--what-this-program-is)
2. [Layer Cake — How `network/` and `cgi/` Fit](#2-layer-cake--how-network-and-cgi-fit)
3. [Chapter 1: `AFd` — The Abstract File Descriptor (`AFd.hpp` / `AFd.cpp`)](#3-chapter-1-afd--the-abstract-file-descriptor-afdhpp--afdcpp)
4. [Chapter 2: `timeout.hpp` — Time Is A Resource](#4-chapter-2-timeouthpp--time-is-a-resource)
5. [Chapter 3: `Epoll` — The Multiplexing Syscall Wrapper (`Epoll.hpp` / `Epoll.cpp`)](#5-chapter-3-epoll--the-multiplexing-syscall-wrapper-epollhpp--epollcpp)
6. [Chapter 4: `Server` — The Listening Socket (`Server.hpp` / `Server.cpp`)](#6-chapter-4-server--the-listening-socket-serverhpp--servercpp)
7. [Chapter 5: `Client` — The Active Connection And HTTP State Machine (`Client.hpp` / `Client.cpp`)](#7-chapter-5-client--the-active-connection-and-http-state-machine-clienthpp--clientcpp)
8. [Chapter 6: `Multiplexer` — The Orchestrator (`Multiplexer.hpp` / `Multiplexer.cpp`)](#8-chapter-6-multiplexer--the-orchestrator-multiplexerhpp--multiplexercpp)
9. [Chapter 7: `Cgi` — Fork-Exec-Pipe CGI/1.1 (`Cgi.hpp` / `Cgi.cpp`)](#9-chapter-7-cgi--fork-exec-pipe-cgi11-cgihpp--cgicpp)
10. [End-To-End Event Flow — One Request From `accept()` To `sendfile()`](#10-end-to-end-event-flow--one-request-from-accept-to-sendfile)
11. [Failure Modes And Why Each `if` Exists](#11-failure-modes-and-why-each-if-exists)
12. [How You Would Re-Write This From Zero](#12-how-you-would-re-write-this-from-zero)

---

## 1. Big Picture — What This Program Is

This is a **42 webserv** — a from-scratch HTTP/1.1 server in C++98. No nginx inside, no `libevent`. You create listening sockets, you `epoll_wait()` for readiness, you `accept()` clients, you `recv()` bytes, you parse HTTP, you `send()`/`sendfile()` a response, you optionally `fork()`+`execve()` a CGI script. Everything is **non-blocking** and **single-process, single-thread, event-driven**.

High-level invariant:

```
Multiplexer owns:
  Epoll (the kernel epoll fd)
  map<string, Server*> m_servers  (one per unique host:port)
  list<Client*> _clientsList      (all connected clients, ordered by last activity)

Epoll watches:
  every Server fd for EPOLLIN (someone wants to connect)
  every Client fd for EPOLLIN or EPOLLOUT (request body arriving, response ready)
  every Cgi pipe read-end for EPOLLIN (CGI stdout arriving)

Events loop:
  wait(100ms) -> for each ready fd -> fdObj->handle_event(events) -> ECONTINUE/EFINISHED/EERROR
```

Low-level invariant: **Every fd that enters epoll is non-blocking and CLOEXEC.** If you forget `SOCK_NONBLOCK` or `O_NONBLOCK` or `EPOLL_CLOEXEC`, you block the entire server or leak fds into CGI children.

---

## 2. Layer Cake — How `network/` and `cgi/` Fit

```
main.cpp
  |
  v
ConfigParser -> vector<ServerConfig>
  |
  v
Multiplexer(vector<ServerConfig>)   <--- network/
  |  creates Server(host,port,config,epoll,clientList) for each unique host:port
  |  Server::run() -> socket() -> setsockopt(SO_REUSEADDR) -> bind() -> listen()
  v
Multiplexer::startup()
  |  epoll.add_fd(Server.fd, EPOLLIN) for each Server
  v
Multiplexer::events_loop()          <--- the infinite while(true)
  |  epoll.wait() -> AFd::handle_event()  (polymorphism)
  |       Server::handle_event() -> accept4() -> new Client(fd) -> epoll.add_fd(Client.fd)
  |       Client::handle_event() -> recv()/send()/sendfile() or CGI handling
  |       Cgi via Client -> fork()/pipe()/execve()/read()  <--- cgi/
  |  _handle_timeout() -> keep-alive vs main timeout vs CGI timeout
  v
HttpRequest / HttpResponse / RequestHandler  (in src/http/, not covered here, but called by Client)
```

The `cgi/` directory is intentionally tiny: one class `Cgi`. It is **owned by `Client`**. `Client` is the only caller. That keeps fd ownership clear.

---

## 3. Chapter 1: `AFd` — The Abstract File Descriptor (`AFd.hpp` / `AFd.cpp`)

### 3.1 Why This File Exists At All

You have two kinds of fds in epoll: listening server fds and connected client fds. `epoll_wait()` returns `epoll_event.data.ptr` — a `void*` you set when you `epoll_ctl(ADD)`. The naive way is to store fd ints and `switch` on them. The clean way is to store a **polymorphic object** whose `handle_event()` does the right thing. That is `AFd`.

It is the **base class** for `Server` and `Client`. It gives you:

* A wrapped `int m_fd` with accessor `get_fd()`.
* A `Type` tag `SERVER`/`CLIENT` so the multiplexer can `static_cast` safely.
* A pure virtual `handle_event(uint32_t event) -> Epoll::EventState`.
* A virtual destructor that `close()`s the fd if it is > 2 (not stdin/stdout/stderr).

High-level: This is the **Strategy pattern** via inheritance. Low-level: It is a thin RAII wrapper around an integer fd.

### 3.2 `include/network/AFd.hpp` — Line By Line

```cpp
#ifndef AFD_HPP
# define AFD_HPP
```
Classic include guard. Without it, if `Server.hpp` and `Client.hpp` both `#include "AFd.hpp"` and some translation unit includes both, you get redefinition errors. The `#ifndef` ensures the compiler sees the class definition exactly once per TU.

```cpp
# include <Epoll.hpp>
# include <string>
```
`Epoll.hpp` is included **only** for the return type `Epoll::EventState`. Note the dependency direction: `Epoll.hpp` forward-declares `class AFd;` but does NOT include `AFd.hpp`. `AFd.hpp` **does** include `Epoll.hpp`. This avoids a circular include. ` <string>` is actually unused in this header (no string member), but is harmless — probably left over from iteration.

```cpp
class AFd
{
  public:
    enum Type {SERVER, CLIENT};
```
An enum to tag the dynamic type. You could use `dynamic_cast`, but this codebase is C++98 without RTTI guarantees and wants a cheap integer check. `SERVER=0`, `CLIENT=1` implicitly. This is used in `Multiplexer::events_loop()` to decide whether to erase from `_clientsList`.

```cpp
    AFd(int fd, Type type);
```
Constructor takes an fd and a type. No default constructor — you must know what you are wrapping.

```cpp
    int get_fd() const;
    Type get_type() const;
```
Const accessors. They are trivial getters but they enforce encapsulation: outside code never writes `m_fd` directly (except `Server::create_socket()` which does `this->m_fd = socket(...)` because it is a derived class accessing `protected`).

```cpp
    virtual Epoll::EventState handle_event(uint32_t event) = 0;
```
Pure virtual. This is what makes `AFd` abstract. You cannot instantiate `AFd`; you must override this in `Server` and `Client`. The parameter is the `epoll_event.events` bitmask (`EPOLLIN|EPOLLOUT|EPOLLERR|...`). The return is `ECONTINUE` (keep watching), `EFINISHED` (remove and delete), `EERROR` (same but error path).

```cpp
    virtual ~AFd();
```
Virtual destructor. **Critical.** If you `delete AFd*` that actually points to a `Client`, without `virtual`, only `AFd::~AFd()` runs and `Client::~Client()` never runs, leaking `Cgi*` and not removing CGI fds from epoll. With `virtual`, the most-derived destructor runs first.

```cpp
  protected:
    int m_fd;
    Type _type;
};
#endif
```
`protected` not `private`: derived classes `Server` and `Client` need to read/write `m_fd`. `_type` is set once in constructor and never changes. The naming is inconsistent (`m_fd` vs `_type`); that is just style drift, not semantic.

### 3.3 `src/network/AFd.cpp` — Line By Line

```cpp
#include <AFd.hpp>
#include <unistd.h>
```
`unistd.h` for `close()`.

```cpp
AFd::AFd(int fd, Type type) : m_fd(fd), _type(type)
{
}
```
Initializer list. No validation. You can pass `-1` (as `Server` does before `create_socket()`). That is intentional: `Server` constructs with `AFd(-1, SERVER)` then later assigns `m_fd = socket(...)`.

```cpp
AFd::~AFd()
{
    if (m_fd > 2)
        close(m_fd);
}
```
The only RAII in this class. If `m_fd` is 0,1,2 (stdin/out/err) it does NOT close — protecting the process standard fds. If `m_fd` is -1, also not closed (`-1 > 2` is false). If `m_fd` is 3+, it is a socket/pipe/file and gets closed. **Boring detail:** `close()` can fail with `EINTR` or `EBADF`, but this destructor ignores return value. That is common; there is nothing useful to do in a destructor on close failure. Note: `Client`'s destructor also does `epoll.del_fd()` before `delete`, so by the time `~AFd` runs, the fd is already removed from epoll interest list. Closing an fd that is still in epoll would auto-remove it on Linux, but explicit `EPOLL_CTL_DEL` is cleaner and avoids races.

```cpp
int AFd::get_fd() const
{
    return this->m_fd;
}

AFd::Type AFd::get_type() const
{
    return this->_type;
}
```
Trivial. Note `this->` is unnecessary but explicit.

### 3.4 How You Would Write This Yourself

Step 1: You realize `epoll_event.data.ptr` wants a `void*`. You want to store different behaviours. You invent `AFd`.
Step 2: You write the header guard, include `Epoll.hpp` for `EventState`, declare `enum Type`, declare pure virtual `handle_event`, virtual destructor.
Step 3: You write the cpp: constructor initializer list, destructor with `if (m_fd > 2) close(m_fd)`, two getters.
Step 4: You test: `Server s(...); AFd* p = &s; p->handle_event(EPOLLIN);` should call `Server::handle_event`.

---

## 4. Chapter 2: `timeout.hpp` — Time Is A Resource

### 4.1 The File In Full

```cpp
#ifndef TIMEOUT_HPP
# define TIMEOUT_HPP

# define KEEPTALIVE_TIMEOUT 17
# define MAIN_TIMEOUT 16
# define CGI_TIMEOUT 16

#endif
```

That is 8 lines and it controls the most subtle bug surface: **when to kill idle connections**.

### 4.2 Line By Line And Why These Numbers

* `KEEPTALIVE_TIMEOUT 17` — If a `Client` is in state `CKEEPT_ALIVE` (finished a request, waiting for next request on same TCP connection because `Connection: keep-alive`), we give it 17 seconds. If no bytes arrive within 17s, we close it. This is slightly longer than `MAIN_TIMEOUT` so that a keep-alive idle connection lingers a bit longer than a half-open receiving connection.

* `MAIN_TIMEOUT 16` — For any other non-CGI state, 16 seconds of silence means timeout. `Multiplexer::_handle_timeout()` checks `now - client->m_lastActivity > timeout`. `m_lastActivity` is updated on every `handle_event()` that returns `ECONTINUE` for a `CLIENT`.

* `CGI_TIMEOUT 16` — If `Client` is `CEXECUTING_CGI`, we give the CGI child 16 seconds to produce output. If `now - _cgi_start > CGI_TIMEOUT`, we `killChild()` and return `504 Gateway Timeout`.

High-level: This file is the **SLA**. Low-level: It is three integer macros consumed by exactly two places: `Multiplexer::_handle_timeout()` and `Client::handle_event()` (CGI check). The values 16/17 are arbitrary but must be > `epoll_wait` timeout (100ms) and < typical browser timeout (30-60s). The 1-second difference between keep-alive and main avoids thrashing.

If you were writing this, you would start with 30s for all, then tune down to 16s to satisfy 42 tester which hammers you with `ab` and checks you close idle fds.

---

## 5. Chapter 3: `Epoll` — The Multiplexing Syscall Wrapper (`Epoll.hpp` / `Epoll.cpp`)

### 5.1 Why Wrap `epoll` At All?

Raw epoll is 4 syscalls: `epoll_create1`, `epoll_ctl(ADD/MOD/DEL)`, `epoll_wait`, `close`. Wrapping them in a class gives you RAII (auto-close), error handling via exceptions, and a single place to log `epoll_ctl` failures. It also hides `m_fd` so no one else calls `epoll_ctl` directly.

### 5.2 `include/network/Epoll.hpp` — Line By Line

```cpp
#ifndef EPOLL_HPP
# define EPOLL_HPP
# include <sys/epoll.h>

# define MAXEVENTS 64
```
`MAXEVENTS 64` — Size of the `epoll_event` array passed to `epoll_wait`. 64 is a balance: large enough to batch many ready fds per syscall, small enough to fit on stack (`64 * sizeof(epoll_event)` ~ 64*12=768 bytes). `Multiplexer::events_loop()` allocates `epoll_event events[MAXEVENTS]` on stack each iteration; 64 is typical.

```cpp
class AFd;
```
Forward declaration. `Epoll` methods take `AFd* ptr` but never need `AFd`'s definition (only a pointer). This breaks the circular include (`AFd.hpp` includes `Epoll.hpp`).

```cpp
class Epoll
{
public:
    enum EventState {
        ECONTINUE = 0,
        EFINISHED = 1,
        EERROR = 2
    };
```
Return codes for `handle_event()`. `ECONTINUE` means "keep this fd in epoll, nothing to clean up". `EFINISHED` means "request/response cycle done, close client" (or for CGI, done). `EERROR` means "recv/send failed or EPOLLERR/HUP, close client". `Multiplexer::events_loop()` checks `!= ECONTINUE` to decide to `del_fd`+`delete`.

```cpp
    Epoll();
    ~Epoll();
```
Constructor creates the epoll fd; destructor closes it. No copy semantics.

```cpp
	int add_fd(int fd, AFd *ptr, int events);
	int edit_fd(int fd, AFd *ptr, int events);
	void del_fd(int fd);
    int wait(epoll_event *events);
```
`add_fd` wraps `EPOLL_CTL_ADD`. `edit_fd` wraps `EPOLL_CTL_MOD`. `del_fd` wraps `EPOLL_CTL_DEL`. `wait` wraps `epoll_wait`. `add_fd`/`edit_fd` return `int` (0 success, -1 failure) so caller can log; `del_fd` is `void` but logs internally on failure; `wait` returns number of ready fds or -1.

```cpp
private:
    Epoll(const Epoll& copy);
    Epoll& operator=(const Epoll&);
    int m_fd;
};
#endif
```
Private copy ctor/assignment = **non-copyable**. You must not copy an epoll fd (would double-close). In C++98 there is no `= delete`, so you declare them private and don't define them. `m_fd` is the single kernel fd.

### 5.3 `src/network/Epoll.cpp` — Line By Line

```cpp
#include <Logger.hpp>
#include <Epoll.hpp>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <string>

Epoll::Epoll() {
	m_fd = epoll_create1(EPOLL_CLOEXEC);
	if (m_fd == -1)
		throw std::runtime_error("error: epoll_create1() failed");
}
```
`epoll_create1(EPOLL_CLOEXEC)` — Creates an epoll instance with `CLOEXEC` flag, meaning the fd is automatically closed on `execve()` (so CGI children don't inherit it). If it fails (e.g., `EMFILE` out of fds), throw. This throw propagates out of `Multiplexer` constructor to `main()`'s catch.

```cpp
Epoll::~Epoll() {
	if (m_fd != -1)
		close(m_fd);
}
```
RAII close. Check `-1` in case construction threw before assignment (though in that case destructor won't run for partially constructed object, but defensive).

```cpp
int Epoll::add_fd(int fd, AFd *ptr, int events) {
	epoll_event ev;
	int ret;

	ev.data.ptr = ptr;
	ev.events = events;
	ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &ev);

	return ret;
}
```
Boring but critical: `ev.data.ptr = ptr` stores the polymorphic object pointer. When `epoll_wait` returns, `events[i].data.ptr` is that same `AFd*`. `events` bitmask is what you want to watch: `EPOLLIN` for readable, `EPOLLOUT` for writable. Returns `ret` (0 ok, -1 errno). Caller logs on -1.

```cpp
int Epoll::edit_fd(int fd, AFd *ptr, int events) {
	epoll_event ev;

	ev.data.ptr = ptr;
	ev.events = events;
	return epoll_ctl(m_fd, EPOLL_CTL_MOD, fd, &ev);
}
```
Identical but `EPOLL_CTL_MOD`. Used to switch a `Client` from `EPOLLIN` (receiving) to `EPOLLOUT` (sending) and back for keep-alive. Note you must provide `ptr` again; kernel updates both mask and ptr.

```cpp
void Epoll::del_fd(int fd) {
	if (epoll_ctl(m_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
		LOG_WARN << "warning: epoll_ctl() failed to delete fd " << fd;
}
```
`EPOLL_CTL_DEL` ignores the `event` struct (NULL). If it fails (e.g., fd already closed or not in epoll), log warning. This is called from `Multiplexer` before `delete` and from `Client` when handing off to CGI.

```cpp
int	Epoll::wait(epoll_event *events) {
	int readyFds = epoll_wait(m_fd, events, MAXEVENTS, 100);
	return readyFds;
}
```
`epoll_wait` with `timeout=100` ms. **Why 100?** Because `Multiplexer::_handle_timeout()` must run periodically even if no fd is ready. If you used `-1` (infinite block), timeouts would never fire when idle. 100ms gives ~10 checks per second, low CPU overhead, responsive timeouts. Returns number of ready events, 0 if timeout, -1 on error (e.g., `EINTR` when signal arrives). Caller in `Multiplexer` treats `-1` as `continue` (retry).

### 5.4 How You Would Write This

1. Write `Epoll.hpp` with `MAXEVENTS`, `enum EventState`, forward declare `AFd`, declare `m_fd`, disable copy.
2. Implement `Epoll()` with `epoll_create1(EPOLL_CLOEXEC)` + throw.
3. Implement `add_fd`, `edit_fd`, `del_fd`, `wait` as thin wrappers, add logging in `del_fd`.

---

## 6. Chapter 4: `Server` — The Listening Socket (`Server.hpp` / `Server.cpp`)

### 6.1 High-Level Role

One `Server` object = one **listening socket** bound to one `ip:port` (e.g., `0.0.0.0:8080`). But a single `ip:port` can have **multiple `ServerConfig`** (virtual hosts distinguished by `Host` header). So `Server` holds `vector<const ServerConfig*> m_configs` — all configs that share that `host:port`. `craft_key()` canonicalizes `ip:port` to deduplicate.

`Server` lifetime: Created by `Multiplexer` constructor, `run()` creates socket/binds/listens, then `Multiplexer::startup()` adds it to epoll. Forever after, its `handle_event()` is called when a new TCP connection is pending.

### 6.2 `include/network/Server.hpp` — Line By Line

```cpp
#ifndef SERVER_HPP
# define SERVER_HPP
#include <ServerConfig.hpp>
#include <Client.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <exception>
#include <string>
#include <vector>
#include <memory>
#include <list>
#include <map>
```
Notice it includes `Client.hpp` — but `Client.hpp` includes `ServerConfig.hpp` and `Epoll.hpp` etc. No circularity because `Server` does NOT include `Multiplexer.hpp`. `<memory>` is unused (probably leftover `std::shared_ptr` idea). `<list>` needed for `list<Client*>&`.

```cpp
class Server : public AFd
{
public:
  std::vector<const ServerConfig *> m_configs;
  std::string m_key;
  const std::string m_ip;
  sockaddr_in m_addr;
  const int m_port;
```
Public members, intentionally exposed to `Multiplexer` for logging and to `Client::_getConfig()` for vhost selection.

* `m_configs` — vector of `const ServerConfig*` pointing into the `vector<ServerConfig> v_configs` owned by `Multiplexer` caller (ultimately `main()`'s `v_configs`). They are **non-owning pointers**; `Server` does not delete them. The `const` means `Server` promises not to mutate config.
* `m_key` — canonical string like `"127.0.0.1:8080"` (via `craft_key`). Used as map key in `Multiplexer::m_servers`.
* `m_ip` — original ip string (e.g., `"127.0.0.1"` or `"0.0.0.0"`). `const` because it never changes after construction.
* `m_addr` — `sockaddr_in` with `sin_family`, `sin_addr`, `sin_port` filled in constructor. Not const because `bind()` needs it, but logically immutable after construction.
* `m_port` — `const int` port.

```cpp
  Server(const std::string &key, const std::string &ip, int port, const ServerConfig *config, Epoll &epoll, std::list<Client *> &clientsList);
```
Takes already-crafted `key`, ip, port, first config, epoll reference, client list reference. Why references? `Server` does not own `Epoll` or `_clientsList`; `Multiplexer` does. `Server` stores references `_epoll` and `_clientsList` to access them in `handle_event()`.

```cpp
  virtual Epoll::EventState handle_event(uint32_t event);
```
Override. Will `accept()` a client.

```cpp
  void run();
  // void end_connection(int fd);
  void add_config(const ServerConfig *config);
```
`run()` does the three syscalls. `add_config` pushes another vhost onto `m_configs` (though `Multiplexer` constructor does `it->second->m_configs.push_back(...)` directly).

```cpp
  static std::string craft_key(const std::string &ip, int port);
```
Static helper to produce canonical `ip:port` string. It uses `getaddrinfo` + `inet_ntop` to ensure `127.0.0.1` and `localhost` that resolves to `127.0.0.1` map to same key? Actually it does `getaddrinfo(ip)` then `inet_ntop` of result, so `"localhost"` would become `"127.0.0.1:8080"` if `hosts` contains `"localhost"`. Clever canonicalization.

```cpp
  virtual ~Server();
private:
  Epoll &_epoll;
	std::list<Client *> &_clientsList;
```
References must be initialized in initializer list and never reseated.

```cpp
  void create_socket();
  void bind_address();
  void start_listening();
  int accept_connection();
};
typedef std::map<std::string, Server*> ServersMap;
#endif
```
`ServersMap` alias used by `Multiplexer`.

### 6.3 `src/network/Server.cpp` — Line By Line

#### Constructor

```cpp
Server::Server(const std::string &key, const std::string &ip, int port,
               const ServerConfig *config, Epoll &epoll,
               std::list<Client *> &clientsList)

    : AFd(-1, AFd::SERVER), m_key(key),
      m_ip(ip), m_port(port), _epoll(epoll),
      _clientsList(clientsList)
{
    m_configs.push_back(config);
    std::memset(&this->m_addr, 0, sizeof(m_addr));
    addrinfo hints, *res;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(ip.c_str(), NULL, &hints, &res) != 0)
    {
        throw std::runtime_error(ip + " is not a valid ip address");
    }
    else
    {
        this->m_addr = *((sockaddr_in *)res->ai_addr);
        freeaddrinfo(res);
    }
    m_addr.sin_port = htons(port);
}
```

* `AFd(-1, SERVER)` — fd is -1 until `create_socket()` fills it. Type is SERVER.
* `m_key(key)` etc — initializer list must initialize `const` members.
* `m_configs.push_back(config)` — first vhost.
* `memset(&m_addr,0,...)` — zero before filling.
* `getaddrinfo(ip.c_str(), NULL, ...)` — Resolves ip string to binary `sockaddr`. `hints.ai_family=AF_INET` restricts to IPv4. `SOCK_STREAM` is TCP. If `ip` is `"0.0.0.0"`, this succeeds and gives `INADDR_ANY`. If `ip` is garbage, throw.
* `this->m_addr = *((sockaddr_in *)res->ai_addr)` — Copy the resulting address. `res->ai_addr` is `sockaddr*` but we know it's `sockaddr_in` because `AF_INET`. Then `freeaddrinfo(res)` — must free the linked list allocated by `getaddrinfo`.
* `m_addr.sin_port = htons(port)` — `htons` = host-to-network-short: converts port to network byte order (big-endian). Without this, port 8080 would be byte-swapped.

#### Destructor

```cpp
Server::~Server()
{
    //
}
```
Empty! Why doesn't it close fd? Because base `~AFd()` will `close(m_fd)`. And `Multiplexer::_deleteServers()` does `epoll.del_fd` before `delete`. So no work needed here.

#### `add_config`, `run`, `create_socket`

```cpp
void Server::add_config(const ServerConfig *config)
{
    m_configs.push_back(config);
}

void Server::run()
{
    create_socket();
    bind_address();
    start_listening();
}

void Server::create_socket()
{
    this->m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->m_fd == -1)
    {
        throw std::runtime_error("error: socket() for " + m_key + " failed");
    }
}
```
`socket(AF_INET, SOCK_STREAM, 0)` — TCP/IPv4. Protocol 0 means default (TCP). On success returns fd >=3. On failure -1 with errno (`EMFILE`, `ENFILE`, `ENOMEM`, `EACCES`). Throw with `m_key` for context.

#### `bind_address`

```cpp
// TODO : make code more readable
void Server::bind_address()
{
    int opt = 1;
    if (setsockopt(this->m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
    {
        throw std::runtime_error("error: setsockopt() failed on " + m_key);
    }
    if (bind(this->m_fd, reinterpret_cast<sockaddr *>(&this->m_addr), sizeof(m_addr)) != 0)
    {
        throw std::runtime_error("error: bind() failed on " + m_key);
    }
}
```
* `setsockopt(SO_REUSEADDR)` — Allows restarting server quickly without `EADDRINUSE` from TIME_WAIT sockets. `opt=1` enables. Without this, after you kill webserv and restart within ~60s, `bind` would fail.
* `bind()` — Associates fd with `m_addr` (ip+port). `reinterpret_cast<sockaddr*>` is required because `bind` takes generic `sockaddr`. On failure (e.g., port already in use, permission denied for port <1024, ip not local), throw.

#### `start_listening`

```cpp
void Server::start_listening()
{
    if (listen(this->m_fd, SOMAXCONN) != 0)
    {
        throw std::runtime_error("error: listen() for " + m_key + " failed");
    }
}
```
`listen(fd, SOMAXCONN)` — Marks socket as passive, willing to accept connections. `SOMAXCONN` is kernel max backlog (typically 4096 or 128). Backlog = queue of completed TCP handshakes waiting for `accept()`.

#### `accept_connection`

```cpp
// return 0 on success -1 if failed
int Server::accept_connection()
{
    int clientFd;

    // TODO : catch client infos
    clientFd = accept4(this->m_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);

    return clientFd;
}
```
`accept4` is Linux-specific `accept` with flags. Older `accept()` would require separate `fcntl` to set nonblock/cloexec. `SOCK_CLOEXEC` = close-on-exec (so CGI `execve` doesn't leak client fds), `SOCK_NONBLOCK` = non-blocking (so `recv`/`send` never block the event loop). Passing `NULL` for addr/len means we don't care about peer IP/port (TODO notes it). Returns new fd or -1 (`EAGAIN` if nonblocking and no pending, `EMFILE` if out of fds, `ECONNABORTED`, etc.).

#### `handle_event`

```cpp
Epoll::EventState Server::handle_event(uint32_t event)
{
    (void)event;
    int clientFd = accept_connection();
    if (clientFd == -1)
    {
        LOG_WARN << "accept4() failed on " << m_key;
        return Epoll::ECONTINUE;
    }
    LOG_INFO << "Accepted a client as fd " << clientFd;
    Client *client = new Client(clientFd, _epoll, m_configs);
    if (_epoll.add_fd(clientFd, static_cast<AFd *>(client), EPOLLIN) != 0)
    {
        LOG_WARN << "epoll_ctl() failed to add fd " << clientFd << " for " << m_key;
        _epoll.del_fd(clientFd);
        delete client;
        LOG_INFO << "Ended connection with client on fd " << clientFd;
    }
    else
    {
        _clientsList.push_back(client);
        client->m_it = --_clientsList.end();
    }
    return Epoll::ECONTINUE;
}
```

* `(void)event` — Server doesn't care what epoll event bits were; `EPOLLIN` is the only one it registered for, but edge-triggered `EPOLLERR` could theoretically fire (though we ignore, treat as accept attempt).
* `accept_connection()` — Try to get a client.
* If `-1`, log and `ECONTINUE` — keep server in epoll, don't kill server.
* If success, `new Client(clientFd, _epoll, m_configs)` — heap-allocate a Client. Why heap? Because we store `Client*` in `list` and `epoll.data.ptr` must remain valid until we delete it. Stack would die.
* `epoll.add_fd(clientFd, client, EPOLLIN)` — Watch new client for readable. If this fails (rare: `EEXIST`, `ENOMEM`, `ENOSPC`), we clean up: `del_fd` (no-op if add failed, but defensive), `delete client` which closes clientFd via `~AFd`.
* Else, `_clientsList.push_back(client); client->m_it = --_clientsList.end()` — Store in Multiplexer's list and give `Client` an iterator to itself. Why? So `Client` can be O(1) erased by `Multiplexer::events_loop()` without searching. And also for LRU timeout handling: we move clients to back on activity.
* Return `ECONTINUE` always (never `EFINISHED` for Server; Server never closes itself).

#### `craft_key`

```cpp
std::string Server::craft_key(const std::string &ip, int port)
{
    std::stringstream ssKey;
    addrinfo hints, *res;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(ip.c_str(), NULL, &hints, &res) != 0)
    {
        throw std::runtime_error(ip + " is not a valid ip address");
    }
    else
    {
        sockaddr_in addr;
        char buffer[16];

        addr = *((sockaddr_in *)res->ai_addr);
        if (inet_ntop(AF_INET, &addr.sin_addr, buffer, INET_ADDRSTRLEN) == NULL)
            throw std::runtime_error("error: inet_ntop() failed for " + ip);
        ssKey << &buffer[0];
        ssKey << ':';
        ssKey << port;
        freeaddrinfo(res);
    }
    return ssKey.str();
}
```
Canonicalizes `"0.0.0.0"` and `"127.0.0.1"` etc. by resolving then `inet_ntop` which prints normalized dotted-decimal. `buffer[16]` holds max IPv4 string `"255.255.255.255"` + null. `INET_ADDRSTRLEN=16`. Then `":port"` appended. Example: `craft_key("localhost",8080)` would resolve localhost to `127.0.0.1` and return `"127.0.0.1:8080"` — thus configs with `host localhost;` and `host 127.0.0.1;` collapse to same Server.

---

## 7. Chapter 5: `Client` — The Active Connection And HTTP State Machine (`Client.hpp` / `Client.cpp`)

This is the **largest and most boring** file. It is 478 lines of state machine, `recv`/`send`/`sendfile`, CGI delegation, header parsing, keep-alive.

### 7.1 High-Level State Machine

```cpp
enum e_state {
  CKEEPT_ALIVE,      // idle, waiting for next request, timeout=17s
  CRECEVING,         // reading request bytes
  CSENDING_HEADERS,  // sending response header buffer
  CSENDING_BODY,     // sendfile() static file body
  CEXECUTING_CGI,    // waiting for CGI child, watching pipe
  CFINISHED,         // done sending
  CTIMEDOUT          // timed out, will send 408
};
```

Transitions:

```
new Client -> CRECEVING
CRECEVING --recv+parse complete--> (maybe CEXECUTING_CGI else CSENDING_HEADERS)
CEXECUTING_CGI --pipe DONE/FAIL--> CSENDING_HEADERS
CSENDING_HEADERS --send headers done--> CSENDING_BODY (if has file) or CFINISHED
CSENDING_BODY --sendfile done--> CFINISHED
CFINISHED --Connection: keep-alive--> CKEEPT_ALIVE -> (reset) -> CRECEVING
CFINISHED --Connection: close--> EFINISHED (Multiplexer deletes)
CRECEVING/CTIMEDOUT/etc --timeout--> handleTimeout()
```

`Client` also owns:

* `HttpRequest _request` — incremental parser, state `REQUEST_LINE`→`HEADERS`→`BODY`→`COMPLETE`/`ERROR`. Wraps `_buffer` and temp file for body.
* `HttpResponse _response` — builder with `header_buffer`, `file_fd`, `file_size`.
* `Cgi* _cgi` — nullable, heap-allocated when CGI needed.
* `time_t m_lastActivity` — updated on every successful event; used by `Multiplexer::_handle_timeout()` LRU list.
* `list<Client*>::iterator m_it` — self-iterator for O(1) removal/move.
* `ssize_t _bytes_sent`, `off_t _file_offset`, `off_t _cgi_body_off` — progress counters for non-blocking partial sends.

### 7.2 `include/network/Client.hpp` — Line By Line

```cpp
#ifndef CLIENT_HPP
# define CLIENT_HPP
#include <RequestHandler.hpp>
#include <ServerConfig.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <Cgi.hpp>
#include <vector>
#include <ctime>
#include <list>

#define APP_BUFFER_SIZE 8192 // 8Kb
```
`APP_BUFFER_SIZE 8192` — App-level recv/send chunk size. 8KB is typical page-aligned buffer. Used for `recv(m_fd, buffer, 8192, 0)` and `sendfile(...,8192)`.

```cpp
class Client : public AFd
{
  public:
    enum e_state {CKEEPT_ALIVE, CRECEVING, CSENDING_HEADERS, CSENDING_BODY, CEXECUTING_CGI, CFINISHED, CTIMEDOUT};

    time_t m_lastActivity;
    e_state m_state;
    std::vector<const ServerConfig *> &m_configs;
    std::list<Client *>::iterator m_it;
```
Public for `Multiplexer` to inspect/move. `m_configs` is a **reference to `Server::m_configs`** (vector of const pointers). So all clients on same `host:port` share the same config vector reference (but it's per-Server, not global). `m_it` is set by `Server::handle_event()` after `push_back`.

```cpp
    Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs);

    virtual Epoll::EventState handle_event(uint32_t event);
    void handleTimeout();

    int startCgi(const std::string &interpreter,
                  const std::string &script_path, int body_fd);

    virtual ~Client();

  private:
    // sockaddr_in _client_addr;
    Epoll &_epoll;
    HttpRequest _request;
    // RequestHandler *_rqst_handler;
    HttpResponse _response;
    ssize_t _bytes_sent;
    off_t _file_offset;

    Cgi *_cgi;
    time_t _cgi_start;
    off_t _cgi_body_off;

    bool _parseCgiHeaders(const std::string &block,
                          HttpStatus::Code &status,
                          bool &explicit_status,
                          std::map<std::string, std::string> &headers,
                          bool &has_location);

    bool _extractStatusLine(const std::string &line, HttpStatus::Code &status);
    bool _parseStatusNumber(const std::string &s, HttpStatus::Code &status);
    std::string _lower(const std::string& s) const;
  
    Epoll::EventState _receiveData();
    Epoll::EventState _sendData();
    Epoll::EventState _handleCgiEvent();
    void              _cgiTimeout();
    void              _buildCgiResponse();
    void              _buildError(HttpStatus::Code errCode);
    const ServerConfig *_getConfig(const std::string &host);

    void _reset();
};
#endif
```
Helper breakdown:

* `_receiveData()` — `recv` loop, feed `HttpRequest::parse`, call `RequestHandler::handle()`, maybe `startCgi()`, switch to `EPOLLOUT`.
* `_sendData()` — `send` headers then `sendfile` body, handle partial writes, keep-alive.
* `_handleCgiEvent()` — `Cgi::readOutput()`, then `_buildCgiResponse()` or `_buildError(502)`.
* `_cgiTimeout()` — kill CGI, build `504`.
* `_buildCgiResponse()` — parse CGI output file, extract headers/body, fill `_response`.
* `_parseCgiHeaders()` etc — CGI spec header parsing.
* `_getConfig()` — Virtual host selection by `Host` header.
* `_reset()` — Clear request/response for keep-alive reuse.

### 7.3 `src/network/Client.cpp` — Line By Line

#### Includes and Constructor

```cpp
#include <RequestHandler.hpp>
#include <HttpRequest.hpp>
#include <timeout.hpp>
#include <Client.hpp>
#include <Logger.hpp>
#include <Epoll.hpp>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <cstring>

Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd, AFd::CLIENT), m_lastActivity(time(NULL)), m_state(CRECEVING),
      m_configs(configs), _epoll(epoll), _request(fd), _bytes_sent(0),
      _file_offset(0), _cgi(NULL), _cgi_start(0), _cgi_body_off(0)
{
}
```
* `AFd(fd, CLIENT)` — client type.
* `m_lastActivity(time(NULL))` — start timeout clock now.
* `m_state(CRECEVING)` — immediately expecting bytes.
* `_request(fd)` — `HttpRequest` takes client fd (so it can create temp file named after fd?).
* `_bytes_sent=0`, `_file_offset=0` — progress for sending.
* `_cgi(NULL)` — no CGI yet.
* `_cgi_start=0`, `_cgi_body_off=0` — CGI timing/offset.

#### `_receiveData()` — The 70-Line Heart

```cpp
Epoll::EventState Client::_receiveData()
{
    char buffer[APP_BUFFER_SIZE + 1];

    ssize_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == -1 || bytes == 0)
    {
        LOG_WARN << "recv() returned " << bytes << " on client with fd " << m_fd;
        return Epoll::EERROR;
    }
    buffer[bytes] = '\0';

    _request.parse(buffer, static_cast<size_t>(bytes));
    if (_request.getState() == HttpRequest::COMPLETE || _request.getState() == HttpRequest::ERROR)
    {
        const std::string host = _request.getHeader("host");
        const ServerConfig &conf = *_getConfig(host);
        // if (_request.isCgi())
        // {

        // }
        RequestHandler rqst_handler(_request, _response, conf);
        rqst_handler.handle();
        if (rqst_handler.isCgi()) {
            int body_fd = rqst_handler.getBodyFd();
            std::string body_path = rqst_handler.getBodyFilePath();
            std::string upload_dst = rqst_handler.getUploadDestination();
            std::string script = rqst_handler.getCgiScriptPath();
            std::string interp = rqst_handler.getCgiInterpreter();
            if (interp.empty() || script.empty())
            {
                if (body_fd != -1)
                    ::close(body_fd);
            }
            else if (startCgi(interp, script, body_fd) != 0)
            {
                // the error is built inside startCgi()
            }
            else
                return Epoll::ECONTINUE;
        }
        m_state = CSENDING_HEADERS;
        if (_epoll.edit_fd(m_fd, this, EPOLLOUT) != 0)
            return Epoll::EERROR;
    }

    return Epoll::ECONTINUE;
}
```

* `recv(m_fd, buffer, 8192, 0)` — Non-blocking recv. Returns -1 with `EAGAIN/EWOULDBLOCK` if no data (should not happen because epoll said EPOLLIN, but can if edge-triggered or race), 0 if peer closed, >0 bytes read. This codebase treats -1 and 0 both as `EERROR` (close connection). Some servers treat `EAGAIN` as not error, but this code is level-triggered (default epoll), so it just errors and closes — acceptable for 42.
* `buffer[bytes]='\0'` — Null-terminates for `parse()` that may treat it as string (but also passes len).
* `_request.parse(buffer, bytes)` — Incremental parse. `HttpRequest` handles request line, headers, chunked/body. It updates internal `_state`. If not COMPLETE/ERROR, we return `ECONTINUE` and wait for more `EPOLLIN` events to call `_receiveData` again. So a request can be split across multiple `recv` calls (TCP segmentation).
* When `COMPLETE` or `ERROR`, we have a full logical request (or a parse error). Now we need to generate response.
* `host = _request.getHeader("host")` — For vhost selection. Note headers are lowercased inside `HttpRequest` (so "host" works).
* `conf = *_getConfig(host)` — Find matching `ServerConfig` by `server_name`. If no match, returns `m_configs[0]` (default).
* `RequestHandler rqst_handler(...)` — This object does filesystem checks: is URI mapped to file/dir? does method allowed? is it CGI? It fills `_response` with status, headers, file_fd etc. `handle()` is synchronous (blocks on `stat`, `open`). Could be slow but not network-block.
* `if (rqst_handler.isCgi())` — CGI path. Note `RequestHandler` has already prepared body file (temp file containing request body) if `POST` with body. It exposes `body_fd`, `script_path`, `interpreter`.
* `interp.empty() || script.empty()` — Misconfiguration: no interpreter/script, cannot CGI. Close body_fd (prevent leak) and fall through to normal `CSENDING_HEADERS` (but response may already be error? Actually handler would have built error).
* `startCgi(...)` — Forks. If it returns 0 success, we `return ECONTINUE` early — **do NOT switch to EPOLLOUT**. Instead `startCgi` removed client fd from epoll and added CGI pipe fd. So this client will now wait for CGI pipe events, not socket events. The early return prevents the `m_state=CSENDING_HEADERS` and `edit_fd(EPOLLOUT)` below.
* If `startCgi` fails (returns -1), it has already built `_buildError(500)` and set `m_state=CSENDING_HEADERS` and `edit_fd(EPOLLOUT)` inside `startCgi`. So we fall through but the else `return` didn't happen, and the code below would redundantly set `CSENDING_HEADERS` again? Actually inside `startCgi` on failure it does `_buildError` + `m_state=CSENDING_HEADERS` + `edit_fd`. Then here we again `m_state=CSENDING_HEADERS` + `edit_fd`. Redundant but harmless.
* If not CGI, or CGI not started, we `m_state=CSENDING_HEADERS` and `edit_fd(m_fd, EPOLLOUT)` — switch interest from reading to writing. If `edit_fd` fails, `EERROR`.
* Return `ECONTINUE`.

**Boring detail:** Why `body_fd` is passed and then closed inside `Cgi::execute()`? Because after fork, parent no longer needs body_fd; child dup2s it to stdin. The parent closes its copy to not leak. If CGI not started, caller closes it (seen above).

#### `_sendData()` — Two-Phase Send

```cpp
Epoll::EventState Client::_sendData()
{
    if (m_state == CSENDING_HEADERS || m_state == CTIMEDOUT)
    {
        std::string headers = _response.getHeaderBuffer();

        {
            ssize_t headers_bytes_sent = send(m_fd, headers.c_str() + _bytes_sent, headers.size() - _bytes_sent, 0);
            if (headers_bytes_sent == -1 || headers_bytes_sent == 0)
            {
                LOG_WARN << "send() returned " << headers_bytes_sent << " on client with fd " << m_fd;
                return Epoll::EERROR;
            }
            _bytes_sent += headers_bytes_sent;
        }

        if (static_cast<size_t>(_bytes_sent) == headers.size())
        {
            if (_response.hasFile())
            {
                _bytes_sent = 0;
                m_state = CSENDING_BODY;
                return Epoll::ECONTINUE;
            }
            else
                m_state = CFINISHED;
        }
        else
        {
            return Epoll::ECONTINUE;
        }
    }
    if (m_state == CSENDING_BODY)
    {
        ssize_t body_bytes_sent = sendfile(m_fd, _response.getFileFd(), &_file_offset, APP_BUFFER_SIZE);
        if (body_bytes_sent == 0 && _file_offset == _response.getFileSize())
            m_state = CFINISHED;
        else if (body_bytes_sent == -1 || body_bytes_sent == 0)
        {
            LOG_WARN << "sendfile() returned " << body_bytes_sent << " on client with fd " << m_fd;
            return Epoll::EERROR;
        }
        if (_file_offset == _response.getFileSize())
            m_state = CFINISHED;
        else
        {
            return Epoll::ECONTINUE;
        }
    }
    if (m_state == CFINISHED && _request.getHeader("connection") == "keep-alive")
    {
        m_state = CKEEPT_ALIVE;
        if (_epoll.edit_fd(m_fd, this, EPOLLIN))
            return Epoll::EERROR;
        _reset();
        return Epoll::ECONTINUE;
    }
    return Epoll::EFINISHED;
}
```

* `CSENDING_HEADERS` block: Get header string (includes status line, Content-Length, Content-Type, Connection, etc.). `send(m_fd, headers.c_str()+_bytes_sent, remaining, 0)` — non-blocking send. May send partially (e.g., 4096 of 8000 bytes) because socket buffer full. `_bytes_sent` tracks progress across multiple `EPOLLOUT` readiness events. If `send` returns -1 (`EAGAIN` or `EPIPE` etc.) or 0 (should not happen for send), `EERROR`.
* When all headers sent (`_bytes_sent==size`), check `hasFile()`. If yes, reset `_bytes_sent=0` (reuse for body? Actually body uses `_file_offset`), transition `CSENDING_BODY` and `ECONTINUE` (will need another `EPOLLOUT` to send body, but we return `ECONTINUE` without `edit_fd` because we already are on `EPOLLOUT`). If no file (e.g., 204, error page with string body already in header buffer? Actually `HttpResponse` may have `_body_string` appended to headers? In this code, body_file vs body_string: if `hasFile()==false`, response is fully in headers, so `CFINISHED`.
* If not all headers sent, `ECONTINUE` (wait for next `EPOLLOUT`).
* `CSENDING_BODY` block: `sendfile(out_fd=m_fd, in_fd=_response.getFileFd(), offset=&_file_offset, count=8192)` — Zero-copy send. Kernel copies file bytes directly to socket without user-space copy. `&_file_offset` is both input (offset to start) and output (offset after sent). Returns bytes sent or -1. Need loop: if we sent some and not yet at file size, `ECONTINUE` (remain `CSENDING_BODY`, wait for next `EPOLLOUT`). If we reached EOF (`_file_offset == fileSize`), `CFINISHED`.
* Note the double check: `if (bytes==0 && offset==size) CFINISHED` and then later `if (offset==size) CFINISHED`. The first handles case where `sendfile` returns 0 because offset already at EOF (file size 0). Second handles normal.
* `CFINISHED` block: Check `Connection: keep-alive` (case-sensitive? `_request.getHeader` lowercases, so "keep-alive" check works). If keep-alive, switch back to `EPOLLIN`, reset request/response, `ECONTINUE` (keep connection open). Otherwise, `EFINISHED` — Multiplexer will `del_fd` and `delete client`. This is the **terminal state** for non-keep-alive.
* Note `CTIMEDOUT` is handled same as `CSENDING_HEADERS` (first if). That state is set by `handleTimeout()` when main timeout fires while receiving; we built `408 Request Timeout` error response and want to send it.

**Boring syscall nuance:** `sendfile` can return `-1` with `EAGAIN` if socket buffer full; caller treats as `EERROR` and kills connection. A more robust server would handle `EAGAIN` as `ECONTINUE` (retry), but this simplified version kills. Similarly `send` with `EAGAIN` kills. For 42 evaluator this often passes; for production you'd check `errno == EAGAIN`.

#### `handle_event()` — The Dispatcher

```cpp
Epoll::EventState Client::handle_event(uint32_t event)
{
    // to do
    if (m_state == CEXECUTING_CGI)
    {
        if (_cgi != NULL && time(NULL) - _cgi_start > CGI_TIMEOUT)
        {
            _cgiTimeout();
            return Epoll::ECONTINUE;
        }
        return _handleCgiEvent();
    }
    if (event & EPOLLERR || event & EPOLLHUP || event & EPOLLRDHUP)
    {
        return Epoll::EERROR;
    }
    if (event & EPOLLIN)
    {
        m_state = CRECEVING;
        return _receiveData();
    }
    if (event & EPOLLOUT)
    {
        return _sendData();
    }
    return Epoll::EFINISHED;
}
```

* First check: If `CEXECUTING_CGI`, we are **not watching client fd** but **CGI pipe fd**. So any event on this `Client` object actually means **pipe readable**. So we check CGI timeout first (if 16s elapsed, kill). Then delegate to `_handleCgiEvent()` regardless of `event` bits. Why? Because `_handleCgiEvent` will `read()` the pipe and check `waitpid`.
* Otherwise, handle socket events. `EPOLLERR|HUP|RDHUP` means peer closed or error → `EERROR`.
* `EPOLLIN` → receiving. Note it sets `m_state=CRECEVING` even if previously `CKEEPT_ALIVE`; okay.
* `EPOLLOUT` → sending.
* Fallthrough `EFINISHED` — if event is 0 or unknown, close.

#### `startCgi()` — Fork Delegation

```cpp
int Client::startCgi(const std::string &interpreter, 
                    const std::string &script_path, int body_fd)
{
    _cgi = new Cgi(_request, interpreter, script_path, body_fd);
    _cgi_start = time(NULL);

    if (_cgi->execute() != 0)
    {
        delete _cgi; _cgi = NULL;
        _buildError(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
        return -1;
    }
    m_state = CEXECUTING_CGI;
    if (_epoll.add_fd(_cgi->getReadEnd(), this, EPOLLIN) != 0)
    {
        delete _cgi; _cgi = NULL;
        _buildError(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
        return -1;
    }
    _epoll.del_fd(m_fd);
    return 0;
}
```

* `new Cgi(...)` — heap allocate, does `_setArgv` and `_setEnv` in ctor.
* `_cgi_start = time(NULL)` — for timeout.
* `_cgi->execute()` — `pipe()`+`mkstemp()`+`fork()`+`execve`. If -1, build 500 error, switch to send headers.
* On success, `m_state=CEXECUTING_CGI` and `add_fd(pipeReadEnd, this, EPOLLIN)` — Now epoll watches the CGI pipe, with same `Client*` ptr. When pipe becomes readable or child exits, `handle_event` will be called again (but via the pipe fd's events).
* `del_fd(m_fd)` — **Stop watching client socket** while CGI runs. The client socket stays open but not in epoll. We will re-add it after CGI finishes. This prevents `recv`/`send` interleaving while CGI is running.
* Return 0.

#### `_handleCgiEvent()`, `handleTimeout()`, `_buildError()`, `_cgiTimeout()`

```cpp
Epoll::EventState Client::_handleCgiEvent() {
    Cgi::e_out result = _cgi->readOutput();

    if (result == Cgi::MORE)
        return Epoll::ECONTINUE;
    
    _epoll.del_fd(_cgi->getReadEnd());
    if (result == Cgi::FAIL || !_cgi->exitedCleanly())
    {
        delete _cgi; _cgi = NULL;
        _buildError(HttpStatus::BadGateway);
    }
    else
    {
        _buildCgiResponse();
        delete _cgi; _cgi = NULL;
    }
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);
    return Epoll::ECONTINUE;
}
```

* `readOutput()` reads from pipe's read-end (`_notify[0]`). If `MORE` (child still running, pipe has data but we didn't drain? Actually `readOutput` tries `read(junk,64)`; if bytes>0 return MORE, meaning child still writing but we haven't reaped), we `ECONTINUE` (keep watching pipe).
* If `DONE` (bytes==0 and waitpid says child exited or killed), we `del_fd(pipe)`, then check `FAIL` or not clean exit → 502 Bad Gateway. Else build CGI response from output file.
* Then re-add client socket with `EPOLLOUT` to send response.

```cpp
void Client::handleTimeout()
{
    if (m_state == CEXECUTING_CGI && _cgi != NULL)
    {
        _cgiTimeout();
    }
    else
    {
        m_state = CTIMEDOUT;
        _buildError(HttpStatus::RequestTimeout);
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
    }
}

void Client::_buildError(HttpStatus::Code errCode) {
    _reset();
    _request.setErrorCode(errCode);
    _request.setState(HttpRequest::ERROR);
    RequestHandler rqst_handler(_request, _response, *m_configs[0]);
    rqst_handler.handle();
}

void Client::_cgiTimeout() {
    LOG_WARN << "CGI timed out on client with fd " << m_fd;
    _epoll.del_fd(_cgi->getReadEnd());
    delete _cgi; _cgi = NULL;
    _buildError(HttpStatus::GatewayTimeout);
    m_state = CSENDING_HEADERS;
    _epoll.add_fd(m_fd, this, EPOLLOUT);
}
```

* `handleTimeout()` is called by `Multiplexer::_handle_timeout()` when `now - lastActivity > timeout`. Two cases:
  * CGI executing → kill child and 504.
  * Otherwise → 408 Request Timeout, switch to `EPOLLOUT`.
* `_buildError` resets response, sets request to ERROR with code, then reuses `RequestHandler` to build error page (looks for `error_page` directive, or default).
* `_cgiTimeout` is more aggressive: it `del_fd(pipe)`, `delete _cgi` which `killChild()` (SIGKILL) and closes fds, then 504.

#### `_buildCgiResponse()` — The 100-Line Parser

This parses CGI output file (`/tmp/_cgi_XXXXXX`) which contains HTTP-like response from script: headers + blank line + body.

```cpp
void Client::_buildCgiResponse() {
    static const size_t MAX_CGI_HEADERS = 64 * 1024;

    struct stat st;
    if (_cgi->getOutputFd() == -1 || fstat(_cgi->getOutputFd(), &st) != 0)
    {
        LOG_ERROR << "cannot fstat cgi output fd";
        _buildError(HttpStatus::BadGateway);
        return ;
    }

    const off_t file_size = st.st_size;
    std::string window;
    char chunk[4096];
    off_t read_at = 0;
    size_t body_start = std::string::npos;
    ssize_t n;

    while ((n = pread(_cgi->getOutputFd(), chunk, sizeof(chunk), read_at)) > 0)
    {
        window.append(chunk, static_cast<size_t>(n));
        read_at += n;

        size_t crlf = window.find("\r\n\r\n");
        size_t lf = window.find("\n\n");
        if (crlf != std::string::npos && (lf == 
            std::string::npos || crlf < lf))
        {
            body_start = crlf + 4;
            break;
        }
        if (lf != std::string::npos)
        {
            body_start = lf + 2;
            break;
        }
        if (window.size() > MAX_CGI_HEADERS)
        {
            LOG_WARN << "CGI response exceed ther limit";
            _buildError(HttpStatus::BadGateway);
            return;
        }
    }
    if (n == -1)
    {
        LOG_ERROR << "pread(cgi_output) -> " << -1;
        _buildError(HttpStatus::BadGateway);
        return;
    }
    if (body_start == std::string::npos)
    {
        LOG_WARN << "CGI output has no header terminator";
        _buildError(HttpStatus::BadGateway);
        return;
    }
    HttpStatus::Code status = HttpStatus::OK;
    bool explicit_status = false;
    bool has_location = false;
    std::map<std::string, std::string> headers;
    window = window.substr(0, body_start);
    if (!_parseCgiHeaders(window, status, explicit_status,
        headers, has_location))
    {
        LOG_WARN << "CGI output contains malformed headers";
        _buildError(HttpStatus::BadGateway);
        return;
    }
    if (!explicit_status && has_location)
        status = HttpStatus::Found;
    
    const off_t body_size = file_size - static_cast<off_t>(body_start);
    
    _response.setStatusCode(status);

    std::map<std::string, std::string>::const_iterator it;
    for (it = headers.begin(); it != headers.end(); ++it)
    {
        const std::string lname = _lower(it->first);
        if (lname == "status" || lname == "content-length")
            continue;
        if (_request.getHeader("Cookie") != "")
        {
            _response.setHeader("Set-Cookie", _request.getHeader("Cookie"));
        }
        _response.setHeader(it->first, it->second);
    }
    if (body_size == 0)
        _response.setBody("");
    else
    {
        if (!_response.setFileBody(_cgi->getOutputPath()))
        {
            LOG_ERROR << "cannot reopen cgi output file";
            _buildError(HttpStatus::BadGateway);
            return ;
        }
        std::ostringstream oss;
        oss << body_size;
        _response.setHeader("Content-Length", oss.str());

        _cgi_body_off = static_cast<off_t>(body_start);
        _file_offset = _cgi_body_off;
    }
    _response.build();
}
```

* `fstat` output fd to get `file_size`. If fail → 502.
* `pread` loop: read chunk by chunk at offset `read_at` without moving file offset (so not disturbing other offset). Accumulate into `window` until we find header terminator `\r\n\r\n` or `\n\n`. Limit `64KB` headers to prevent infinite allocation if script never sends blank line.
* `body_start` is index after terminator (where body begins).
* If `n==-1` (pread error) or `body_start==npos` (no terminator) → 502.
* `_parseCgiHeaders(window.substr(0,body_start), ...)` parses headers. If fails → 502.
* If no explicit status but has `Location`, default to `302 Found` (CGI spec: Location without Status means redirect).
* Then generate response: set status, copy headers except `Status` and `Content-Length` (we compute), also inject `Set-Cookie` if request had Cookie (odd logic: it copies request Cookie as Set-Cookie — maybe bug, but spec says CGI can output Set-Cookie; here they propagate? Might be copy-paste).
* If body_size==0, empty body.
* Else `_response.setFileBody(_cgi->getOutputPath())` — reopen temp file as `HttpResponse` file body. Set `Content-Length` to body_size. Set `_cgi_body_off` and `_file_offset` to `body_start` so `sendfile` starts at body, not headers. Then `_response.build()` builds header buffer.

**Boring detail:** `pread` is used instead of `read` + `lseek` to be thread-safe, but here single-thread so equivalent. `window.substr(0,body_start)` discards body part of window after header parse — okay because body will be sent via `sendfile` from same file at offset `body_start`.

#### Header Parsing Helpers

```cpp
bool Client::_parseCgiHeaders(const std::string &block,
    HttpStatus::Code &status, bool &explicit_status,
    std::map<std::string, std::string> &headers, bool &has_location)
{
    size_t start = 0;
    while (start < block.size())
    {
        size_t nl = block.find('\n', start);
        if (nl == std::string::npos)
            nl = block.size();
        std::string line = block.substr(start, nl - start);
        start = nl + 1;

        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.empty())
            break;
        if (line.find('\r') != std::string::npos)
            return false;

        if (line.compare(0, 5, "HTTP/") == 0)
        {
            if (!_extractStatusLine(line, status))
                return false;
            explicit_status = true;
            continue;
        }
        if (_lower(line).compare(0, 7, "status:") == 0)
        {
            if (!_parseStatusNumber(line.substr(7), status))
                return false;
            explicit_status = true;
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0)
            return false;
        std::string name  = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        for (size_t i = 0; i < name.size(); ++i)
        {
            char c = name[i];
            bool ok = ::isalnum(c) || c == '-' || c == '_';
            if (!ok)
                return false;
        }
        size_t b = value.find_first_not_of(" \t");
        if (b == std::string::npos)
            value.clear();
        else
        {
            size_t e = value.find_last_not_of(" \t");
            value = value.substr(b, e-b+1);
        }
        for (size_t i = 0; i < value.size(); ++i)
        {
            if ((unsigned char)value[i] < 32 && value[i] != '\t')
                return false;
        }
        if (_lower(name) == "location")
            has_location = true;
        headers[name] = value;
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
    while (i < s.size() && isdigit((unsigned char)s[i]))
    {
        code = code * 10 + (s[i] - '0');
        if (code > 999)
            return false;
        ++i;
    }
    if (code < 100)
        return false;
    status = static_cast<HttpStatus::Code>(code);
    return true;
}

std::string Client::_lower(const std::string &s) const {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = static_cast<char>(::tolower((unsigned char)out[i]));
    }
    return out;
}
```

* `_parseCgiHeaders` iterates line-by-line splitting on `\n`, strips trailing `\r`, rejects stray `\r` inside, handles:
  * `HTTP/1.1 200 OK` → `_extractStatusLine` → `_parseStatusNumber` → explicit_status true.
  * `Status: 302` → parse status number.
  * `Name: value` → validate name chars (alnum, `-`, `_`), trim whitespace around value, validate control chars, detect `Location`.
* `_parseStatusNumber` skips leading whitespace, parses 3-digit code, rejects >999 or <100.

#### `_getConfig`, `_reset`, Destructor

```cpp
const ServerConfig *Client::_getConfig(const std::string &host)
{
    for (size_t i = 0; i < m_configs.size(); i++)
    {
        // todo : use std::find
        for (size_t n = 0; n < m_configs[i]->names.size(); n++)
        {
            if (m_configs[i]->names[n] == host)
            {
                return m_configs[i];
            }
        }
    }
    LOG_INFO << "Client with fd " << m_fd << " will use the default config";
    return m_configs[0];
}

void Client::_reset()
{
    _request.reset();
    _response.reset();
    _bytes_sent = 0;
    _file_offset = 0;
}

Client::~Client()
{
    if (_cgi)
    {
        _epoll.del_fd(_cgi->getReadEnd());
        delete _cgi; _cgi = NULL;
    }
}
```

* `_getConfig` does linear search through `m_configs[i]->names` for exact Host match. If `host` includes port (`example.com:8080`) but `names` is just `example.com`, this fails and default is used. No `Host` header stripping port? Actually `_request.getHeader("host")` may include port if client sent `Host: a:8080`. The `ServerConfig::names` likely stores without port. So mismatch. But default fallback hides bug.
* `_reset` for keep-alive: clear request/response, reset offsets.
* Destructor: if CGI still alive, remove pipe from epoll and `delete _cgi` which kills child and closes fds. Then base `~AFd` closes `m_fd`.

---

## 8. Chapter 6: `Multiplexer` — The Orchestrator (`Multiplexer.hpp` / `Multiplexer.cpp`)

### 8.1 High-Level

`Multiplexer` is the **owner**. It is instantiated in `main()` with `vector<ServerConfig>`. It deduplicates configs into `map<string, Server*> m_servers` keyed by `ip:port`. It owns `Epoll _epoll` and `list<Client*> _clientsList`. It runs `startup()` → `events_loop()` forever.

Design choice: `list` not `vector` for clients because we need stable iterators and O(1) `erase`/`push_back` for LRU ordering. `map` for servers because key is string and we need fast lookup for deduplication.

### 8.2 `include/network/Multiplexer.hpp` — Line By Line

```cpp
#ifndef MULTIPLEXER_HPP
# define MULTIPLEXER_HPP
# include <ServerConfig.hpp>
# include <Server.hpp>
# include <Client.hpp>
# include <Epoll.hpp>
# include <vector>
# include <map>
# include <list>
# include <memory>

//this Multiplexer will manage all servers from config file
class Multiplexer
{
public:
	std::map <std::string, Server*> m_servers;

  Multiplexer(const std::vector<ServerConfig> &v_configs);

	void startup();
	void events_loop();
	~Multiplexer();
private:
	Epoll _epoll;
	std::list<Client *> _clientsList;

	void _handle_timeout();
	void _deleteServers();
	void _deleteClientsList();
};

#endif
```

* `m_servers` is **public** — `main()` iterates it to log. Could be private with accessor, but public is simpler for 42.
* Constructor takes `const vector<ServerConfig>&` — non-owning reference to configs that outlive Multiplexer (they're in `main` stack). It will store `const ServerConfig*` pointers into that vector; so `v_configs` must not reallocate/move after construction (it doesn't).
* `startup()` adds servers to epoll, then calls `events_loop()`.
* `events_loop()` is blocking infinite.
* `_epoll` is private member before `_clientsList` (construction order matches declaration order: `_epoll` first, then `_clientsList`).
* `_clientsList` is LRU list: front = oldest activity, back = most recent. `_handle_timeout()` always checks front.
* `_deleteServers` / `_deleteClientsList` are cleanup helpers called in destructor and on construction failure.

### 8.3 `src/network/Multiplexer.cpp` — Line By Line

#### Constructor — Deduplication Logic

```cpp
#include <Multiplexer.hpp>
#include <timeout.hpp>
#include <Logger.hpp>
#include <Epoll.hpp>
#include <sys/epoll.h>
#include <string.h>
#include <stdexcept>
#include <iostream>
#include <map>

Multiplexer::Multiplexer(const std::vector<ServerConfig> &v_configs)
{
    std::string key;
    for (size_t confs = 0; confs < v_configs.size(); confs++)
    {
        for (size_t hosts = 0; hosts < v_configs[confs].hosts.size(); hosts++)
        {
            for (size_t ports = 0; ports < v_configs[confs].listen.size(); ports++)
            {
                key = Server::craft_key(v_configs[confs].hosts[hosts], v_configs[confs].listen[ports]);

                ServersMap::iterator it = m_servers.find(key);
                if (it == m_servers.end())
                {
                    try
                    {
                        this->m_servers[key] =
                            new Server(key, v_configs[confs].hosts[hosts], v_configs[confs].listen[ports],
                                       &v_configs[confs], _epoll, _clientsList);
                        this->m_servers.at(key)->run();
                    }
                    catch (...)
                    {
                        _deleteServers();
                        throw;
                    }
                }
                else
                    it->second->m_configs.push_back(&v_configs[confs]);
            }
        }
    }
}
```

* Triple nested loop: for each `ServerConfig` (one `server {}` block), for each `hosts` (e.g., `host 0.0.0.0; host 127.0.0.1;`), for each `listen` port (e.g., `listen 8080; listen 8081;`). Cartesian product: if a config has 2 hosts × 2 ports = 4 Server keys. This correctly handles `server { listen 80; listen 443; host a; host b; }` producing 4 sockets.
* `craft_key` canonicalizes.
* `find(key)` — if not found, `new Server(...)` with `key`, host, port, pointer to config, `_epoll`, `_clientsList`. Then `run()` (socket/bind/listen). If any of those throw (e.g., bind fails because port already used), we catch `...`, call `_deleteServers()` to clean up already-created servers (avoid leak), then rethrow to `main`.
* If found, `it->second->m_configs.push_back(&v_configs[confs])` — Add this config as another virtual host on existing Server. So `m_servers` size is number of unique `ip:port` combos, not number of `server {}` blocks.

**Boring detail:** `v_configs[confs]` is a `ServerConfig` object in vector. Taking `&v_configs[confs]` is safe only as long as vector doesn't reallocate. `Multiplexer` constructor doesn't modify `v_configs`, and `main` doesn't modify it after, so pointer stays valid.

#### `startup()`

```cpp
void Multiplexer::startup()
{
    if (m_servers.empty())
        throw std::runtime_error("Error: This Multiplexer's servers list is empty");
    ServersMap::iterator it;
    for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
    {
        if (_epoll.add_fd(it->second->get_fd(), it->second, EPOLLIN))
            throw std::runtime_error("failed to add the server " + it->second->m_key +
                                     " in the epoll instance");
        LOG_INFO << "added " << it->second->m_key << " as " << it->second->get_fd();
    }
    events_loop();
}
```

* Guard empty.
* For each server, `add_fd(Server.fd, Server*, EPOLLIN)`. If fails, throw (e.g., `EPOLL_CTL_ADD` fails if fd already in epoll — shouldn't happen).
* Then enter infinite loop `events_loop()` — never returns except on throw (none).

#### `events_loop()` — The Infinite While

```cpp
void Multiplexer::events_loop()
{
    int readyFds;
    AFd *fdObj;

    while (true)
    {
        epoll_event events[MAXEVENTS];
        readyFds = _epoll.wait(events);
        if (readyFds < 0)
            continue;

        for (int i = 0; i < readyFds; i++)
        {
            fdObj = static_cast<AFd *>(events[i].data.ptr);
            if (fdObj->handle_event(events[i].events) != Epoll::ECONTINUE)
            {
                if (fdObj->get_type() == AFd::CLIENT)
                {
                    Client *client = static_cast<Client *>(fdObj);
                    _clientsList.erase(client->m_it);
                }
                _epoll.del_fd(fdObj->get_fd());
                delete fdObj;
            }
            else if (fdObj->get_type() == AFd::CLIENT)
            {
                Client *client = static_cast<Client *>(fdObj);
                client->m_lastActivity = time(NULL);
                _clientsList.erase(client->m_it);
                _clientsList.push_back(client);
                client->m_it = --_clientsList.end();
            }
        }
        _handle_timeout();
    }
}
```

* `events[MAXEVENTS]` on stack.
* `readyFds = wait(...)` — blocks 100ms, returns -1 on `EINTR`, 0 on timeout, >0 ready.
* If `-1`, `continue` (retry).
* For each ready fd:
  * `fdObj = static_cast<AFd*>(events[i].data.ptr)` —Retrieve the Server or Client object (or Client via CGI pipe, but same pointer type). `data.ptr` was set in `add_fd`.
  * `fdObj->handle_event(events[i].events)` — Polymorphic dispatch.
  * If `!= ECONTINUE` (i.e., `EFINISHED` or `EERROR`), we close: if CLIENT, `erase` from list using its self-iterator, `del_fd`, `delete fdObj` (which closes fd via `~AFd` and cleans CGI).
  * Else if `CLIENT` and `ECONTINUE`, we update LRU: `m_lastActivity = time(NULL)`, move to back of list (`erase` then `push_back`). This makes `_clientsList.front()` always the **least recently active** client, so `_handle_timeout()` can cheaply check only front without scanning all.
  * For `SERVER`, `ECONTINUE` does nothing except the dispatch already accepted a client (if any). No LRU for servers.
* After processing all ready fds, call `_handle_timeout()`.

**Boring invariant:** `_clientsList` order is maintained **only** here. `Server::handle_event()` also `push_back` new clients, but doesn't update `_handle_timeout` ordering — that's done here on next loop if client does `ECONTINUE`. There is a subtle race: if a new client is added and immediately times out before its first `handle_event` updates `m_lastActivity`, its `m_lastActivity` is ctor time, which is recent, so not timed out. Fine.

#### `_handle_timeout()` — The LRU Killer

```cpp
void Multiplexer::_handle_timeout()
{
    time_t now = time(NULL);
    time_t timeout;
    Client *client;
    while (true)
    {
        if (_clientsList.empty())
            return;
        client = _clientsList.front();
        if (client->m_state == Client::CKEEPT_ALIVE)
            timeout = KEEPTALIVE_TIMEOUT;
        else
            timeout = MAIN_TIMEOUT;
        if (now - client->m_lastActivity > timeout)
        {
            if (client->m_state == Client::CRECEVING ||
                client->m_state == Client::CEXECUTING_CGI)
            {
                client->handleTimeout();
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
        else
        {
            break;
        }
    }
}
```

* `now = time(NULL)` once per invocation (not per client) — consistent.
* While list not empty, peek `front()` (oldest).
* Pick timeout based on state: 17 vs 16.
* If `now - lastActivity > timeout`, it's expired.
  * If `CRECEVING` or `CEXECUTING_CGI`, we **don't close**; we call `handleTimeout()` which builds 408 or kills CGI and switches to sending error response. Then we **move it to back** with new `m_lastActivity` so it won't be checked again immediately; it will now be `CSENDING_HEADERS` and will be checked next loop but with fresh timestamp, giving it time to send error response.
  * Else (e.g., `CSENDING_HEADERS`, `CSENDING_BODY`, `CKEEPT_ALIVE`, `CFINISHED`), we **kill**: `pop_front`, `del_fd`, `delete`.
* Else `break` — because list is ordered by activity, if front is not expired, no later element is expired either. This is why LRU ordering is crucial: we don't need to scan all.

**Note:** `CEXECUTING_CGI` also appears here, but `Client::handle_event` also checks CGI timeout via `time(NULL)-_cgi_start > CGI_TIMEOUT`. So there are **two CGI timeout paths**: one via `events_loop` LRU check, one via direct check at top of `Client::handle_event` when pipe event arrives. Both call `_cgiTimeout()`.

#### `_deleteServers`, `_deleteClientsList`, Destructor

```cpp
void Multiplexer::_deleteServers()
{
    ServersMap::iterator it;
    for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
    {
        if (it->second != NULL)
        {
            _epoll.del_fd(it->second->get_fd());
            delete it->second;
        }
    }
}

void Multiplexer::_deleteClientsList()
{
    Client *client;

    while (!_clientsList.empty())
    {
        client = _clientsList.front();
        _epoll.del_fd(client->get_fd());
        delete client;
        _clientsList.pop_front();
    }
}

Multiplexer::~Multiplexer()
{
    _deleteClientsList();
    _deleteServers();
}
```

* Order: delete clients first, then servers. Destructor tears down everything on program exit or exception.
* `_deleteServers` iterates map, `del_fd` then `delete` (which closes). No `m_servers.clear()` needed but okay.

---

## 9. Chapter 7: `Cgi` — Fork-Exec-Pipe CGI/1.1 (`Cgi.hpp` / `Cgi.cpp`)

### 9.1 High-Level CGI

CGI/1.1 is: web server forks, execs a script (e.g., `php-cgi`, `python3`), passes HTTP request via **environment variables** and **stdin** (for POST body), captures **stdout** as HTTP response.

This implementation:

* Environment: `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `SERVER_SOFTWARE`, `SERVER_NAME`, `SERVER_PORT`, `REQUEST_METHOD`, `REQUEST_URI`, `SCRIPT_NAME`, `SCRIPT_FILENAME`, `QUERY_STRING`, `CONTENT_LENGTH`, `CONTENT_TYPE`, plus `HTTP_*` headers.
* Argv: `[interpreter, script_path, NULL]` — e.g., `["/usr/bin/php-cgi", "/var/www/cgi-bin/foo.php", NULL]`.
* Stdin: `body_fd` (temp file containing request body) dup2'd to `STDIN_FILENO` (or `/dev/null` if no body).
* Stdout: `mkstemp("/tmp/_cgi_XXXXXX")` → child `dup2(output_fd, STDOUT_FILENO)`. Parent reads output via `pread` after child exits, not via pipe streaming. Pipe is only for **synchronization** (notify parent when child exits).
* Pipe `_notify[2]`: parent holds read end `_notify[0]` in epoll, child closes both? Actually child closes read end `_notify[0]` (close) but keeps write end? Wait code: child does `close(_notify[0])` only, leaving `_notify[1]` open until child exits (when kernel closes it). Parent does `close(_notify[1])` after fork. So parent's `read(_notify[0])` returns 0 when child exits (EOF).
* Output file `_output_path` is temp file that persists after child exits; `Client::_buildCgiResponse()` later `pread`s it to parse headers/body.

### 9.2 `include/cgi/Cgi.hpp` — Line By Line

```cpp
#ifndef CGI_HPP
#define CGI_HPP

#include <HttpRequest.hpp>
#include <vector>
#include <string>
#include <ctime>

#define PIPE_BUFFER_SIZE 65536 // 64 Kb

class Cgi
{
public:
  enum e_out
  {
    MORE,
    DONE,
    FAIL
  };

  Cgi(HttpRequest &request, const std::string &interpreter,
      const std::string &script_path, int body_fd);
  ~Cgi();

  int execute();
  e_out readOutput();
  void killChild();

  int getReadEnd() const;
  int getOutputFd() const;
  const std::string &getOutputPath() const;
  pid_t getPid() const;
  bool exitedCleanly() const;

private:
  HttpRequest &_request;
  std::string _interpreter;
  std::string _script_path;
  int _body_fd;

  int _notify[2];
  int _output_fd;
  std::string _output_path;
  int _status;
  pid_t _pid;
  bool _reaped;
  time_t _start;

  std::vector<std::string> _env;
  std::vector<const char *> _cenv;
  std::vector<const char *> _cargv;

  void _setArgv();
  void _setEnv();
};

#endif
```

* `PIPE_BUFFER_SIZE 65536` unused (maybe for future `read` buffer).
* `e_out MORE/DONE/FAIL` — `readOutput()` returns `MORE` if `read(pipe)>0` (child still writing status?), `DONE` if `read==0` (EOF + waitpid success), `FAIL` if `read==-1`.
* `HttpRequest& _request` — reference to Client's request, used for env construction (method, uri, headers). Not owned, not copied — `Client` outlives `Cgi`, so dangling not an issue; but if you moved `Cgi` after `Client` died, it'd dangle. Current design ensures `Cgi` dies before `Client`.
* `_interpreter` e.g., `"/usr/bin/python3"`, `_script_path` e.g., `"/var/www/foo.py"`.
* `_body_fd` — may be -1 if no body.
* `_notify[2]` — pipe fds, init to -1.
* `_output_fd` — `mkstemp` fd, -1 initially.
* `_output_path` — temp path.
* `_status` — `waitpid` status.
* `_pid` — child pid, -1 initially.
* `_reaped` — whether `waitpid` succeeded.
* `_start` — fork time (though Client also tracks `_cgi_start`; duplicate).
* `_env` — vector<string> owning env strings; `_cenv` — vector<const char*> C-style pointers into `_env` strings for `execve`; `_cargv` — argv C array.

### 9.3 `src/cgi/Cgi.cpp` — Line By Line

#### Includes & Constructor

```cpp
#include <Logger.hpp>
#include <Cgi.hpp>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cerrno>
#include <string>
#include <map>

Cgi::Cgi(HttpRequest &request, const std::string &interpreter,
      const std::string &script_path, int body_fd)
  : _request(request), _interpreter(interpreter),
    _script_path(script_path), _body_fd(body_fd),
    _output_fd(-1), _status(0),
    _pid(-1), _reaped(false), _start(0)
{
  _notify[0] = -1;
  _notify[1] = -1;
  _setArgv();
  _setEnv();
}
```

* `_output_fd=-1`, `_pid=-1` sentinel invalid.
* `_setArgv()` and `_setEnv()` called eagerly, before fork, so child doesn't need to allocate.

#### `_setArgv`

```cpp
void Cgi::_setArgv() {
  _cargv.push_back(_interpreter.c_str());
  _cargv.push_back(_script_path.c_str());
  _cargv.push_back(NULL);
}
```

* Stores pointers to `std::string::c_str()` inside `_interpreter` and `_script_path` which remain valid as long as `Cgi` lives (they are members). `NULL` terminates for `execve`.

#### `_setEnv` — The CGI Spec

```cpp
void Cgi::_setEnv() {
  _env.push_back("GATEWAY_INTERFACE=CGI/1.1");
  _env.push_back("SERVER_PROTOCOL=" + _request.getVersion());
  _env.push_back("SERVER_SOFTWARE=webserv/1.0");

  std::string host = _request.getHeader("host");
  std::string name;
  std::string port;
  size_t colon = host.find(':');
  if (colon != std::string::npos)
  {
    name = host.substr(0, colon);
    port = host.substr(colon + 1);
  }
  _env.push_back("SERVER_NAME=" + name);
  _env.push_back("SERVER_PORT=" + port);

  _env.push_back("REQUEST_METHOD=" + _request.getMethodStr());
  _env.push_back("REQUEST_URI=" + _request.getUri().getOriginal());
  _env.push_back("SCRIPT_NAME=" + _request.getUri().getPath());
  _env.push_back("SCRIPT_FILENAME=" + _script_path);
  _env.push_back("QUERY_STRING=" + _request.getUri().getQuery());

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
  std::string content_type = _request.getHeader("content-type");
  if (content_type != "")
    _env.push_back("CONTENT_TYPE=" + content_type);

  std::map<std::string, std::string>::const_iterator it;
  for (it = _request.getHeaders().begin(); it != _request.getHeaders().end(); ++it)
  {
    const std::string &nm = it->first;
    const std::string &vl = it->second;

    if (nm == "content-length" || nm == "content-type")
      continue;

    std::string env = "HTTP_";
    for (size_t i = 0; i < nm.size(); ++i)
    {
      env += (nm[i] == '-') ? '_' : static_cast<char>(::toupper(nm[i]));
    }
    _env.push_back(env + "=" + vl);
  }

  for (size_t i = 0; i < _env.size(); ++i)
    _cenv.push_back(_env[i].c_str());
  _cenv.push_back(NULL);
}
```

* `GATEWAY_INTERFACE` always `CGI/1.1`.
* `SERVER_PROTOCOL` from request version (e.g., `HTTP/1.1`).
* `SERVER_SOFTWARE` hardcoded `webserv/1.0`.
* `SERVER_NAME/PORT` parsed from `Host` header by splitting at `:`. If no colon, port is empty string (but spec expects numeric port; here they send empty if `Host: example.com` without port).
* `REQUEST_METHOD` (GET/POST/DELETE).
* `REQUEST_URI` original (path+query+fragment? Actually `Uri::getOriginal()` includes original string).
* `SCRIPT_NAME` path part of URI, `SCRIPT_FILENAME` physical script path, `QUERY_STRING` query part.
* `CONTENT_LENGTH` via `fstat(body_fd)` if body exists. Note they `ostringstream` to string.
* `CONTENT_TYPE` if present.
* Then for each header except content-length/type, create `HTTP_<NAME>` where `-` → `_` and uppercased: e.g., `user-agent` → `HTTP_USER_AGENT`. This is CGI spec: all request headers become `HTTP_*` env vars. The spec says to also include `CONTENT_LENGTH/TYPE` without `HTTP_` prefix — already done.
* Finally build `_cenv` C array: pointers to each string's `c_str()` plus NULL.

**Boring note:** `::toupper` with `unsigned char` cast? They do `::toupper(nm[i])` without cast; okay because `nm` is lowercased? Headers are lowercased by `HttpRequest`, so `nm[i]` is `a-z` or `-`. `toupper` on char may have UB if signed negative, but here not an issue. Still, correct code would cast.

#### `execute()` — The Fork

```cpp
int Cgi::execute() {
  if (pipe(_notify) == -1)
  {
    LOG_ERROR << "pipe() -> " << strerror(errno);
    return -1;
  }
  if (fcntl(_notify[0], F_SETFL, O_NONBLOCK) == -1)
  {
    LOG_ERROR << "fcntl() -> " << strerror(errno);
    (void)close(_notify[0]); _notify[0] = -1;
    (void)close(_notify[1]); _notify[1] = -1;
    return -1;
  }

  char tmpl[] = "/tmp/_cgi_XXXXXX";
  _output_fd = mkstemp(tmpl);
  if (_output_fd == -1)
  {
    LOG_ERROR << "mkstemp() -> " << strerror(errno);
    (void)close(_notify[0]); _notify[0] = -1;
    (void)close(_notify[1]); _notify[1] = -1;
    return -1;
  }
  _output_path = tmpl;

  _pid = fork();
  if (_pid == -1)
  {
    LOG_ERROR << "fork() -> " << strerror(errno);
    (void)close(_notify[0]); _notify[0] = -1;
    (void)close(_notify[1]); _notify[1] = -1;
    (void)close(_output_fd); _output_fd = -1;
    unlink(_output_path.c_str()); _output_path.clear();
    return -1;
  }

  if (_pid == 0)
  {
    (void)close(_notify[0]);
    (void)dup2(_output_fd, STDOUT_FILENO);
    (void)close(_output_fd);

    int input = (_body_fd != -1) ? _body_fd : open("/dev/null", O_RDONLY);
    if (input != -1)
    {
      (void)lseek(input, 0, SEEK_SET);
      (void)dup2(input, STDIN_FILENO);
      (void)close(input);
    }

    // I must remove this if I want to see the script errors
    // int blackhole = open("/dev/null", O_WRONLY);
    // if (blackhole != -1)
    // {
    //   (void)dup2(blackhole, STDERR_FILENO);
    //   (void)close(blackhole);
    // }

    std::string dir;
    size_t slash = _script_path.find_last_of('/');
    if (slash == std::string::npos)
      dir = ".";
    else if (slash == 0)
      dir = "/";
    else
      dir = _script_path.substr(0, slash);
    (void)chdir(dir.c_str());

    execve(_cargv[0], (char *const *)&_cargv[0], (char *const *)&_cenv[0]);
    LOG_ERROR << "execve(" << _interpreter << ") -> " << strerror(errno);
    _exit(127);
  }

  (void)close(_notify[1]); _notify[1] = -1;

  if (_body_fd != -1)
  {
    (void)close(_body_fd);
    _body_fd = -1;
  }
  _start = time(NULL);
  return 0;
}
```

* `pipe(_notify)` — Creates pipe; `_notify[0]` read, `[1]` write. Returns -1 on `EMFILE`.
* `fcntl(_notify[0], O_NONBLOCK)` — Make read end non-blocking so `read()` in `readOutput()` never blocks the event loop.
* `mkstemp(tmpl)` — Creates temp file with `0600` and replaces `XXXXXX` with random. Returns fd and modifies `tmpl` to actual path. `tmpl` must be char array not `const char*` because `mkstemp` modifies it.
* `fork()` — Duplicates process. Returns -1 error, 0 child, >0 parent pid.
* Child block (`_pid == 0`):
  * `close(_notify[0])` — Close read end (child will not read pipe).
  * `dup2(_output_fd, STDOUT_FILENO)` — Redirect stdout to temp file. So `printf`/`cout` in script goes to file.
  * `close(_output_fd)` — Original fd no longer needed after dup2.
  * `input = (body_fd != -1) ? body_fd : open("/dev/null")` — If request had body, use its fd; else use empty `/dev/null`. Note `body_fd` is the temp file holding request body (from `HttpRequest`), its offset may be at end; we `lseek(input,0,SEEK_SET)` to rewind, then `dup2(input, STDIN)`. So CGI script's stdin is request body.
  * `close(input)` after dup2.
  * Commented blackhole for stderr — if enabled, would redirect stderr to `/dev/null` so script errors are hidden. Currently disabled (stderr goes to server's stderr? Actually child's stderr is still inherited from parent, which is terminal/webserv log — you will see script tracebacks; comment says remove to see errors).
  * `chdir(dir)` — Change to script's directory, so relative file operations work. `dir` is extracted from `_script_path` last slash.
  * `execve(...)` — Replace child image with interpreter. If fails, log and `_exit(127)` (127 is conventional "command not found"). Note `_exit` not `exit` (avoid flushing parent buffers).
* Parent block:
  * `close(_notify[1])` — Close write end; parent only reads.
  * `close(_body_fd)` — Parent no longer needs body file; child has dup'd it.
  * `_start = time()` — For timeout (duplicate of Client's `_cgi_start`).

#### `readOutput`, `killChild`, `exitedCleanly`

```cpp
Cgi::e_out Cgi::readOutput() {
  char junk[64];
  ssize_t bytes = read(_notify[0], junk, sizeof(junk));
  if (bytes > 0)
  {
    return MORE;
  }
  if (bytes == 0)
  {
    int res;
    do { res = waitpid(_pid, &_status, WNOHANG); }
    while (res == -1 && errno == EINTR);
    if (res == 0)
    {
      (void)kill(_pid, SIGKILL);
      while(waitpid(_pid, &_status, 0) == -1 && errno == EINTR) {}
    }
    _reaped = true;
    return DONE;
  }
  return FAIL;
}

void Cgi::killChild() {
  if (_pid > 0 && !_reaped)
  {
    (void)kill(_pid, SIGKILL);
    while(waitpid(_pid, &_status, 0) == -1 && errno == EINTR) {}
  }
}

bool Cgi::exitedCleanly() const {
  return 
  (
    _reaped &&
    WIFEXITED(_status) &&
    WEXITSTATUS(_status) == EXIT_SUCCESS
  );
}

int Cgi::getReadEnd() const {
  return _notify[0];
}

int Cgi::getOutputFd() const {
  return _output_fd;
}

const std::string& Cgi::getOutputPath() const {
  return _output_path;
}

pid_t Cgi::getPid() const {
  return _pid;
}

Cgi::~Cgi()
{
  killChild();
  if (_notify[0] != -1) (void)close(_notify[0]);
  if (_notify[1] != -1) (void)close(_notify[1]);
  if (_output_fd != -1) (void)close(_output_fd);
  // if (!_output_path.empty()) (void)unlink(_output_path.c_str());
}
```

* `readOutput` — Called when epoll says pipe readable.
  * `read(_notify[0], junk,64)` — Non-blocking read. If `bytes>0`, there is data — but we discard `junk`. Why? The pipe is only for notification, not for CGI output (output goes to file). Child never writes to pipe! Wait, child closes `_notify[0]` but keeps `_notify[1]` open. Child never writes; parent closed `_notify[1]`. So when does `read` return >0? Actually child inherits `_notify[1]` and keeps it open until exit; it never writes, so `read` should never return >0 under normal flow. But if child `execve` fails and `_exit(127)`, it still closes `_notify[1]` on exit. So `bytes>0` case is **unreachable** in current code? Possibly they intended child to write something but removed. So `MORE` is dead code but kept for future. Real path is `bytes==0` (EOF when child exits and write end closed).
  * `bytes==0` → EOF → child exited. Do `waitpid(_pid, &_status, WNOHANG)` — non-blocking; if `res==0` (still running, maybe pipe EOF but child not yet reaped? Shouldn't happen because pipe EOF means write end closed, which happens on child's exit, but `waitpid` might still be 0 if child is zombie not yet? Actually after child exits, kernel closes its fds including `_notify[1]`, so parent's `read` gets 0 and child is zombie; `waitpid(WNOHANG)` should return pid. But they handle `res==0` (no state change) by `kill(SIGKILL)` and blocking `waitpid` to reap. Defensive.
  * `res==-1 && EINTR` loop handles signal interruption.
  * Set `_reaped=true`, return `DONE`.
  * Else `return FAIL` if `read` was -1 (`EAGAIN` would be -1 with `EAGAIN`, but they treat as FAIL; if pipe is non-blocking and no data, `read` returns -1 `EAGAIN`, which would be FAIL, leading `_handleCgiEvent` to 502. That is arguably a bug, but with current child-never-writes design, `read` will be 0 or -1 EAGAIN? Actually after child fork, pipe has no writer except child; child does not write, so parent's first `read` after `EPOLLIN` would be? `EPOLLIN` on pipe means data available or writer closed. If writer closed, `read` returns 0. If writer still open but no data, shouldn't be `EPOLLIN`. So `EAGAIN` shouldn't happen when epoll says readable. Okay.
* `killChild` — `SIGKILL` child if not reaped, then blocking `waitpid`.
* `exitedCleanly` — Checks `WIFEXITED` (normal exit, not signal) and `WEXITSTATUS==0`.
* Destructor — `killChild` + close fds. Note commented `unlink` — temp file is **not deleted** in destructor; it leaks to `/tmp` for debugging? `Client::_buildCgiResponse` reopens it via `setFileBody`, and after response sent, file remains. Could accumulate.

---

## 10. End-To-End Event Flow — One Request From `accept()` To `sendfile()`

1. `Multiplexer::events_loop()` blocks in `epoll_wait(100ms)`. `Server` fd is `EPOLLIN` because client `connect()` completed TCP handshake.
2. `Server::handle_event()` → `accept4(SOCK_NONBLOCK|SOCK_CLOEXEC)` → `Client* c = new Client(fd, ...)`.
3. `epoll.add_fd(clientFd, c, EPOLLIN)` and `list.push_back(c)`.
4. Next `epoll_wait` returns `EPOLLIN` for client. `Client::handle_event()` → `_receiveData()` → `recv(8192)` → `_request.parse()` → if incomplete, return `ECONTINUE` (stay `EPOLLIN`).
5. Repeat `recv` until `HttpRequest::COMPLETE` (needs CRLF CRLF + body per Content-Length/chunked).
6. When complete, `RequestHandler::handle()` examines `ServerConfig` (matched via `_getConfig(host)`), checks `Location`, determines file path or CGI. If file, fills `HttpResponse` with file fd/size, headers.
7. `_epoll.edit_fd(clientFd, EPOLLOUT)` → `CSENDING_HEADERS`.
8. Next `EPOLLOUT`: `_sendData()` → `send(headers)` (maybe partial, re-arm `EPOLLOUT`).
9. When headers done, if `hasFile`, `CSENDING_BODY` → `sendfile(8192)` loop until `_file_offset == size`.
10. When `CFINISHED`:
    - If `Connection: keep-alive`, `edit_fd(EPOLLIN)`, `_reset()`, wait for next request on same connection (LRU updated).
    - Else `EFINISHED` → Multiplexer `del_fd` + `delete Client`.
11. If CGI: after `parse`, `startCgi()` → `pipe`+`mkstemp`+`fork`+`execve`. `del_fd(clientFd)`, `add_fd(pipeRead, Client*, EPOLLIN)`, `CEXECUTING_CGI`.
12. Next `EPOLLIN` on pipe → `_handleCgiEvent()` → `readOutput()` → `waitpid` → `_buildCgiResponse()` → `add_fd(clientFd, EPOLLOUT)` → send as before.

Timeouts interleave via `_handle_timeout()` every loop.

---

## 11. Failure Modes And Why Each `if` Exists

* `getaddrinfo` fail → throw, prevents server start with bad IP.
* `socket` fail → throw, usually `EMFILE` (out of fds).
* `setsockopt` fail → throw, rare.
* `bind` fail → throw, `EADDRINUSE` if port taken.
* `listen` fail → throw.
* `accept4` fail → log, `ECONTINUE` (don't kill server).
* `epoll.add_fd` fail → log, delete client.
* `recv` -1/0 → `EERROR` (close). `0` is peer closed, `-1` is `EAGAIN` or error.
* `send` -1/0 → `EERROR`.
* `sendfile` -1 → `EERROR`.
* `epoll.edit_fd` fail → `EERROR`.
* `pipe` fail → 500.
* `fcntl O_NONBLOCK` fail → 500.
* `mkstemp` fail → 500.
* `fork` fail → 500 (`EAGAIN` out of pids).
* `execve` fail → child `_exit(127)`, parent sees 502.
* `CGI headers >64KB` → 502.
* `pread` fail → 502.
* `fstat` fail → 502.
* `no header terminator` → 502.
* `malformed header` → 502.
* `CGI timeout 16s` → 504.
* `main timeout 16s` → 408.
* `client list empty` → `_handle_timeout` early return.

---

## 12. How You Would Re-Write This From Zero

1. **Write `timeout.hpp`** first: `#define` 16/17/16.
2. **Write `AFd.hpp`**: enum Type, `int m_fd`, `Type _type`, pure virtual `handle_event`, virtual destructor with `close` guard.
3. **Write `AFd.cpp`**: constructor initializer, destructor, getters.
4. **Write `Epoll.hpp`**: `MAXEVENTS 64`, `enum EventState`, `m_fd`, disable copy, declare `add/edit/del/wait`.
5. **Write `Epoll.cpp`**: `epoll_create1(EPOLL_CLOEXEC)`, `epoll_ctl` wrappers, `epoll_wait(...,100)`.
6. **Write `Server.hpp`**: public `m_configs`, `m_key`, `m_ip`, `m_addr`, `m_port`, ctor `key,ip,port,config,epoll,list`, `handle_event`, `run`, `craft_key`, private `create/bind/listen/accept`.
7. **Write `Server.cpp`**: constructor `getaddrinfo`+`htons`, `create_socket` via `socket`, `bind_address` via `setsockopt REUSEADDR`+`bind`, `start_listening` via `listen SOMAXCONN`, `accept_connection` via `accept4 CLOEXEC NONBLOCK`, `handle_event` via `accept`+`new Client`+`add_fd`, `craft_key` via `getaddrinfo`+`inet_ntop`+`stringstream`.
8. **Write `Client.hpp`**: `e_state` with 7 states, `APP_BUFFER_SIZE 8192`, public `m_lastActivity/m_state/m_configs/m_it`, ctor, `handle_event/handleTimeout/startCgi`, private `Epoll&`, `HttpRequest/Response`, offsets, `Cgi*`, helpers for CGI header parsing, `recv/send` etc.
9. **Write `Client.cpp`**: ctor init list, `_receiveData` with `recv`+`HttpRequest::parse`+`RequestHandler`+`startCgi`+`edit_fd EPOLLOUT`, `_sendData` with `send` headers then `sendfile` body then keep-alive check, `handle_event` dispatch with CGI timeout check, `startCgi` with `new Cgi`+`execute`+`add_fd pipe`, `_handleCgiEvent` with `readOutput`+`buildCgiResponse`, `handleTimeout`, `_buildError`, `_cgiTimeout`, `_buildCgiResponse` with `fstat`+`pread` loop+header terminator search+`_parseCgiHeaders`, `_parseCgiHeaders` line-by-line state, `_lower`, `_getConfig` vhost search, `_reset`, destructor with `del_fd pipe`+`delete Cgi`.
10. **Write `Multiplexer.hpp`**: `map<string,Server*> m_servers`, ctor `vector<ServerConfig>`, `startup`, `events_loop`, destructor, private `Epoll _epoll`, `list<Client*> _clientsList`, helpers.
11. **Write `Multiplexer.cpp`**: ctor triple loop dedup via `craft_key`, `new Server`+`run` with try/catch cleanup, `startup` with `add_fd` for each server then `events_loop`, `events_loop` with `wait`+`for each handle_event`+`LRU update`+`_handle_timeout`, `_handle_timeout` with front check and move/close logic, `_deleteServers/_deleteClientsList`/dtor.
12. **Write `Cgi.hpp`**: `PIPE_BUFFER_SIZE`, `enum e_out`, ctor `HttpRequest&+interpreter+script+body_fd`, `execute/readOutput/killChild`, getters, private `HttpRequest&`, strings, `int _notify[2]`, `int _output_fd`, `string _output_path`, pid/status/reaped/start, `vector<string> _env`, `vector<const char*> _cenv/_cargv`, `_setArgv/_setEnv`.
13. **Write `Cgi.cpp`**: ctor init + `_setArgv/_setEnv`, `_setArgv` push interpreter/script/NULL, `_setEnv` build CGI spec env strings then `cenv` C array, `execute` with `pipe`+`fcntl NONBLOCK`+`mkstemp`+`fork`+ child `dup2 stdout/body`+`chdir`+`execve`+ parent `close write`+`close body`, `readOutput` with `read`+`waitpid`+`kill` if still running, `killChild`, `exitedCleanly`, getters, destructor with `killChild`+`close fds`.

If you follow this order, you rebuild the exact system. Every line has a reason: non-blocking guarantees progress, CLOEXEC prevents leaks, LRU list makes timeout O(1), pipe+mkstemp isolates CGI, header parsing enforces CGI spec, keep-alive reuses TCP.

---

*End of boring guide. You now know every line in `network/` and `cgi/`.*
