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
		// ~HttpRequest();

		void	parse(const std::string &raw_data);
		void	parse(const char *data, size_t len);
		// void	clear();
		// void	prepareBodyStorage(const std::string &filepath);

		ParseState			getState() const;
		HttpStatus::Code	getErrorCode() const;
		Method				getMethod() const;
		std::string		getMethodStr() const;
		const Uri			&getUri() const;
		const std::string	&getVersion() const;
		const std::string	&getTmpFilename() const;
		size_t				getContentLength() const;
		const std::string	&getBody() const;
		size_t				getBytesReceived() const;
		const std::map<std::string, std::string>	&getHeaders() const;
		std::string	getHeader(const std::string &name) const;

		bool isBufferEmpty();

		void	setState(ParseState state);
		void	setErrorCode(HttpStatus::Code code);

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

		Method		_method;
		Uri			_uri;
		std::string	_version;
		std::map<std::string, std::string>	_headers;

		std::string		_temp_filename;
		std::ofstream	_body_file;
		size_t			_content_length;
		size_t			_bytes_received;

		std::string		_buffer;
		std::string		_body;

		BodyMode	_body_mode;
		ChunkState	_chunk_state;
		size_t		_chunk_size;

		void	_parseRequestLine(const std::string &line);
		void	_parseHeaders(const std::string &header);
		bool	_parseContentLength(const std::string &value);
		void	_prepareBody(void);
		void	_processBody(void);
		void	_processChunked(void);
		bool	_parseChunkSize(const std::string &line);
		// void	_extractLeftover();
};
