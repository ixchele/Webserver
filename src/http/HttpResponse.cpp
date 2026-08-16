#include "HttpResponse.hpp"

#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

HttpResponse::HttpResponse(void) : _status_code(HttpStatus::OK), _file_fd(-1), _file_size(0),_has_file(false)
{
}

HttpResponse::~HttpResponse(void)
{
	if (_file_stream.is_open())
		_file_stream.close();
	if (_file_fd != -1)
		close(_file_fd);
}

void HttpResponse::setStatusCode(HttpStatus::Code code)
{
	_status_code = code;
}

void HttpResponse::setHeader(const std::string &key, const std::string &value)
{
	_headers[key] = value;
}

void HttpResponse::setBody(const std::string &body_str)
{
	_body_string = body_str;
	_has_file = false;
	setHeader("Content-Length", _intToString(_body_string.length()));
}

bool HttpResponse::setFileBody(const std::string &filepath)
{
	struct stat file_stat;
	int fd = open(filepath.c_str(), O_RDONLY);

	if (fd == -1)
		return false;

	if (fstat(fd, &file_stat) != 0)
	{
		close(fd);
		return false;
	}

	if (_file_fd != -1)
		close(_file_fd);
	_file_fd = fd;

	_file_stream.open(filepath.c_str(), std::ios::binary);
	if (!_file_stream.is_open())
	{
		close(_file_fd);
		_file_fd = -1;
		return false;
	}

	_has_file = true;
	_file_size = file_stat.st_size;
	setHeader("Content-Length", _intToString(static_cast<size_t>(_file_size)));

	return true;
}

void HttpResponse::build(void)
{
	_header_buffer = _generateStatusLine() + "\r\n";

	std::map<std::string, std::string>::const_iterator it;
	for (it = _headers.begin(); it != _headers.end(); ++it)
		_header_buffer += it->first + ": " + it->second + "\r\n";

	_header_buffer += "\r\n";

	if (!_has_file && !_body_string.empty())
		_header_buffer += _body_string;
}

const std::string &HttpResponse::getHeaderBuffer(void) const
{
	return _header_buffer;
}

bool HttpResponse::hasFile(void) const
{
	return _has_file;
}

std::ifstream &HttpResponse::getFileStream(void)
{
	return _file_stream;
}

int HttpResponse::getFileFd(void) const
{
	return _file_fd;
}

off_t HttpResponse::getFileSize(void) const
{
	return _file_size;
}

std::string HttpResponse::_intToString(size_t value) const
{
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

std::string HttpResponse::_generateStatusLine(void) const
{
	return "HTTP/1.1 " + _intToString(static_cast<size_t>(_status_code)) + " " + HttpStatus::codeMessage(_status_code);
}

void HttpResponse::reset()
{
	_status_code = HttpStatus::OK;
	_headers.clear();
	_body_string.clear();
	_header_buffer.clear();
	if (_file_stream.is_open())
		_file_stream.close();
	if (_file_fd != -1)
	{
		close(_file_fd);
		_file_fd = -1;
	}
	_has_file = false;
}
