#include "RequestHandler.hpp"
#include <cstdlib>
#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <Logger.hpp>
#include <dirent.h>
#include <fstream>
#include <cstdio>
#include <cerrno>
#include <cstring>

RequestHandler::RequestHandler(const HttpRequest &request, HttpResponse &response, const ServerConfig &config)
	: _request(request), _response(response), _config(config), _route(NULL) {
		// pass
	}

RequestHandler::~RequestHandler(void) {
	// pass
}

void	RequestHandler::handle(void) {
	if (_request.getState() == HttpRequest::ERROR) {
		_buildErrorResponse(_request.getErrorCode());
		return;
	}

	_route = _config.matchRoute(_request.getUri().getPath());

	if (!_route->return_val.empty()) {
		HttpStatus::Code	code = _route->return_status != 0
			? static_cast<HttpStatus::Code>(_route->return_status)
			: HttpStatus::Found;
		_response.setStatusCode(code);
		_response.setHeader("Location", _route->return_val);
		_response.setHeader("Content-Type", "text/html");
		_response.setHeader("Content-Lenght", "0");
		_response.setHeader("Connection", "close");
		_response.build();
		return;
	}

	{
		std::string real_path_tmp = _route ? _resolvePath() : _request.getUri().getPath();
		if (isCgiRequest(real_path_tmp)) {
			return;
		}
	}

	if (!_isBodySizeValid()) {
		_buildErrorResponse(HttpStatus::PayloadTooLarge);
		return;
	}

	if (!_isMethodAllowed()) {
		_buildErrorResponse(HttpStatus::MethodNotAllowed);
		return;
	}

	std::string	real_path = _resolvePath();

	switch (_request.getMethod()) {
		case HTTP_GET:
			_handleGet(real_path);
			break;
		case HTTP_POST:
			_handlePost(real_path);
			break;
		case HTTP_DELETE:
			_handleDelete(real_path);
			break;
		default:
			_buildErrorResponse(HttpStatus::NotImplemented); // NOTE: 501
			break;
	}
}

bool	RequestHandler::_isBodySizeValid(void) const {
	if (_route->client_max_body_size <= 0)
		return true;

	size_t	body_size = _request.getBytesReceived();
	if (_request.getContentLength() > body_size)
		body_size = _request.getContentLength();

	if (body_size > static_cast<size_t>(_route->client_max_body_size))
		return false;

	return true;
}

bool	RequestHandler::_isMethodAllowed(void) const {
	int    method_bit = 0;

	switch (_request.getMethod()) {
		case HTTP_GET:    method_bit = 1; break; // 001
		case HTTP_POST:   method_bit = 2; break; // 010
		case HTTP_DELETE: method_bit = 4; break; // 100
		default: return false;
	}

	return (_route->methods & method_bit) != 0;
}

std::string	RequestHandler::_resolvePath(void) const {
	std::string	path = _route->root;
	std::string	uri_path = _request.getUri().getPath();

	if (path.length() > 0 && path[path.length() - 1] == '/' && uri_path.length() > 0 && uri_path[0] == '/')
		uri_path = uri_path.substr(1);

	else if (path.length() > 0 && path[path.length() - 1] != '/' && uri_path.length() > 0 && uri_path[0] != '/')
		path += "/";

	char	buff[PATH_MAX];

	if (realpath((path + uri_path).c_str(), buff) != NULL) {
		return std::string(buff);
	}
	return "";
}


void	RequestHandler::_buildErrorResponse(HttpStatus::Code code) {
	_response.setStatusCode(code);
	_response.setHeader("Content-Type", "text/html");

	bool	custom_page_loaded = false;
	int		code_int = static_cast<int>(code);

	if (_route != NULL) {
		std::map<int, std::string>::const_iterator it = _route->error_page.find(code_int);

		if (it != _route->error_page.end()) {
			std::string	error_path = _route->root;
			std::string	page_name = it->second;

			if (error_path.length() > 0 && error_path[error_path.length() - 1] != '/')
				error_path += "/";
			if (page_name.length() > 0 && page_name[0] == '/')
				page_name = page_name.substr(1);

			error_path += page_name;

			if (_response.setFileBody(error_path)) {
				custom_page_loaded = true;
			}
		}
	}

	if (!custom_page_loaded) {
		std::ostringstream	oss;
		oss << code_int;
		std::string	code_str = oss.str();

		std::string	html = "<html>\r\n"
			"<head><title>" + code_str + " Error</title></head>\r\n"
			"<body style=\"font-family: Arial, sans-serif; text-align: center; margin-top: 50px;\">\r\n"
			"    <h1>" + code_str + " - " + HttpStatus::codeMessage((HttpStatus::Code)code_int) + "</h1>\r\n"
			"    <hr style=\"width: 50%;\">\r\n"
			"    <p>webserv/1.0 (1337)</p>\r\n"
			"</body>\r\n"
			"</html>\r\n";

		_response.setBody(html);
	}

	_response.build();
}


