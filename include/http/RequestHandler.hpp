#pragma once

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ServerConfig.hpp"
#include "CommonConfig.hpp"
#include "HttpStatus.hpp"
#include <string>

class RequestHandler {
	public:
		RequestHandler(const HttpRequest &request, HttpResponse &response, const ServerConfig &config);
		~RequestHandler(void);

		void	handle(void);

	private:
		const HttpRequest	&_request;
		HttpResponse		&_response;
		const ServerConfig	&_config;

		const CommonConfig	*_route;

		bool	_isBodySizeValid(void) const;
		bool	_isMethodAllowed(void) const;

		std::string	_resolvePath(void) const;

		void	_handleGet(const std::string &real_path);
		void	_handlePost(const std::string &real_path);
		void	_handleDelete(const std::string &real_path);

		void	_handleDirectory(const std::string &real_path);

		bool	_isCgiExtension(const std::string &real_path) const;
		void	_handleCGI(const std::string &real_path);

		void		_buildErrorResponse(HttpStatus::Code code);
		std::string	_guessMimeType(const std::string &path) const;
};
