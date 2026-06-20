#pragma once
# include <AFd.hpp>
# include <Server.hpp>

class Client : public AFd
{
  public:
    const Server *m_server;

    Client(const int &fd, const Server *server);

    virtual void handdle_event(uint32_t event = EPOLLIN);

    ~Client();

  private:
    int m_fd;
};
