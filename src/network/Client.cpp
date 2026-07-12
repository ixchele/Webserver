#include <Client.hpp>
#include <Epoll.hpp>
#include <iostream>
#include <unistd.h>
#include <Server.hpp>

Client::~Client()
{
  close(m_fd);
  if (m_requst != NULL)
    delete m_requst;
}

Client::Client(int fd, Server *server, Epoll *epoll)
    : AFd(fd), m_server(server), m_epoll(epoll)
{
  m_requst = new Request(this);
}

void Client::handdle_event(uint32_t event)
{
  // to do
  if (event == EPOLLIN)
  {
    if (this->m_requst->receive_data() <= 0)
      m_server->end_connection(m_fd);
  }
  else if (event == EPOLLOUT)
  {
    std::cout << this->m_requst->m_sbuffer << std::endl;
    write (m_fd, "All readed\n", 11);
    m_server->end_connection(m_fd);
  }
  else
    m_server->end_connection(m_fd);
}
