#include "CommonConfig.hpp"
#include <LocationConfig.hpp>
#include <ConfigParser.hpp>

LocationConfig::LocationConfig(void) 
	: path(""), methods(GET), upload("") {
	// pass
}

void	LocationConfig::resetConf(void) {
	this->path = "";
	this->methods = GET;
	this->upload = "";
	CommonConfig::resetConf();
}
