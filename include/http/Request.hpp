#pragma once

#include "HttpStatus.hpp"
#include "Uri.hpp"
#include "HttpMethod.hpp"
#include <string>
#include <map>
#include <fstream>

class HttpRequest {
	public:
		typedef HttpMethod Method;

		enum ParseState {
			REQUEST_LINE,
			HEADERS,
			HEADERS_COMPLETE,
			BODY,
			COMPLETE,
			ERROR
		};

		HttpRequest(int client_fd);
		~HttpRequest();

		void	parse(const std::string &raw_data);
		void	clear();
		void	prepareBodyStorage(const std::string &filepath);

		ParseState			getState() const;
		HttpStatus::Code	getErrorCode() const;
		Method				getMethod() const;
		const Uri			&getUri() const;
		const std::string	&getVersion() const;
		const std::string	&getTmpFilename() const;
		size_t				getContentLength() const;
		const std::map<std::string, std::string>	&getHeaders() const;

	private:
		int					_client_fd;
		ParseState			_state;
		HttpStatus::Code	_code;

		Method		_method;
		Uri			_uri;
		std::string	_version;
		std::map<std::string, std::string>	_headers;

		std::string		_temp_filename;
		std::ofstream	_body_file;
		size_t			_content_length;
		size_t			_bytes_received;

		std::string		_buffer;

		void	_parseRequestLine(const std::string &line);
		void	_parseHeaders(const std::string &header);
		void	_extractLeftover();
};
