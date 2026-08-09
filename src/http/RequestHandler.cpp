#include "RequestHandler.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>

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

	if (_request.getContentLength() > static_cast<size_t>(_route->client_max_body_size))
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

	return path + uri_path;
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
			"    <h1>" + code_str + " - Une erreur est survenue</h1>\r\n"
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

	// TODO : plug isCgiExtension later

	// if (_isCgiExtension(real_path)) {
	// 	_handleCGI(real_path);
	// 	return;
	// }

	_response.setStatusCode(HttpStatus::OK); // 200

	_response.setHeader("Content-Type", _guessMimeType(real_path));

	if (!_response.setFileBody(real_path)) {
		_buildErrorResponse(HttpStatus::InternalServerError); // 500
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

	return "application/octet-stream";
}
