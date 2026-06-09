#include <CommonConfig.hpp>

CommonConfig::CommonConfig()	
	: root(""),
	autoindex(false),
	client_max_body_size(1),
	cgi_extension(""),
	cgi_path(""),
	return_val("") {
	// pass
}

CommonConfig::~CommonConfig() {
	// pass
}

void	CommonConfig::resetConf(void) {
	this->root = "";
	this->index.clear();
	this->autoindex = false;
	this->client_max_body_size = 1;
	this->cgi_extension = "";
	this->cgi_path = "";
	this->error_page.clear();
	this->return_val = "";
}
