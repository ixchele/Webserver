#include "ConfigParser.hpp"
#include <ServerConfig.hpp>
#include <LocationConfig.hpp>
#include <cstddef>
#include <sstream>
#include <vector>

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

static void	validateDirectives(const LocationConfig &location) {
	if (location.root.empty())
		throw ConfigParser::ConfigException("invalid location block " + location.path + " : root directive miss");
	if (!location.upload.empty() && !(location.methods & POST))
		throw ConfigParser::ConfigException("invalid location block " + location.path + " : upload path without POST method");
}

void    ServerConfig::applyInheritance() {
	if (this->listen.empty())
		throw ConfigParser::ConfigException("invalid server block : required at least one listen directive");

    for (std::vector<LocationConfig>::iterator location = locations.begin(); location != this->locations.end(); ++location) {
        if (location->root.empty() && !this->root.empty())
            location->root = this->root;

		if (location->index.empty() && !this->index.empty())
			location->index = this->index;

		if (location->return_val.empty() && !this->return_val.empty())
			location->return_val = this->return_val;

		if (location->autoindex == false && this->autoindex == true)
			location->autoindex = this->autoindex;

		if (location->client_max_body_size == -1 && this->client_max_body_size != -1)
			location->client_max_body_size = this->client_max_body_size;

		if (!this->error_page.empty())
			location->error_page.insert(this->error_page.begin(), this->error_page.end());

		if (!this->cgi_pass.empty())
			location->cgi_pass.insert(this->cgi_pass.begin(), this->cgi_pass.end());

		validateDirectives(*location);
	}

}

std::string ServerConfig::str(const std::string& indent) const {
	std::stringstream ss;

	ss << indent << "=== SERVER BLOCK ===\n";

	ss << indent << "listen: ";
	if (listen.empty()) {
		ss << "(empty)";
	} else {
		for (std::size_t i = 0; i < listen.size(); ++i) {
			ss << listen[i] << " ";
		}
	}
	ss << "\n";

	ss << indent << "host: " << (host.empty() ? "(empty)" : host) << "\n";
	ss << indent << "name: " << (name.empty() ? "(empty)" : name) << "\n";

	ss << CommonConfig::str(indent);

	if (locations.empty()) {
		ss << indent << "locations: (none)\n";
	} else {
		ss << indent << "locations:\n";
		for (std::size_t i = 0; i < locations.size(); ++i) {
			ss << locations[i].str(indent + "    ");
			ss << "\n";
		}
	}

	ss << indent << "====================\n";
	return ss.str();
}
