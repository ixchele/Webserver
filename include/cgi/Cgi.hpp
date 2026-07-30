#include <CgiReadEnd.hpp>
#include <CgiWriteEnd.hpp>
#include <Request.hpp>

class Cgi
{
public:
  CgiReadEnd *m_cgiReadEnd;
  CgiWriteEnd *m_cgiWriteEnd;

  Cgi(HttpRequest &request);
  ~Cgi();

private:
  HttpRequest &_request;
};
