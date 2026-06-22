#pragma once
#include <Server.hpp>
#include <vector>
#include <ServerConfig.hpp>
#include <Epoll.hpp>

using std::vector;

//this Multiplexer will manage all servers from config file
class Multiplexer
{
public:
	vector <Server *> v_servers;

  	Multiplexer(const vector<ServerConfig*> &v_configs);

	void startup();
	void events_loop();
	~Multiplexer();
private:
	Epoll m_epoll;
};
