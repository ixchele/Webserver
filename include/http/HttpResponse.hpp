#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include "HttpStatus.hpp"
#include <string>
#include <map>
#include <fstream>

class HttpResponse {
	public:
		HttpResponse();
		~HttpResponse();

		void	setStatusCode(HttpStatus::Code code);
		void	setHeader(const std::string &key, const std::string &value);

		void	setBody(const std::string &body_str);
		bool	setFileBody(const std::string &filepath);

		void	build();

		const std::string	&getHeaderBuffer() const;
		bool				hasFile() const;
		std::ifstream		&getFileStream();

	private:
		HttpStatus::Code					_status_code;
		std::map<std::string, std::string>	_headers;

		std::string	_header_buffer;
		std::string	_body_string;

		std::ifstream	_file_stream;
		size_t			_file_size;
		bool			_has_file;
		bool			_is_built;

		std::string	_generateStatusLine() const;
		std::string	_intToString(size_t value) const;
};

#endif