void	RequestHandler::_handleGet(const std::string &real_path) {
	struct stat	file_stat;

	if (stat(real_path.c_str(), &file_stat) != 0) {
		_buildErrorResponse(HttpStatus::NotFound); // 404
		return;
	}

	if (access(real_path.c_str(), R_OK) != 0) {
		_buildErrorResponse(HttpStatus::Forbidden); // 403
		return;
	}

	if (S_ISDIR(file_stat.st_mode)) {
		_handleDirectory(real_path);
		return;
	}

	if (_isCgiExtension(real_path)) {
		_handleCGI(real_path);
		return;
	}

	_response.setStatusCode(HttpStatus::OK);

	_response.setHeader("Content-Type", _guessMimeType(real_path));

	if (!_response.setFileBody(real_path)) {
		_buildErrorResponse(HttpStatus::InternalServerError);
		return;
	}

	_response.build();
}

std::string	RequestHandler::_guessMimeType(const std::string &path) const {
	size_t	dot_pos = path.find_last_of('.');

	if (dot_pos == std::string::npos)
		return "application/octet-stream";

	std::string	ext = path.substr(dot_pos);

	if (ext == ".html" || ext == ".htm")	return "text/html";
	if (ext == ".css")						return "text/css";
	if (ext == ".js")						return "application/javascript";
	if (ext == ".jpg" || ext == ".jpeg")	return "image/jpeg";
	if (ext == ".png")						return "image/png";
	if (ext == ".gif")						return "image/gif";
	if (ext == ".ico")						return "image/x-icon";
	if (ext == ".json")						return "application/json";
	if (ext == ".txt")						return "text/plain";
	if (ext == ".pdf")						return "application/pdf";
	if (ext == ".mp4")						return "video/mp4";
	if (ext == ".mp3")						return "audio/mpeg";

	return "application/octet-stream";
}

bool	RequestHandler::_isCgiExtension(const std::string &real_path) const {
	size_t	dot_pos = real_path.find_last_of('.');
	if (dot_pos == std::string::npos)
		return false;
	std::string	ext = real_path.substr(dot_pos);

	if (_route != NULL && _route->cgi_pass.find(ext) != _route->cgi_pass.end())
		return true;
	if (_config.cgi_pass.find(ext) != _config.cgi_pass.end())
		return true;
	for (size_t i = 0; i < _config.locations.size(); ++i) {
		const LocationConfig &loc = _config.locations[i];
		if (loc.cgi_pass.find(ext) != loc.cgi_pass.end())
			return true;
	}
	return false;
}

// --- API CGI précise ---

bool	RequestHandler::isCgiRequest(const std::string &real_path) const {
	return _isCgiExtension(real_path);
}

bool	RequestHandler::isCgi() const {
	std::string real;
	if (_route != NULL)
		real = _resolvePath();
	else
		real = _request.getUri().getPath();
	return _isCgiExtension(real);
}

bool	RequestHandler::isCgiGet() const {
	return isCgi() && _request.getMethod() == HTTP_GET;
}

bool	RequestHandler::isCgiPost() const {
	return isCgi() && _request.getMethod() == HTTP_POST;
}

bool	RequestHandler::isCgiDelete() const {
	return isCgi() && _request.getMethod() == HTTP_DELETE;
}

RequestHandler::CgiMode	RequestHandler::getCgiMode() const {
	if (!isCgi())
		return CGI_NONE;
	switch (_request.getMethod()) {
		case HTTP_GET: return CGI_GET;
		case HTTP_POST: return CGI_POST;
		case HTTP_DELETE: return CGI_DELETE;
		default: return CGI_NONE;
	}
}

int	RequestHandler::getBodyFd() const {
	return _request.openBodyFile();
}

