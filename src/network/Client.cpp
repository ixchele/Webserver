#include <RequestHandler.hpp>
#include <HttpRequest.hpp>
#include <timeout.hpp>
#include <Client.hpp>
#include <Logger.hpp>
#include <Epoll.hpp>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
// #include <Response.hpp>

Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd, AFd::CLIENT), m_lastActivity(time(NULL)), m_state(CRECEVING),
      m_configs(configs), _epoll(epoll), _request(fd), _bytes_sent(0),
      _file_offset(0), _cgi(NULL), _cgi_start(0)
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
        // if (rqst_handler.isCgi())
        // {

        // }
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
    // const std::string &raw = _cgi->getOutput();

    // HttpStatus::Code code = HttpStatus::OK;
    // std::map<std::string, std::string> headers;
    // std::string body;
    // bool in_headers = true;
    
    // size_t i = 0;
    // while (i < raw.size())
    // {
    //     size_t nl = raw.find('\n', i);
    // }
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