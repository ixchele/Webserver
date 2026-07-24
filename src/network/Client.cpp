#include <Response.hpp>
#include <Client.hpp>
#include <Epoll.hpp>
#include <unistd.h>
#include <iostream>

Client::~Client()
{
  close(m_fd);
}

Client::Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs)
    : AFd(fd), m_configs(configs), _epoll(epoll)
{

}

void Client::_receive_data() {
    char buffer[APP_BUFFER_SIZE + 1];

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == static_cast<size_t>(-1) || bytes == 0)
    {
        // TODO: we must do something about logs
        std::cerr << "warning: read() failed with " << bytes << " in " << std::endl; 
        end_connection();
        return ;
    }
    m_buffer.append(buffer, bytes);
    std::cout << m_buffer;
    std::cout << std::endl;
}

void Client::handdle_event(uint32_t event)
{
  // to do
  if (event & EPOLLERR || event & EPOLLHUP || event & EPOLLRDHUP)
  {
    end_connection();
    return ;
  }
  if (event & EPOLLIN)
  {
    _receive_data();
    // m_requst.parse("vector");
    end_connection();
    return ;
  }
  if (event & EPOLLOUT)
  {
    // m_response.response();

    end_connection();
    return ;
  }
  else
    end_connection();
}

void Client::end_connection() {
  _epoll.del_fd(m_fd);
  std::cerr << "Ended connection with " << m_fd << std::endl;
  delete this;
}

const ServerConfig *Client::get_config(const std::string &host) const {
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
    return m_configs[0];
}
