#include <Client.hpp>
#include <Epoll.hpp>
// #include <Response.hpp>
#include <Request.hpp>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>

Client::~Client()
{
    close(m_fd);
}

Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd), m_configs(configs), _epoll(epoll), _request(fd)//, _response(_request, fd)
{
  (void)_epoll;
}

int Client::_receive_data()
{
    char buffer[APP_BUFFER_SIZE + 1];

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == static_cast<size_t>(-1) || bytes == 0)
    {
        // TODO: we must do something about logs
        std::cerr << "warning: recv() failed with " << bytes << " in " << std::endl;
        return EVENT_ERROR;
    }
    buffer[bytes] = '\0';
    _request.parse(buffer);
    std::cout << "Method: " << _request.getMethod() << std::endl;
    std::cout << "Path: " << _request.getUri().getPath() << std::endl;
    std::cout << "version: " << _request.getVersion() << std::endl;
    return EVENT_FINISHED;
}

int Client::handdle_event(uint32_t event)
{
    // to do
    if (event & EPOLLERR || event & EPOLLHUP || event & EPOLLRDHUP)
    {
      return EVENT_ERROR;
    }
    if (event & EPOLLIN)
    {
        _receive_data();
        // m_requst.parse("vector");
        return EVENT_FINISHED;
    }
    if (event & EPOLLOUT)
    {
        // m_response.response();
        return EVENT_FINISHED;
    }
    else
        return EVENT_FINISHED;
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
    std::cerr << "warning: Client with fd " << m_fd << " will use the default config" << std::endl;
    return m_configs[0];
}
