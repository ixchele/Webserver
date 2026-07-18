#ifndef RESPONSE_HPP
# define RESPONSE_HPP
# include <Request.hpp>
# include <Client.hpp>
# include <fstream>
# include <string>

class HttpResponse
{
  public:
    HttpResponse(Client *client, HttpRequest *request);
    ~HttpResponse();

    void response();

  private:
    const Client *_client;
    HttpRequest *_request;
    std::ifstream _bodyFile;

    std::string _get_error_message(int code);
};

#endif
