#include <Request.hpp>
#include <cstddef>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

Request::Request(int client_fd) {
	this->_client_fd = client_fd;
}

Request::ParseState	Request::getState() const {
	return _state;
}

Request::Method	Request::getMethod() const {
	return _method;
}

const std::string &Request::getUri() const {
	return _uri;
}

const std::string	&Request::getVersion() const {
	return _version;
}

const std::map<std::string, std::string> &Request::getHeaders() const {
	return _headers; 
}

const std::string &Request::getTempFilename() const {
	return _temp_filename;
}

size_t	Request::getContentLength() const {
	return _content_length;
}

void	Request::_parseRequestLine(const std::string &line) {
	
}
