#pragma once
# include <AFd.hpp>
# include <Server.hpp>

class Server;

class Client : public AFd
{
  public:
    Server *m_server;

    Client(const int &fd, Server *server);

    virtual void handdle_event(uint32_t event);

    virtual ~Client();

  private:
    int m_fd;
};
