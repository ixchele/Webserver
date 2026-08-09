#include <ConfigParser.hpp>
#include <ServerConfig.hpp>
#include <LocationConfig.hpp>
#include <cstddef>
#include <sstream>
#include <vector>

ServerConfig::ServerConfig() {
	//pass
}

ServerConfig::~ServerConfig(void) {
	// pass
}

void	ServerConfig::resetConf(void) {
	this->listen.clear();
	this->hosts.clear();
	this->names.clear();
	this->locations.clear();
	CommonConfig::resetConf();
}

static void	validateDirectives(const LocationConfig &location) {
	if (location.root.empty())
		throw ConfigParser::ConfigException("invalid location block " + location.path + " : root directive miss");
	if (!location.upload.empty() && !(location.methods & HTTP_POST))
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

		if (location->client_max_body_size == 1024 && this->client_max_body_size != 1024)
			location->client_max_body_size = this->client_max_body_size;

		if (!this->error_page.empty())
			location->error_page.insert(this->error_page.begin(), this->error_page.end());

		if (!this->cgi_pass.empty())
			location->cgi_pass.insert(this->cgi_pass.begin(), this->cgi_pass.end());

		validateDirectives(*location);
	}

}

const CommonConfig    *ServerConfig::matchRoute(const std::string &uri) const {
	const LocationConfig	*best_match = NULL;
	size_t					longest_match_len = 0;

	for (size_t i = 0; i < locations.size(); ++i) {
		const std::string	&loc_path = locations[i].path;

		if (uri.find(loc_path) == 0) {
			if (loc_path.length() > longest_match_len) {
				longest_match_len = loc_path.length();
				best_match = &locations[i];
			}
		}
	}

	if (best_match != NULL)
		return best_match;

	return this;
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

	ss << indent << "hosts: " << '[';
	for (std::vector<std::string>::const_iterator it = hosts.begin(); it != hosts.end(); ++it)
		ss << *it << (it + 1 == hosts.end() ? "" : ", ");
	ss << "]" << std::endl;
	if (this->names.empty())
		ss << indent << "names: " << "(empty)" << "\n";
	else {
		ss << indent << "names: " << "[";
		for (std::vector<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
			ss << *it << (it + 1 == names.end() ? "" : ", ");
		ss << "]" << std::endl;
	}

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
