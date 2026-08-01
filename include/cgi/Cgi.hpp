#include <CgiReadEnd.hpp>
#include <CgiWriteEnd.hpp>
#include <HttpRequest.hpp>
#include <vector>
#include <string>

class Cgi
{
public:
  CgiReadEnd *m_cgiReadEnd;
  CgiWriteEnd *m_cgiWriteEnd;

  Cgi(HttpRequest &request);
  ~Cgi();

  int get_pid(void);
  

private:
  HttpRequest &_request;
  pid_t _pid;
  std::vector <std::string> _env;
  std::vector <const char *> _cenv;
};
