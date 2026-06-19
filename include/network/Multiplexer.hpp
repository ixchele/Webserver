#pragma once
#include <Server.hpp>
#include <vector>
#include <ServerConfig.hpp>

using std::vector;

//this Multiplexer will manage all servers from config file
class Multiplexer
{
public:
	vector <AFd *> v_fds;

  Multiplexer(const vector<ServerConfig*> &v_configs);

	void startup();
	// void response_loop();
	~Multiplexer();
private:
};
