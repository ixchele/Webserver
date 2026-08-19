#include <RequestHandler.hpp>
#include <HttpRequest.hpp>
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
      _file_offset(0)
{
}

Epoll::EventState Client::_receive_data()
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
        const ServerConfig &conf = *_get_config(host);
        RequestHandler rqst_handler(_request, _response, conf);
        rqst_handler.handle();
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

Epoll::EventState Client::_send_data()
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
    if (event & EPOLLIN)
    {
        m_state = CRECEVING;
        return _receive_data();
    }
    if (event & EPOLLOUT)
    {
        return _send_data();
    }
    else
        return Epoll::EFINISHED;
}

int Client::startCgi(const std::string &interpreter, 
                    const std::string &script_path, int body_fd)
{
    _cgi = new Cgi(_request, interpreter, script_path, body_fd);
    _cgi_start = time(NULL);
_epoll
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
    if (_epoll.add_fd(_cgi->getReadEnd(), this, EPOLLIN) != 0)
    {
        delete _cgi;
        _cgi = NULL;
        _build_cgi_error(HttpStatus::InternalServerError);
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
        return -1;
    }
    _epoll.del_fd(m_fd);
    return 0;
}

void Client::handle_timeout()
{
    _reset();
    m_state = CTIMEDOUT;
    _request.setErrorCode(HttpStatus::RequestTimeout);
    _request.setState(HttpRequest::ERROR);
    RequestHandler rqst_handler(_request, _response, *m_configs[0]);
    rqst_handler.handle();
    _epoll.edit_fd(m_fd, this, EPOLLOUT);
}

const ServerConfig *Client::_get_config(const std::string &host)
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
    //
}