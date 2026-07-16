#pragma once
# include <Request.hpp>
# include <fstream>

class Response
{
  public:
    Response(HttpRequest *request);
    ~Response();

  private:
    HttpRequest *_request;
    std::fstream _file;
};
