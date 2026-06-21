#pragma once
#include <Client.hpp>
#include <ServerConfig.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>
#include <exception>
#include <AFd.hpp>

using std::string;
using std::vector;

// each server in config file will be inistiated from this class
//
//

#define BACKLOG 5
#define MAX_CLIENTS 1024

typedef char methods_t;

class Client;

class Server : public AFd
{
  public:
    sockaddr_in m_addr;
    const ServerConfig *m_config;
    Client *m_currentClient;

    Server(const std::string &ip, const short &port, const ServerConfig *config);

    virtual void handdle_event(uint32_t event);

    void run();
    void end_connection();

    virtual ~Server();

  private:
    const std::string m_ip;
    const int         m_port; 

    void creat_socket();
    void bind_address();
    void start_listening();
    void accept_connection();
};
