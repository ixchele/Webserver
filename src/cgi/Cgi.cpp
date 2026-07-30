#include <Cgi.hpp>

Cgi::Cgi(HttpRequest &request)
  : _request(request)
{
}

Cgi::~Cgi()
{
}
