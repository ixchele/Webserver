#pragma once
#include <Client.hpp>
#include <ServerConfig.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>
#include <exception>

using std::string;
using std::vector;

// each server in config file will be inistiated from this class
//
//

#define BACKLOG 5
#define MAX_CLIENTS 1024

typedef char methods_t;

class AFd
{
  public:
    AFd(int &fd);

    int get_fd() const;

    virtual void handdle_event() = 0;

    ~AFd();

  protected:
    int m_fd;
};
