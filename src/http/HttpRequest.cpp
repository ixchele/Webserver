#include "HttpStatus.hpp"
#include "Uri.hpp"
#include <HttpRequest.hpp>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unistd.h>
#include <iostream>
#include <cctype>

// hard safety ceiling against OOM for unbounded (chunked) bodies
#define MAX_REQUEST_BODY   (64 * 1024 * 1024)

HttpRequest::HttpRequest(int client_fd) 
    : _client_fd(client_fd), _state(REQUEST_LINE), _code(HttpStatus::OK), _method(HTTP_UNKNOWN),
      _content_length(0), _bytes_received(0), _body_mode(BODY_NONE),
      _chunk_state(CHUNK_SIZE_LINE), _chunk_size(0)
{
	(void)_client_fd;
}

HttpRequest::ParseState	HttpRequest::getState() const { return _state; }

HttpStatus::Code	HttpRequest::getErrorCode() const { return _code; }

HttpRequest::Method	HttpRequest::getMethod() const { return _method; }

std::string	HttpRequest::getMethodStr() const {
	switch(_method)
	{
		case HTTP_GET:		return "GET";
		case HTTP_POST:		return "POST";
		case HTTP_DELETE:	return "DELETE";
		default: return "";
	}
}

const Uri	&HttpRequest::getUri() const { return _uri; }

const std::string	&HttpRequest::getVersion() const { return _version; }

const std::string	&HttpRequest::getTmpFilename() const { return _temp_filename; }

size_t	HttpRequest::getContentLength() const { return _content_length; }

const std::string	&HttpRequest::getBody() const { return _body; }

size_t	HttpRequest::getBytesReceived() const { return _bytes_received; }

const std::map<std::string, std::string> &HttpRequest::getHeaders() const { return _headers; }

std::string HttpRequest::getHeader(const std::string &name) const {
	if (_headers.find(name) != _headers.end())
		return _headers.at(name);
	return "";
}

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

    if (key == "content-length") {
        if (_headers.find("content-length") != _headers.end() || _headers.find("transfer-encoding") != _headers.end()) {
            _state = ERROR;
            _code = HttpStatus::BadRequest;
            return;
        }
        if (!_parseContentLength(value)) {
            _state = ERROR;
            _code = HttpStatus::BadRequest; //NOTE : 400
            return;
        }
    }

    if (key == "transfer-encoding") {
        for (size_t i = 0; i < value.length(); ++i)
            value[i] = std::tolower(value[i]);
        if (value != "chunked") {
            _state = ERROR;
            _code = HttpStatus::NotImplemented; //NOTE : 501
            return;
        }
        if (_headers.find("content-length") != _headers.end()) {
            _state = ERROR;
            _code = HttpStatus::BadRequest; //NOTE : 400 RFC 7230 §3.3.3
            return;
        }
    }

    _headers[key] = value;
}

bool	HttpRequest::_parseContentLength(const std::string &value) {
    if (value.empty())
        return false;

    size_t	num = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        if (!std::isdigit(value[i]))
            return false;
        if (num > (static_cast<size_t>(-1) - (value[i] - '0')) / 10)
            return false;
        num = num * 10 + (value[i] - '0');
    }

    _content_length = num;
    return true;
}


void	HttpRequest::parse(const std::string &raw_data) {
	parse(raw_data.c_str(), raw_data.size());
}

void	HttpRequest::parse(const char *data, size_t len) {
	_buffer.append(data, len);

	while (_state != COMPLETE && _state != ERROR) {

		if (_state == REQUEST_LINE || _state == HEADERS) {
			size_t pos = _buffer.find("\r\n");

			if (pos == std::string::npos)
				break;

			std::string line = _buffer.substr(0, pos);

			if (_state == REQUEST_LINE)
			{
				_parseRequestLine(line);
				_buffer.erase(0, pos + 2);
			}

			else if (_state == HEADERS) {
				if (line.empty()) {
					_buffer.erase(0, pos + 2);
					_prepareBody();
				} else {
					_parseHeaders(line);
					_buffer.erase(0, pos + 2);
				}
			}

			if (_state == ERROR)
				break;
		}

		else if (_state == BODY) {
			if (_body_mode == BODY_CONTENT_LENGTH && _content_length - _bytes_received > 0 && _buffer.empty())
				break; // wait for more data

			_processBody();

			if (_body_mode == BODY_CONTENT_LENGTH && _bytes_received == _content_length)
				_state = COMPLETE;
			else if (_body_mode == BODY_NONE)
				break;
		}
	}
}

