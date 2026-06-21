#pragma once
# include <AFd.hpp>
# include <Server.hpp>

class Server;

class Client : public AFd
{
  public:
    const Server *m_server;

    Client(const int &fd, const Server *server);

    virtual void handdle_event(uint32_t event);

    virtual ~Client();

  private:
    int m_fd;
};
