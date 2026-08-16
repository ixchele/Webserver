#pragma once

#include "HttpStatus.hpp"
#include <string>
#include <map>
#include <fstream>

class HttpResponse {
	public:
		HttpResponse(void);
		~HttpResponse(void);

		void	setStatusCode(HttpStatus::Code code);
		void	setHeader(const std::string &key, const std::string &value);

		void	setBody(const std::string &body_str);
		bool	setFileBody(const std::string &filepath);

		void	build(void);

		const std::string	&getHeaderBuffer(void) const;
		bool				hasFile(void) const;
		std::ifstream		&getFileStream(void);
		int					getFileFd(void) const;
		off_t					getFileSize(void) const;

		void reset();

	private:
		HttpStatus::Code					_status_code;
		std::map<std::string, std::string>	_headers;

		std::string	_body_string;
		std::string	_header_buffer;

		std::ifstream	_file_stream;
		int				_file_fd;
		off_t			_file_size;
		bool			_has_file;

		std::string	_generateStatusLine(void) const;
		std::string	_intToString(size_t value) const;
};
