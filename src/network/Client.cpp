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
    : AFd(fd), m_requst(fd), m_configs(configs), _epoll(epoll)
{

}

void Client::_receive_data() {
    char buffer[APP_BUFFER_SIZE + 1];

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == -1 || bytes == 0)
    {
        // TODO: we must do something about logs
        std::cerr << "warning: read() failed with " << bytes << " in " << std::endl;
        return;
    }
    for (int i = 0; i < bytes; bytes++)
    {
      v_buffer.push_back(buffer[i]);
    }
}

void Client::handdle_event(uint32_t event)
{
  // to do
  if (event == EPOLLIN)
  {
    _receive_data();
    // m_requst.parse("vector");
    _end_connection();
  }
  else if (event == EPOLLOUT)
  {
    // m_response.response();

    _end_connection();
  }
  else
    _end_connection();
}

void Client::_end_connection() {
  _epoll.del_fd(m_fd);
  std::cerr << "Ended connection with " << m_fd << std::endl;
  delete this;
}
