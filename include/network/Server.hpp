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
#include <list>

class Server : public AFd
{
public:
  std::vector<const ServerConfig *> m_configs;
  std::string m_key;
  const std::string m_ip;
  sockaddr_in m_addr;
  const int m_port;

  Server(const std::string &key, const std::string &ip, short port, const ServerConfig *config, Epoll &epoll, std::list<Client *> &clientsList);

  virtual Epoll::EventState handle_event(uint32_t event);

  void run();
  // void end_connection(int fd);
  void add_config(const ServerConfig *config);

  static std::string craft_key(const std::string &ip, int port);

  virtual ~Server();

private:
  Epoll &_epoll;
	std::list<Client *> &_clientsList;

  void create_socket();
  void bind_address();
  void start_listening();
  int accept_connection();
};


typedef std::map<std::string, Server*> ServersMap;

#endif
