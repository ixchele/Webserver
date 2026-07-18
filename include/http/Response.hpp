#ifndef RESPONSE_HPP
#define RESPONSE_HPP
#include <HttpStatus.hpp>
#include <Request.hpp>
#include <Client.hpp>
#include <fstream>
#include <string>

class HttpResponse
{
  public:
    enum State {
        Building,
        Sending
    };

    HttpResponse(Client *client, HttpRequest *request);
    ~HttpResponse();

    void response();

  private:
    const Client *_client;
    HttpRequest *_request;
    State _state;
    int _bodyFile;

    std::string _get_code_message(HttpStatus::Code code);
    // void _get();
    // void _post();
    void _delete();
};

#endif
