#include <HttpRequest.hpp>
#include <Client.hpp>
#include <Logger.hpp>
#include <Epoll.hpp>
// #include <Response.hpp>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <Logger.hpp>

Client::~Client()
{
    close(m_fd);
}

Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd, AFd::CLIENT), m_lastActivity(time(NULL)), m_state(IDLE), 
    m_configs(configs), _epoll(epoll), _request(fd)//, _response(_request, fd)
{
  (void)_epoll;
}

Epoll::EventState Client::_receive_data()
{
    char buffer[APP_BUFFER_SIZE + 1];

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == static_cast<size_t>(-1) || bytes == 0)
    {
        // TODO: we must do something about logs
        LOG_WARN << "recv() failed with " << bytes << " on client with fd " << m_fd;
        return Epoll::EVENT_ERROR;
    }
    buffer[bytes] = '\0';

    _request.parse(buffer);

    if (_request.getState() == HttpRequest::HEADERS_COMPLETE)
    {
        // _check_request();
        _request.parse("");
    }
    if (_request.getState() == HttpRequest::BODY)
    {
        // _check_request();
        _request.parse("");
    }
    if (_request.getState() == HttpRequest::COMPLETE || _request.getState() == HttpRequest::ERROR)
    {
        // _epoll.edit_fd(m_fd, this, EPOLLOUT);
    }

    
    LOG_DEBUG << "The Request of client on fd " << m_fd << ":" \
    << "\nMethod: " << _request.getMethod() \
     << "\nPath: " << _request.getUri().getPath() \
     << "\nversion: " << _request.getVersion();

    return Epoll::EVENT_FINISHED;
}

// Epoll::EventState Client::_send_data() {
//    int bytes = send(m_fd, m_buffer.c_str(), m_buffer.size(), 0);
//    m_buffer.erase(0, bytes);
//    if (m_buffer.empty())
//    {}
// }

Epoll::EventState Client::handle_event(uint32_t event)
{
    // to do
    if (event & EPOLLERR || event & EPOLLHUP || event & EPOLLRDHUP)
    {
      return Epoll::EVENT_ERROR;
    }
    if (event & EPOLLIN)
    {
        m_state = RECEVING;
        _receive_data();
        // m_requst.parse("vector");
        return Epoll::EVENT_FINISHED;
    }
    if (event & EPOLLOUT)
    {
        // m_response.response();
        return Epoll::EVENT_FINISHED;
    }
    else
        return Epoll::EVENT_FINISHED;
}

void Client::handle_timeout() {
  m_state = TIMEDOUT;
  _epoll.edit_fd(m_fd, this, EPOLLOUT);
}

const ServerConfig *Client::_get_config(const std::string &host)
{
    for (size_t i = 0; i < m_configs.size(); i++)
    {
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
