#pragma once
#include <ServerConfig.hpp>
#include <Server.hpp>
#include <Epoll.hpp>
#include <vector>
#include <map>

using std::vector;

//this Multiplexer will manage all servers from config file
class Multiplexer
{
public:
	std::map <std::string, Server *> m_servers;

  	Multiplexer(const vector<ServerConfig*> &v_configs);

	void startup();
	void events_loop();
	~Multiplexer();
private:
	Epoll *m_epoll;
};
