#pragma once
#include <AFd.hpp>
#include <Server.hpp>
#include <Epoll.hpp>
#include <Request.hpp>

class Server;
class Request;

class Client : public AFd
{
public:
  Server *m_server;
  Epoll *m_epoll;
  Request m_requst;

  Client(int fd, Server *server, Epoll *epoll);

  virtual void handdle_event(uint32_t event);

  virtual ~Client();
};
