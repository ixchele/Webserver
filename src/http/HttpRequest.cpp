#include "HttpStatus.hpp"
#include "Uri.hpp"
#include <HttpRequest.hpp>
#include <cstddef>
#include <sstream>
#include <string>
#include <unistd.h>
#include <iostream>
#include <cctype>

HttpRequest::HttpRequest(int client_fd) 
    : _client_fd(client_fd), _state(REQUEST_LINE), _content_length(0), _bytes_received(0)
{
	(void)_client_fd;
	(void)_bytes_received;
}

HttpRequest::ParseState	HttpRequest::getState() const { return _state; }

HttpStatus::Code	HttpRequest::getErrorCode() const { return _code; }

HttpRequest::Method	HttpRequest::getMethod() const { return _method; }

const Uri	&HttpRequest::getUri() const { return _uri; }

const std::string	&HttpRequest::getVersion() const { return _version; }

const std::string	&HttpRequest::getTmpFilename() const { return _temp_filename; }

size_t	HttpRequest::getContentLength() const { return _content_length; }

const std::map<std::string, std::string> &HttpRequest::getHeaders() const { return _headers; }

const std::string &HttpRequest::getHeader(const std::string &name) const { return _headers.at(name); }

void	HttpRequest::_parseRequestLine(const std::string &line) {
	std::istringstream	iss(line);
	std::string			method_str, uri, version, extra;

	if (!std::getline(iss, method_str, ' ')
			|| !std::getline(iss, uri, ' ')
			|| !std::getline(iss, version, ' ')
			|| std::getline(iss, extra, ' '))
	{
		_state = ERROR;
		_code = HttpStatus::BadRequest; // NOTE : error 400
		return;
	}

	if (method_str.empty() || uri.empty() || version.empty()) {
		_state = ERROR;
		_code = HttpStatus::BadRequest; // NOTE : error 400
		return;
	}

	if (method_str == "GET") _method = HTTP_GET;
	else if (method_str == "POST") _method = HTTP_POST;
	else if (method_str == "DELETE") _method = HTTP_DELETE;

	else {
		_state = ERROR;
		_code = HttpStatus::NotImplemented; // NOTE : 501
		return;
	}

	if (!_uri.parse(uri)) {
		_state = ERROR;
		_code = HttpStatus::BadRequest; // NOTE : 400
		return;
	}

	if (version != "HTTP/1.1") {
		_state = ERROR;
		_code = HttpStatus::HTTPVersionNotSupported; // NOTE : 505
		return;
	}
	_version = version;

	_state = HEADERS;
}

void HttpRequest::_parseHeaders(const std::string &line) {
    size_t	colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        _state = ERROR;
        _code = HttpStatus::BadRequest; // NOTE : 400
        return;
    }

    std::string	key = line.substr(0, colon_pos);
    std::string	value = line.substr(colon_pos + 1);

    if (key.empty() || key[key.length() - 1] == ' ' || key[key.length() - 1] == '\t') {
        _state = ERROR;
        _code = HttpStatus::BadRequest; // NOTE : 400
        return;
    }

    for (size_t i = 0; i < key.length(); ++i)
        key[i] = std::tolower(key[i]);

    size_t	value_start = value.find_first_not_of(" \t");
    if (value_start != std::string::npos) {
        size_t	value_end = value.find_last_not_of(" \t");
        value = value.substr(value_start, value_end - value_start + 1);
    } else
        value = "";

    _headers[key] = value;
}


void	HttpRequest::parse(const std::string &raw_data) {

	_buffer.append(raw_data);

	while (_state != HEADERS_COMPLETE && _state != COMPLETE && _state != ERROR) {

		if (_state == REQUEST_LINE || _state == HEADERS) {
			size_t pos = _buffer.find("\r\n");

			if (pos == std::string::npos)
				break; 

			std::string line = _buffer.substr(0, pos);

			if (_state == REQUEST_LINE)
				_parseRequestLine(line);

			else if (_state == HEADERS) {
				if (line.empty()) {
					_buffer.erase(0, pos + 2);

					bool	has_body = false;

					if (_headers.find("transfer-encoding") != _headers.end() && 
							_headers["transfer-encoding"] == "chunked") {
						has_body = true;
					} 
					else if (_headers.find("content-length") != _headers.end()) {
						has_body = true;
						std::istringstream	iss(_headers["content-length"]);
						iss >> _content_length;
					}

					_state = has_body ? HEADERS_COMPLETE : COMPLETE;
					break;
				}
				else
					_parseHeaders(line);
			}

			if (_state != HEADERS_COMPLETE) {
				_buffer.erase(0, pos + 2);
			}
		}

		else if (_state == BODY) {
			// TODO : body parsing, handle content lenght and chunked encoding.
			// right now we break to not loop infintly
			break; 
		}
	}
}
