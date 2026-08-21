#ifndef CGI_HPP
#define CGI_HPP

#include <HttpRequest.hpp>
#include <vector>
#include <string>
#include <ctime>

#define PIPE_BUFFER_SIZE 65536 // 64 Kb

class Cgi
{
public:
  enum e_out
  {
    MORE,
    DONE,
    FAIL
  };

  Cgi(HttpRequest &request, const std::string &interpreter,
      const std::string &script_path, int body_fd);
  ~Cgi();

  int execute();
  e_out readOutput();
  void killChild();

  int getReadEnd() const;
  int getOutputFd() const;
  const std::string &getOutputPath() const;
  pid_t getPid() const;
  bool exitedCleanly() const;

private:
  HttpRequest &_request;
  std::string _interpreter;
  std::string _script_path;
  int _body_fd;

  int _notify[2];
  int _output_fd;
  std::string _output_path;
  int _status;
  pid_t _pid;
  bool _reaped;
  time_t _start;

  std::vector<std::string> _env;
  std::vector<const char *> _cenv;
  std::vector<const char *> _cargv;

  void _setArgv();
  void _setEnv();
};

#endif
