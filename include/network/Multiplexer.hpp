#pragma once
#include <Server.hpp>
#include <vector>
#include <ServerConfig.hpp>

using std::vector;

//this Multiplexer will manage all servers from config file
class Multiplexer
{
public:
	vector <Server> v_servers;

  Multiplexer(const vector<ServerConfig*> &v_configs);

	void run_all_servers();
	// void response_loop();
private:
};
