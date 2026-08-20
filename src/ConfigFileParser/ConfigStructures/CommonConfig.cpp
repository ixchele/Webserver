#include <CommonConfig.hpp>
#include <sstream>

CommonConfig::CommonConfig()	
	: root(""),
	autoindex(false),
	client_max_body_size(1024),
    methods(0),
	return_val(""),
	return_status(0) {
	// pass
}

CommonConfig::~CommonConfig() {
	// pass
}

void	CommonConfig::resetConf(void) {
	this->root = "";
	this->index.clear();
	this->autoindex = false;
	this->client_max_body_size = 1024;
    this->methods = 0;
	this->cgi_pass.clear();
	this->error_page.clear();
	this->return_val = "";
	this->return_status = 0;
}


std::string CommonConfig::str(const std::string& indent) const {
    std::stringstream ss;

    ss << indent << "root: " << (root.empty() ? "(empty)" : root) << "\n";
    
    ss << indent << "index: ";
    if (index.empty()) {
        ss << "(empty)";
    } else {
        for (std::size_t i = 0; i < index.size(); ++i) {
            ss << index[i] << " ";
        }
    }
    ss << "\n";

    ss << indent << "autoindex: " << (autoindex ? "on" : "off") << "\n";
    
    ss << indent << "client_max_body_size: " << client_max_body_size << "\n";

    ss << indent << "cgi_pass:\n";
    if (cgi_pass.empty()) {
        ss << indent << "  (empty)\n";
    } else {
        for (std::map<std::string, std::string>::const_iterator it = cgi_pass.begin(); it != cgi_pass.end(); ++it) {
            ss << indent << "  " << it->first << " -> " << it->second << "\n";
        }
    }

    ss << indent << "error_page:\n";
    if (error_page.empty()) {
        ss << indent << "  (empty)\n";
    } else {
        for (std::map<int, std::string>::const_iterator it = error_page.begin(); it != error_page.end(); ++it) {
            ss << indent << "  " << it->first << " -> " << it->second << "\n";
        }
    }

    ss << indent << "return_val: " << (return_val.empty() ? "(empty)" : return_val) << "\n";
    ss << indent << "return_status: " << return_status << "\n";

    return ss.str();
}