std::string	RequestHandler::getBodyFilePath() const {
	return _request.getTmpFilename();
}

std::string	RequestHandler::getCgiInterpreter(const std::string &real_path) const {
	size_t dot_pos = real_path.find_last_of('.');
	if (dot_pos == std::string::npos)
		return "";
	std::string ext = real_path.substr(dot_pos);

	if (_route != NULL) {
		std::map<std::string, std::string>::const_iterator it = _route->cgi_pass.find(ext);
		if (it != _route->cgi_pass.end())
			return it->second;
	}
	std::map<std::string, std::string>::const_iterator it2 = _config.cgi_pass.find(ext);
	if (it2 != _config.cgi_pass.end())
		return it2->second;
	for (size_t i = 0; i < _config.locations.size(); ++i) {
		const LocationConfig &loc = _config.locations[i];
		std::map<std::string, std::string>::const_iterator it3 = loc.cgi_pass.find(ext);
		if (it3 != loc.cgi_pass.end())
			return it3->second;
	}
	return "";
}

std::string	RequestHandler::getCgiScriptPath() const {
	if (_route == NULL)
		return "";
	return _resolvePath();
}

std::string	RequestHandler::getCgiInterpreter() const {
	std::string script = getCgiScriptPath();
	if (script.empty())
		script = _request.getUri().getPath();
	return getCgiInterpreter(script);
}

std::string	RequestHandler::getUploadDestination() const {
	if (_route == NULL)
		return "";
	if (_request.getMethod() != HTTP_POST)
		return "";
	const LocationConfig *loc = dynamic_cast<const LocationConfig *>(_route);
	if (loc == NULL || loc->upload.empty())
		return "";
	std::string dir_path = loc->upload;
	std::string uri_path = _request.getUri().getPath();
	std::string base_name = uri_path;
	size_t last_slash = uri_path.find_last_of('/');
	if (last_slash != std::string::npos)
		base_name = uri_path.substr(last_slash + 1);
	if (base_name.empty() || base_name == "." || base_name == "..")
		base_name = "upload";
	if (dir_path.length() > 0 && dir_path[dir_path.length() - 1] != '/')
		dir_path += "/";
	return dir_path + base_name;
}

void	RequestHandler::_handleCGI(const std::string &real_path) {
	LOG_DEBUG << "[CGI STUB] request: " << real_path
	          << " mode=" << getCgiMode()
	          << " interpreter=" << getCgiInterpreter(real_path)
	          << " body_fd=" << getBodyFd()
	          << " body_path=" << getBodyFilePath()
	          << " upload_dst=" << getUploadDestination();
	return;
}

void    RequestHandler::_handleDirectory(const std::string &real_path) {
	std::string	target_index_path = "";
	bool		index_found = false;

	for (size_t i = 0; i < _route->index.size(); ++i) {

		std::string	test_path = real_path;
		if (test_path.length() > 0 && test_path[test_path.length() - 1] != '/')
			test_path += "/";
		test_path += _route->index[i];

		struct stat	file_stat;
		if (stat(test_path.c_str(), &file_stat) == 0 && access(test_path.c_str(), R_OK) == 0 && !S_ISDIR(file_stat.st_mode)) {
			target_index_path = test_path;
			index_found = true;
			break;
		}
	}

	if (index_found) {
		_response.setStatusCode(HttpStatus::OK);
		_response.setHeader("Content-Type", _guessMimeType(target_index_path));

		if (!_response.setFileBody(target_index_path))
			_buildErrorResponse(HttpStatus::InternalServerError); // 500
		else
			_response.build();
		return;
	}

	if (!_route->autoindex) {
		_buildErrorResponse(HttpStatus::Forbidden); // 403
		return;
	}

	DIR	*dir = opendir(real_path.c_str());
	if (dir == NULL) {
		_buildErrorResponse(HttpStatus::Forbidden); // 403
		return;
	}

	std::string	uri_path = _request.getUri().getPath();
	std::string	html = "<html>\r\n<head><title>Index of " + uri_path + "</title></head>\r\n"
		"<body>\r\n<h1>Index of " + uri_path + "</h1>\r\n<hr><pre>\n";

	if (uri_path != "/") {
		html += "<a href=\"../\">../</a>\n";
	}

	struct dirent	*entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string	name = entry->d_name;

		if (name == "." || name == "..")
			continue;

		std::string	href = uri_path;
		if (href.length() > 0 && href[href.length() - 1] != '/')
			href += "/";
		href += name;

		if (entry->d_type == DT_DIR) {
			name += "/";
			href += "/";
		}

		html += "<a href=\"" + href + "\">" + name + "</a>\n";
	}
	closedir(dir);

	html += "</pre><hr></body>\r\n</html>\r\n";

	_response.setStatusCode(HttpStatus::OK);
	_response.setHeader("Content-Type", "text/html");
	_response.setBody(html);
	_response.build();
}

