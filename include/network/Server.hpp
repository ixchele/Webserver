#pragma once
#include <Client.hpp>
#include <ServerConfig.hpp>
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

class Server
{
  public:
    sockaddr_in m_addr;
    const ServerConfig *m_config;

    Server(const std::string &ip, const short &port, const ServerConfig *config);
    Server();

    int get_fd();

    void set_addr();

    int creat_socket();
    int bind_address();
    int start_listening();
    Client accept_connection();
    void run();

    ~Server();

  private:
    int m_fd;
};
