#pragma once
#include <AFd.hpp>
#include <Server.hpp>
#include <Epoll.hpp>

class Server;

class Client : public AFd
{
public:
  Server *m_server;
  Epoll *m_epoll;

  Client(int fd, Server *server, Epoll *epoll);

  virtual void handdle_event(uint32_t event);

  virtual ~Client();
};
