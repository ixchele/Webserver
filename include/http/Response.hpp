#ifndef RESPONSE_HPP
#define RESPONSE_HPP
#include <HttpStatus.hpp>
#include <Request.hpp>
#include <fstream>
#include <string>

class HttpResponse
{
  public:
    enum State {
        Building,
        SendingHeaders,
        SendingBody,
        Complete
    };

    HttpResponse(HttpRequest &request, int clientFd);
    ~HttpResponse();

    void response();

  private:
    HttpRequest &_request;
    int _clientFd;
    State _state;
    int _bodyFile;
    HttpStatus::Code _statusCode;
    std::string _buffer;
    size_t _bytesSent;

    std::string _get_code_message(HttpStatus::Code code);
    // void _get();
    // void _post();
    void _delete();
    void _build_headers();
    void _send_headers();
};

#endif
