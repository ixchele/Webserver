#pragma once
#include <Client.hpp>
#include <ServerConfig.hpp>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>
#include <exception>
#include <AFd.hpp>
#include <map>

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
  std::map<int, Client *> m_clients;
  const std::string m_ip;
  const int m_port;

  Server(const std::string &ip, const short &port, const ServerConfig *config);

  virtual void handdle_event(uint32_t event);

  void run();
  void end_connection(int fd);
  void add_config(const ServerConfig *config);
  const ServerConfig *get_config(const string &host);

  virtual ~Server();

private:
  vector<const ServerConfig *> m_configs;

  void creat_socket();
  void bind_address();
  void start_listening();
  int accept_connection();
};
