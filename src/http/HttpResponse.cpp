#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse(void) : _status_code(HttpStatus::OK), _has_file(false) {
}

HttpResponse::~HttpResponse(void) {
	if (_file_stream.is_open())
		_file_stream.close();
}

void	HttpResponse::setStatusCode(HttpStatus::Code code) {
	_status_code = code;
}

void	HttpResponse::setHeader(const std::string &key, const std::string &value) {
	_headers[key] = value;
}

void	HttpResponse::setBody(const std::string &body_str) {
	_body_string = body_str;
	_has_file = false;
	setHeader("Content-Length", _intToString(_body_string.length()));
}

bool	HttpResponse::setFileBody(const std::string &filepath) {
	_file_stream.open(filepath.c_str(), std::ios::binary);

	if (!_file_stream.is_open())
		return false;

	_file_stream.seekg(0, std::ios::end);
	size_t size = _file_stream.tellg();
	_file_stream.seekg(0, std::ios::beg);

	_has_file = true;
	setHeader("Content-Length", _intToString(size));

	return true;
}

void	HttpResponse::build(void) {
	_header_buffer = _generateStatusLine() + "\r\n";

	std::map<std::string, std::string>::const_iterator it;
	for (it = _headers.begin(); it != _headers.end(); ++it)
		_header_buffer += it->first + ": " + it->second + "\r\n";

	_header_buffer += "\r\n";

	if (!_has_file && !_body_string.empty())
		_header_buffer += _body_string;
}

const std::string	&HttpResponse::getHeaderBuffer(void) const {
	return _header_buffer;
}

bool	HttpResponse::hasFile(void) const {
	return _has_file;
}

std::ifstream	&HttpResponse::getFileStream(void) {
	return _file_stream;
}

std::string	HttpResponse::_intToString(size_t value) const {
	std::ostringstream	oss;
	oss << value;
	return oss.str();
}

std::string	HttpResponse::_generateStatusLine(void) const {
	std::string	reason;

	switch (_status_code) {
		case HttpStatus::OK: reason = "OK"; break;
		case HttpStatus::BadRequest: reason = "Bad Request"; break;
		case HttpStatus::Forbidden: reason = "Forbidden"; break;
		case HttpStatus::NotFound: reason = "Not Found"; break;
		case HttpStatus::MethodNotAllowed: reason = "Method Not Allowed"; break;
		case HttpStatus::PayloadTooLarge: reason = "Payload Too Large"; break;
		case HttpStatus::InternalServerError: reason = "Internal Server Error"; break;
		case HttpStatus::NotImplemented: reason = "Not Implemented"; break;
		case HttpStatus::HTTPVersionNotSupported: reason = "HTTP Version Not Supported"; break;
		default: reason = "Unknown"; break;
	}

	return "HTTP/1.1 " + _intToString(static_cast<size_t>(_status_code)) + " " + reason;
}

void HttpResponse::reset() {
	_status_code = HttpStatus::OK;
	_headers.clear();
	_body_string.clear();
	_header_buffer.clear();
	_file_stream.close();
	_has_file = false;
}
