#ifndef REQUESTHANDLER_HPP
#define REQUESTHANDLER_HPP

#include <string>
#include "CommonConfig.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ServerConfig.hpp"

// TODO : RequestHandler
// find uri configuration
// check if the request is legal
// execute GET | POST | DELETE

// 

class RequestHandler {
	public:
		RequestHandler(const HttpRequest &request, HttpResponse &response, const ServerConfig &config);
		~RequestHandler();

		void	handle();

	private:
		const HttpRequest	&_request;
		HttpResponse		&_response;
		const ServerConfig	&_config;
		const CommonConfig	*_route;

		bool	_isMethodAllowed() const;
		bool	_isBodySizeValid() const;


		void	_handleGet(const std::string &real_path);
		void	_handlePost(const std::string &real_path);
		void	_handleDelete(const std::string &real_path);

		// std::string	_resolvePath(const std::string &uri_path) const;
		// std::string	_guessMimeType(const std::string &path) const;
		//
		// bool	_isCgiExtension(const std::string &real_path) const;
		// void	_handleCGI(const std::string &real_path);
		// void	_handleDirectory(const std::string &real_path, const std::string &uri);
		// void	_buildErrorResponse(HttpStatus::Code code);

};

#endif
