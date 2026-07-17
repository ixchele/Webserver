#pragma once
# include <Request.hpp>
# include <fstream>

class HttpResponse
{
  public:
    HttpResponse(HttpRequest *request);
    ~HttpResponse();

    void response();

  private:
    HttpRequest *_request;
    std::ifstream _bodyFile;
};
