#pragma once
#include <string>
#include <sys/socket.h>
#include <vector>
#include <netinet/in.h>
#include "ServerConfig.hpp"

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
		Server(const ServerConfig& serverConfig);
		Server();

    int get_sockFd();
    
    void set_addr();

    void creat_socket();
    void bind_address();
    void start_listening();
    void run();
		//~Server();

  private:
    sockaddr_in addr;
    const ServerConfig m_serverConfig;
    int m_sockFd;
};
