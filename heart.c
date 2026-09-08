// === include/network/AFd.hpp ===
#ifndef AFD_HPP
# define AFD_HPP
# include <Epoll.hpp>
# include <string>

class AFd
{
  public:
    enum Type {SERVER, CLIENT};
    AFd(int fd, Type type);

    int get_fd() const;
    Type get_type() const;

    virtual Epoll::EventState handle_event(uint32_t event) = 0;

    virtual ~AFd();

  protected:
    int m_fd;
    Type _type;
};

#endif

// === src/network/AFd.cpp ===
#include <AFd.hpp>
#include <unistd.h>

AFd::AFd(int fd, Type type) : m_fd(fd), _type(type)
{
}

AFd::~AFd()
{
    if (m_fd > 2)
        close(m_fd);
}

int AFd::get_fd() const
{
    return this->m_fd;
}

AFd::Type AFd::get_type() const
{
    return this->_type;
}

// === include/network/timeout.hpp ===
#ifndef TIMEOUT_HPP
# define TIMEOUT_HPP

# define KEEPTALIVE_TIMEOUT 17
# define MAIN_TIMEOUT 16
# define CGI_TIMEOUT 16

#endif
// === include/network/Epoll.hpp ===
#ifndef EPOLL_HPP
# define EPOLL_HPP
# include <sys/epoll.h>

# define MAXEVENTS 64

class AFd;

class Epoll
{
public:
    enum EventState {
        ECONTINUE = 0,
        EFINISHED = 1,
        EERROR = 2
    };
    Epoll();
    ~Epoll();

	int add_fd(int fd, AFd *ptr, int events);
	int edit_fd(int fd, AFd *ptr, int events);
	void del_fd(int fd);
    int wait(epoll_event *events);

private:
    Epoll(const Epoll& copy);
    Epoll& operator=(const Epoll&);
    int m_fd;
};

#endif

// === src/network/Epoll.cpp ===
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

Epoll::~Epoll() {
	if (m_fd != -1)
		close(m_fd);
}

int Epoll::add_fd(int fd, AFd *ptr, int events) {
	epoll_event ev;
	int ret;

	ev.data.ptr = ptr;
	ev.events = events;
	ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &ev);

	return ret;
}

int Epoll::edit_fd(int fd, AFd *ptr, int events) {
	epoll_event ev;

	ev.data.ptr = ptr;
	ev.events = events;
	return epoll_ctl(m_fd, EPOLL_CTL_MOD, fd, &ev);
}

void Epoll::del_fd(int fd) {
	if (epoll_ctl(m_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
		LOG_WARN << "warning: epoll_ctl() failed to delete fd " << fd;
}

int	Epoll::wait(epoll_event *events) {
	int readyFds = epoll_wait(m_fd, events, MAXEVENTS, 100);
	return readyFds;
}

// === include/network/Server.hpp ===
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

class Server : public AFd
{
public:
  std::vector<const ServerConfig *> m_configs;
  std::string m_key;
  const std::string m_ip;
  sockaddr_in m_addr;
  const int m_port;

  Server(const std::string &key, const std::string &ip, int port, const ServerConfig *config, Epoll &epoll, std::list<Client *> &clientsList);

  virtual Epoll::EventState handle_event(uint32_t event);

  void run();
  // void end_connection(int fd);
  void add_config(const ServerConfig *config);

  static std::string craft_key(const std::string &ip, int port);

  virtual ~Server();

private:
  Epoll &_epoll;
	std::list<Client *> &_clientsList;

  void create_socket();
  void bind_address();
  void start_listening();
  int accept_connection();
};


typedef std::map<std::string, Server*> ServersMap;

#endif

// === src/network/Server.cpp ===
#include <Server.hpp>
#include <Logger.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iostream>
#include <sstream>
#include <cstdlib>

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

Server::~Server()
{
    //
}

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

void Server::start_listening()
{
    if (listen(this->m_fd, SOMAXCONN) != 0)
    {
        throw std::runtime_error("error: listen() for " + m_key + " failed");
    }
}

// return 0 on success -1 if failed
int Server::accept_connection()
{
    int clientFd;

    // TODO : catch client infos
    clientFd = accept4(this->m_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);

    return clientFd;
}

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

// === include/network/Client.hpp ===
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


class Client : public AFd
{
  public:
    enum e_state {CKEEPT_ALIVE, CRECEVING, CSENDING_HEADERS, CSENDING_BODY, CEXECUTING_CGI, CFINISHED, CTIMEDOUT};

    time_t m_lastActivity;
    e_state m_state;
    std::vector<const ServerConfig *> &m_configs;
    std::list<Client *>::iterator m_it;

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

// === src/network/Client.cpp ===
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

// === include/network/Multiplexer.hpp ===
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

// === src/network/Multiplexer.cpp ===
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

// === include/cgi/Cgi.hpp ===
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

// === src/cgi/Cgi.cpp ===
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

void Cgi::_setArgv() {
  _cargv.push_back(_interpreter.c_str());
  _cargv.push_back(_script_path.c_str());
  _cargv.push_back(NULL);
}

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
