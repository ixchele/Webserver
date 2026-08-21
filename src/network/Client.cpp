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
            RequestHandler::CgiMode mode = rqst_handler.getCgiMode();
            LOG_DEBUG << "CGI hook: mode=" << mode << " script=" << script << " interp=" << interp
                      << " body_fd=" << body_fd << " body_path=" << body_path << " upload_dst=" << upload_dst;
            
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

    LOG_DEBUG << "The Request of client on fd " << m_fd << ":"
              << "\tMethod: " << _request.getMethod()
              << "\tPath: " << _request.getUri().getPath()
              << "\tVersion: " << _request.getVersion()
              << "\tconnection: " << _request.getHeader("connection");

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
        LOG_DEBUG << "Client with fd " << m_fd << " will be keept alive";
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
    if (event & EPOLLERR || event & EPOLLHUP || event & EPOLLRDHUP)
    {
        return Epoll::EERROR;
    }
    if (m_state == CEXECUTING_CGI)
    {
        if (_cgi != NULL && time(NULL) - _cgi_start > CGI_TIMEOUT)
        {
            _cgiTimeout();
            return Epoll::ECONTINUE;
        }
        return _handleCgiEvent();
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
    LOG_DEBUG << "CGI -> status " << status << ", body " << body_size << " bytes";
    
    _response.setStatusCode(status);

    std::map<std::string, std::string>::const_iterator it;
    for (it = headers.begin(); it != headers.end(); ++it)
    {
        const std::string lname = _lower(it->first);
        if (lname == "status" || lname == "content-length")
            continue;
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
