#pragma once
#include "ServerConfig.hpp"
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>

using std::string;
using std::vector;

// each server in config file will be inistiated from this class
//
//

#define BACKLOG 5
#define MAX_CLIENTS 1024

typedef char methods_t;

class Socket
{
  public:
    Socket(const int &addr, const short &port, const ServerConfig *&config);
    Socket();

    int get_sockFd();

    void set_addr();

    int creat_socket();
    int bind_address();
    int start_listening();
    void run();
    ~Socket();

  private:
    int m_port;
    int m_sockFd;
    ServerConfig *m_config;
};
