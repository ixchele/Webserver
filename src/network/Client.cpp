#include <Client.hpp>
#include <Epoll.hpp>
#include <iostream>
#include <unistd.h>
#include <Server.hpp>

Client::~Client()
{
  close(m_fd);
}

Client::Client(int fd, Server *server, Epoll *epoll)
    : AFd(fd), m_server(server), m_epoll(epoll), m_requst(this)
{
}

void Client::handdle_event(uint32_t event)
{
  // to do
  if (event == EPOLLIN)
  {
    if (this->m_requst.receive_data() == 0)
    this->m_epoll->edit_fd(m_fd, this, EPOLLOUT);
  }
  m_server->end_connection(m_fd);
}