void	HttpRequest::_prepareBody(void) {

	if (_headers.find("transfer-encoding") != _headers.end()) {
		if (_headers["transfer-encoding"] == "chunked") {
			_body_mode = BODY_CHUNKED;
			_chunk_state = CHUNK_SIZE_LINE;
			_state = BODY;
			return;
		}
	}

	if (_headers.find("content-length") != _headers.end()) {
		_body_mode = BODY_CONTENT_LENGTH;
		_state = BODY;
		return;
	}

	_state = COMPLETE;
}

void	HttpRequest::_processBody(void) {
	if (_body_mode == BODY_CHUNKED)
		_processChunked();
	else
	{
		size_t remaining = _content_length - _bytes_received;
		size_t take = _buffer.size() < remaining ? _buffer.size() : remaining;

		if (take > 0) {
			_body.append(_buffer, 0, take);
			_bytes_received += take;
			_buffer.erase(0, take);
		}
	}
}

void	HttpRequest::_processChunked(void) {
	while (true) {

		if (_chunk_state == CHUNK_SIZE_LINE) {
			size_t pos = _buffer.find("\r\n");
			if (pos == std::string::npos)
				break;

			std::string line = _buffer.substr(0, pos);

			if (!_parseChunkSize(line)) {
				_state = ERROR;
				_code = HttpStatus::BadRequest;
				return;
			}

			_buffer.erase(0, pos + 2);

			if (_chunk_size == 0) {
				_chunk_state = CHUNK_TRAILERS;
			} else {
				_chunk_state = CHUNK_DATA;
			}
		}

		else if (_chunk_state == CHUNK_DATA) {
			size_t take = _buffer.size() < _chunk_size ? _buffer.size() : _chunk_size;

			if (take > 0) {
				if (_bytes_received + take > MAX_REQUEST_BODY) {
					_state = ERROR;
					_code = HttpStatus::PayloadTooLarge;
					return;
				}
				_body.append(_buffer, 0, take);
				_buffer.erase(0, take);
				_bytes_received += take;
				_chunk_size -= take;
			}

			if (_chunk_size == 0)
				_chunk_state = CHUNK_CRLF; // wait for the terminating CRLF
			else
				break; // need more data for this chunk
		}

		else if (_chunk_state == CHUNK_CRLF) {
			if (_buffer.size() < 2)
				break;
			if (_buffer.compare(0, 2, "\r\n") != 0) {
				_state = ERROR;
				_code = HttpStatus::BadRequest;
				return;
			}
			_buffer.erase(0, 2);
			_chunk_state = CHUNK_SIZE_LINE;
		}

		else if (_chunk_state == CHUNK_TRAILERS) {
			if (_buffer.size() < 2)
				break;
			// trailer section ends on an empty line; discard trailers
			size_t pos = _buffer.find("\r\n\r\n");
			if (pos == std::string::npos)
				break;
			_buffer.erase(0, pos + 4);
			_state = COMPLETE;
			return;
		}
	}
}

bool	HttpRequest::_parseChunkSize(const std::string &line) {
	size_t	pos = line.find(';');
	std::string hexpart = pos == std::string::npos ? line : line.substr(0, pos);
	if (hexpart.empty())
		return false;

	size_t	num = 0;
	for (size_t i = 0; i < hexpart.length(); ++i) {
		char c = hexpart[i];
		unsigned int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else return false;

		if (num > (static_cast<size_t>(-1) - d) / 16)
			return false;
		num = num * 16 + d;
	}

	_chunk_size = num;
	return true;
}

bool HttpRequest::isBufferEmpty() { return _buffer.empty(); }

void	HttpRequest::setState(ParseState state) {
	_state = state;
}

void	HttpRequest::setErrorCode(HttpStatus::Code code) {
	_code = code;
}

void HttpRequest::reset() {
	_state = REQUEST_LINE;
	_code = HttpStatus::OK;
	_content_length = 0;
	_bytes_received = 0;
	_uri.reset();
	_version.clear();
	_headers.clear();
	_temp_filename.clear();
	_body_file.close();
	_content_length = 0;
	_bytes_received = 0;
	_buffer.clear();
	_body.clear();
	_body_mode = BODY_NONE;
	_chunk_state = CHUNK_SIZE_LINE;
	_chunk_size = 0;
}
