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
    : AFd(fd), m_server(server), m_epoll(epoll)
{
}

void Client::handdle_event(uint32_t event)
{
  // to do
  (void)event;
  write(m_fd, "Accepted\n", 9);
  m_server->end_connection(m_fd);
}
