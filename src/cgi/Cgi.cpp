#include <Logger.hpp>
#include <Cgi.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <map>

Cgi::Cgi(HttpRequest &request, const std::string &interpreter,
      const std::string &script_path, int body_fd)
  : _request(request), _interpreter(interpreter),
    _script_path(script_path), _body_fd(-1), _status(0),
    _pid(-1), _reaped(false), _start(0)
{
  _setArgv();
  _setEnv();
}

void Cgi::_setArgv() {
  _cargv.push_back(_interpreter.c_str());
  _cargv.push_back(_script_path.c_str());
  _cargv.push_back(NULL);
}

void Cgi::_setEnv() {
  _env.push_back("GATEWAY_INTERFACE=CGI/1.1");
  _env.push_back(_request.getVersion());
  _env.push_back("SERVER_SOFTWARE=webserv/1.0");

  std::string host = _request.getHeader("host");
  std::string name;
  std::string port;
  size_t colon = host.find(':');
  if (colon != std::string::npos)
  {
    name = host.substr(0, colon);
    port = host.substr(colon + 1);
  }
  _env.push_back("SERVER_NAME=" + name);
  _env.push_back("SERVER_PORT=" + port);

  _env.push_back("REQUEST_METHOD=" + _request.getMethodStr());
  _env.push_back("REQUEST_URI=" + _request.getUri().getOriginal());
  _env.push_back("SCRIPT_NAME=" + _request.getUri().getPath());
  _env.push_back("SCRIPT_FAILENAME=" + _script_path);
  _env.push_back("QUERY_STRING=" + _request.getUri().getQuery());
}

pid_t Cgi::getPid() const {
  return _pid;
}

int Cgi::execute() {
 
}

Cgi::~Cgi()
{
}