void    RequestHandler::_handlePost(const std::string &real_path) {
	if (_isCgiExtension(real_path)) {
		_handleCGI(real_path);
		return;
	}

	if (_request.getBytesReceived() == 0) {
		_response.setStatusCode(HttpStatus::NoContent); // 204
		_response.build();
		return;
	}

	const LocationConfig	*loc = dynamic_cast<const LocationConfig *>(_route);
	if (loc == NULL || loc->upload.empty()) {
		_response.setStatusCode(HttpStatus::NoContent); // 204
		_response.build();
		return;
	}

	std::string	dir_path = loc->upload;
	std::string	uri_path = _request.getUri().getPath();
	std::string	base_name = uri_path;
	size_t		last_slash = uri_path.find_last_of('/');

	if (last_slash != std::string::npos)
		base_name = uri_path.substr(last_slash + 1);

	if (base_name.empty() || base_name == "." || base_name == "..")
		base_name = "upload";

	if (dir_path.length() > 0 && dir_path[dir_path.length() - 1] != '/')
		dir_path += "/";

	std::string	full_path = dir_path + base_name;

	const std::string &tmp_path = _request.getTmpFilename();
	if (!tmp_path.empty()) {
		struct stat st;
		if (stat(tmp_path.c_str(), &st) != 0) {
			_buildErrorResponse(HttpStatus::InternalServerError);
			return;
		}
		if (::rename(tmp_path.c_str(), full_path.c_str()) != 0) {
			if (errno == EXDEV) {
				std::ifstream src(tmp_path.c_str(), std::ios::binary);
				std::ofstream dst(full_path.c_str(), std::ios::binary | std::ios::trunc);
				if (!src.is_open() || !dst.is_open()) {
					LOG_ERROR << "cross-device copy failed: src=" << tmp_path << " dst=" << full_path << " : " << strerror(errno);
					_buildErrorResponse(HttpStatus::InternalServerError);
					return;
				}
				dst << src.rdbuf();
				if (!dst) {
					_buildErrorResponse(HttpStatus::InternalServerError);
					return;
				}
				src.close();
				dst.close();
				::unlink(tmp_path.c_str());
			} else {
				LOG_ERROR << "rename(" << tmp_path << " -> " << full_path << ") failed: " << strerror(errno);
				_buildErrorResponse(HttpStatus::InternalServerError);
				return;
			}
		}
		_response.setStatusCode(HttpStatus::Created);
		_response.setBody("Upload OK");
		_response.build();
		return;
	}

	const std::string &body = _request.getBody();
	if (!body.empty()) {
		std::ofstream	out(full_path.c_str(), std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			_buildErrorResponse(HttpStatus::InternalServerError);
			return;
		}
		out.write(body.c_str(), static_cast<std::streamsize>(body.size()));
		out.close();
		_response.setStatusCode(HttpStatus::Created);
		_response.setBody("Upload OK");
		_response.build();
		return;
	}

	_buildErrorResponse(HttpStatus::InternalServerError);
}

void    RequestHandler::_handleDelete(const std::string &real_path) {
	if (_isCgiExtension(real_path)) {
		_handleCGI(real_path);
		return;
	}
	struct stat	file_stat;

	if (stat(real_path.c_str(), &file_stat) != 0) {
		_buildErrorResponse(HttpStatus::NotFound); // 404
		return;
	}

	if (S_ISDIR(file_stat.st_mode)) {
		_buildErrorResponse(HttpStatus::Forbidden); // 403
		return;
	}

	if (unlink(real_path.c_str()) == 0) {
		_response.setStatusCode(HttpStatus::NoContent); // 204
		_response.build();

	}
	else
		_buildErrorResponse(HttpStatus::Forbidden); // 403
}
