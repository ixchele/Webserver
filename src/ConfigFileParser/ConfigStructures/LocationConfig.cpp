#include "CommonConfig.hpp"
#include <LocationConfig.hpp>
#include <ConfigParser.hpp>
#include <sstream>

LocationConfig::LocationConfig(void) 
	: path(""), upload("") {
		// pass
}

void	LocationConfig::resetConf(void) {
	this->path = "";
	this->methods = HTTP_GET;
	this->upload = "";
	CommonConfig::resetConf();
}


std::string LocationConfig::toString(const std::string& indent) const {
	std::stringstream	ss;

	ss << indent << "--- Location [" << (path.empty() ? "UNDEFINED" : path) << "] ---\n";

	ss << indent << "methods: " << methods << " (";
	if (methods == 0) ss << "None/Default";
	else {
		if (methods & HTTP_GET) ss << "GET ";
		if (methods & HTTP_POST) ss << "POST ";
		if (methods & HTTP_DELETE) ss << "DELETE";
	}
	ss << ")\n";

	ss << indent << "upload: " << (upload.empty() ? "(empty)" : upload) << "\n";

	ss << CommonConfig::str(indent);

	return ss.str();
}
