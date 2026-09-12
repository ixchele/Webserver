#pragma once

#include "HttpStatus.hpp"
#include "Uri.hpp"
#include "HttpMethod.hpp"
#include <string>
#include <map>
#include <fstream>

#define MAX_HEADER_SIZE 8192

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
		void	parse(const char *data, size_t len);

		ParseState			getState() const;
		HttpStatus::Code	getErrorCode() const;
		Method				getMethod() const;
		std::string			getMethodStr() const;
		const Uri			&getUri() const;
		const std::string	&getVersion() const;
		const std::string	&getTmpFilename() const;
		size_t				getContentLength() const;
		const std::string	&getBody() const;
		size_t				getBytesReceived() const;
		std::string			getPathName() const;
		const std::map<std::string, std::string>	&getHeaders() const;
		std::string	getHeader(const std::string &name) const;

		bool isBufferEmpty();

		int		openBodyFile() const;
		bool	hasBodyFile() const;

		void	setState(ParseState state);
		void	setErrorCode(HttpStatus::Code code);
		void	setPathName(std::string path_name);

		void reset();



	private:
		enum BodyMode {
			BODY_NONE,
			BODY_CONTENT_LENGTH,
			BODY_CHUNKED
		};

		enum ChunkState {
			CHUNK_SIZE_LINE,
			CHUNK_DATA,
			CHUNK_CRLF,
			CHUNK_TRAILERS
		};

		int					_client_fd;
		ParseState			_state;
		HttpStatus::Code	_code;

		Uri			_uri;

		Method		_method;
		std::string	_version;
		std::map<std::string, std::string>	_headers;

		std::string		_temp_filename;
		std::fstream	_body_file;
		size_t			_content_length;
		size_t			_bytes_received;

		std::string		_buffer;
		std::string		_body;

		BodyMode	_body_mode;
		ChunkState	_chunk_state;
		size_t		_chunk_size;

		size_t headers_size;
		std::string path_name;

		void	_parseRequestLine(const std::string &line);
		void	_parseHeaders(const std::string &header);
		bool	_parseContentLength(const std::string &value);
		void	_prepareBody(void);
		void	_processBody(void);
		void	_processChunked(void);
		bool	_parseChunkSize(const std::string &line);
		bool	_createTempFile(void);
		void	_closeBodyFile(void);
		// void	_extractLeftover();
};
