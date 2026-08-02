#include <CgiOutput.hpp>
#include <CgiInput.hpp>
#include <HttpRequest.hpp>
#include <vector>
#include <string>

class Cgi
{
public:
  CgiOutput *m_cgiOutput;
  CgiInput *m_cgiInput;

  Cgi(HttpRequest &request);
  ~Cgi();

  int get_pid(void);

  int execute();

private:
  HttpRequest &_request;
  pid_t _pid;
  int readPipe[2];
  int writePipe[2];
  std::vector <std::string> _env;
  std::vector <const char *> _cenv;
};
