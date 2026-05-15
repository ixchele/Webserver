#pragma once

#include <string>
#include <vector>

struct CommonConfig {
	std::string	root;
	std::string	index;
	bool       	autoindex;
	size_t     	client_max_body_size;

	CommonConfig() : autoindex(false), client_max_body_size(1048576) {}
	virtual ~CommonConfig() {}
};

struct LocationConfig : public CommonConfig {
	std::string	path;
	int        	methods; // NOTE : bitwise 1 | 2 | 4

	LocationConfig() : CommonConfig(), methods(0) {}
};

struct ServerConfig : public CommonConfig {
	int							port;
	std::string					host;
	std::vector<std::string>	server_names;
	std::vector<LocationConfig>	locations;

	ServerConfig() : CommonConfig(), port(8080) {}
};

class Server : private ServerConfig {
	Server();
	Server(const ServerConfig &servConf) : ServerConfig(servConf) {}

};
