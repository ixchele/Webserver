#include <ServerConfig.hpp>

ServerConfig::ServerConfig() : name("exemple.com") {
	//pass
}

ServerConfig::~ServerConfig(void) {
	// pass
}

void	ServerConfig::resetConf(void) {
	this->listen.clear();
	this->host.clear();
	this->name = "exemple.com";
	this->locations.clear();
	CommonConfig::resetConf();
}
