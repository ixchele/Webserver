#include <RequestHandler.hpp>
#include <HttpRequest.hpp>
#include <Client.hpp>
#include <Logger.hpp>
#include <Epoll.hpp>
// #include <Response.hpp>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <Logger.hpp>

Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd, AFd::CLIENT), m_lastActivity(time(NULL)), m_state(CRECEVING),
      m_configs(configs), _epoll(epoll), _request(fd), _bytes_sent(0) //, _response(_request, fd)
{
    (void)_epoll;
}

Epoll::EventState Client::_receive_data()
{
    char buffer[APP_BUFFER_SIZE + 1];

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == static_cast<size_t>(-1) || bytes == 0)
    {
        LOG_WARN << "recv() returned " << bytes << " on client with fd " << m_fd;
        return Epoll::EERROR;
    }
    buffer[bytes] = '\0';

    if (_request.getState() == HttpRequest::REQUEST_LINE)
    {
        _request.parse(buffer);
    }
    if (_request.getState() == HttpRequest::HEADERS_COMPLETE)
    {
        _request.parse("");
    }
    if (_request.getState() == HttpRequest::BODY)
    {
        _request.parse("");
    }
    if (_request.getState() == HttpRequest::COMPLETE || _request.getState() == HttpRequest::ERROR)
    {
        const std::string host = _request.getHeader("host");
        ServerConfig conf = *_get_config(host);
        RequestHandler rqst_handler(_request, _response, conf);
        rqst_handler.handle();
        m_state = CSENDING_HEADERS;
        _epoll.edit_fd(m_fd, this, EPOLLOUT);
    }

    LOG_DEBUG << "The Request of client on fd " << m_fd << ":"
              << "\tMethod: " << _request.getMethod()
              << "\tPath: " << _request.getUri().getPath()
              << "\tVersion: " << _request.getVersion()
              << "\tConnection: " << _request.getHeader("Connection");

    return Epoll::ECONTINUE;
}

Epoll::EventState Client::_send_data()
{
    char buffer[APP_BUFFER_SIZE + 1];
    size_t body_bytes = 0;
    std::string headers = _response.getHeaderBuffer();
    size_t headers_bytes = send(m_fd, headers.c_str() + _bytes_sent, headers.size() - _bytes_sent, 0);
    if (headers_bytes == static_cast<size_t>(-1) || headers_bytes == 0)
    {
        LOG_WARN << "send() returned " << headers_bytes << " on client with fd " << m_fd;
        return Epoll::EERROR;
    }
    _bytes_sent += headers_bytes;
    if (_response.hasFile())
    {
        _response.getFileStream().read(buffer, APP_BUFFER_SIZE);
        body_bytes += send(m_fd, buffer, APP_BUFFER_SIZE, 0);
        buffer[body_bytes] = '\0';
        if (body_bytes == static_cast<size_t>(-1) || body_bytes == 0)
        {
            LOG_WARN << "send() returned " << body_bytes << " on client with fd " << m_fd;
            return Epoll::EERROR;
        }
        _bytes_sent += body_bytes;
    }
    LOG_DEBUG << "we sent " << _bytes_sent << " bytes to client with fd " << m_fd;
    if (_request.getHeader("Connection") == "keep-alive")
    {
        m_state = CKEEPT_ALIVE;
        _epoll.edit_fd(m_fd, this, EPOLLIN);
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

void Client::handle_timeout()
{
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

Client::~Client()
{
    //
}