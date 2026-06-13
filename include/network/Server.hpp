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
    Server(const short &port, const ServerConfig *config);
    Server();

    int get_fd();

    void set_addr();

    int creat_socket();
    int bind_address();
    int start_listening();
    Client accept_connection();
    void run();

    static void generate_servers(vector <Server> &v_servers, vector<ServerConfig*> &v_configs);

    ~Server();

  private:
    sockaddr_in m_addr;
    int m_fd;
    const ServerConfig *m_config;
};
