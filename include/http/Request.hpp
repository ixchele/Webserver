#pragma once

#include <string>
#include <map>
#include <fstream>

class HttpRequest {
public:
    enum Method {
        GET,
        POST,
        DELETE,
        UNKNOWN
    };

    enum ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY,
        COMPLETE,
        ERROR
    };

    HttpRequest(int client_fd);
    ~HttpRequest();

    void parse(const std::string& raw_data);
    void clear();

    ParseState getState() const;
    Method getMethod() const;
    const std::string& getUri() const;
    const std::string& getVersion() const;
    const std::map<std::string, std::string>& getHeaders() const;
    const std::string& getTempFilename() const;
    size_t getContentLength() const;

private:
    int         _client_fd;
    ParseState  _state;

    Method      _method;
    std::string _uri;
    std::string _version;
    std::map<std::string, std::string> _headers;

    std::string   _temp_filename;
    std::ofstream _body_file;
    size_t        _content_length;
    size_t        _bytes_received;

    std::string _buffer;

    void _parseRequestLine();
    void _parseHeaders();
    void _extractLeftover();
};
