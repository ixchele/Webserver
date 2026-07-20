#ifndef SERVER_HPP
# define SERVER_HPP
#include <ServerConfig.hpp>
#include <Client.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <exception>
#include <string>
#include <vector>
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
  vector<const ServerConfig *> m_configs;
  std::map<int, Client> m_clients;
  std::string m_key;
  const std::string m_ip;
  sockaddr_in m_addr;
  const int m_port;
  Epoll *m_epoll;

  Server(const std::string &key, const std::string &ip, short port, const ServerConfig *config, Epoll *epoll);

  virtual void handdle_event(uint32_t event);

  void run();
  void end_connection(int fd);
  void add_config(const ServerConfig *config);
  const ServerConfig *get_config(const string &host) const;

  static string craft_key(const string &ip, int port);

  virtual ~Server();

private:
  void creat_socket();
  void bind_address();
  void start_listening();
  int accept_connection();
};

#endif
