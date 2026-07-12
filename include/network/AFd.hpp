#pragma once
#include <ServerConfig.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>
#include <exception>
#include <sys/epoll.h>

class AFd
{
  public:
    AFd(int fd);

    int get_fd() const;

    virtual void handdle_event(uint32_t event) = 0;

    virtual ~AFd();

  protected:
    int m_fd;
};
