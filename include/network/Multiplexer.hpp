#ifndef MULTIPLEXER_HPP
# define MULTIPLEXER_HPP
# include <ServerConfig.hpp>
# include <Server.hpp>
# include <Client.hpp>
# include <Epoll.hpp>
# include <vector>
# include <map>
# include <list>

# define KEEPTALIVE_TIMEOUT 7
# define MAIN_TIMEOUT 60
# define CGI_TIMEOUT 6

//this Multiplexer will manage all servers from config file
class Multiplexer
{
public:
	std::map <std::string, Server*> m_servers;

  Multiplexer(const std::vector<ServerConfig> &v_configs);

	void startup();
	void events_loop();
	~Multiplexer();
private:
	Epoll _epoll;
	std::list<Client *> _clientsList;

	void _handle_timeout();
};

#endif
