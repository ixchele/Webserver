#pragma once
#include "Server.hpp"
#include <vector>

using std::vector;

// this Webserver will manage all servers from config file
class WebServer
{
public:
	void run();
	void response_loop();
private:
	vector <Server> m_servers;
};
