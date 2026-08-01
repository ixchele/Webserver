#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse() 
	: _status_code(HttpStatus::OK), _file_size(0), _has_file(false), _is_built(false) {}

HttpResponse::~HttpResponse() {
	if (_file_stream.is_open())
		_file_stream.close();
}

void    HttpResponse::setStatusCode(HttpStatus::Code code) { _status_code = code; }

void    HttpResponse::setHeader(const std::string &key, const std::string &value) {
	_headers[key] = value;
}

void    HttpResponse::setBody(const std::string &body_str) {
	_body_string = body_str;
	_has_file = false;
	setHeader("Content-Length", _intToString(_body_string.length()));
}

bool    HttpResponse::setFileBody(const std::string &filepath) {
	_file_stream.open(filepath.c_str(), std::ios::binary);

	if (!_file_stream.is_open())
		return false;

	_file_stream.seekg(0, std::ios::end);
	_file_size = _file_stream.tellg();
	_file_stream.seekg(0, std::ios::beg);

	_has_file = true;
	setHeader("Content-Length", _intToString(_file_size));

	return true;
}

void    HttpResponse::build() {
	_header_buffer = _generateStatusLine() + "\r\n";

	std::map<std::string, std::string>::const_iterator	it;
	for (it = _headers.begin(); it != _headers.end(); ++it) {
		_header_buffer += it->first + ": " + it->second + "\r\n";
	}

	_header_buffer += "\r\n";

	if (!_has_file && !_body_string.empty()) {
		_header_buffer += _body_string;
	}

	_is_built = true;
}

const std::string    &HttpResponse::getHeaderBuffer() const { return _header_buffer; }
bool                 HttpResponse::hasFile() const { return _has_file; }
std::ifstream        &HttpResponse::getFileStream() { return _file_stream; }

std::string    HttpResponse::_generateStatusLine() const {
	return "HTTP/1.1 " + _intToString(_status_code) + " OK";
}

std::string    HttpResponse::_intToString(size_t value) const {
	std::ostringstream    oss;
	oss << value;
	return oss.str();
}
