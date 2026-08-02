#include <CgiOutput.hpp>
#include <Logger.hpp>
#include <Cgi.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <map>

Cgi::Cgi(HttpRequest &request)
  : _request(request), _pid(-1), _status(0)
{
  std::map<std::string, std::string>::const_iterator HeadIt;
  std::string var;

  for (HeadIt = request.getHeaders().begin(); HeadIt != request.getHeaders().end(); ++HeadIt)
  {
    var.clear();
    std::string::const_iterator strIt;
    for (strIt = HeadIt->first.begin(); strIt != HeadIt->first.end(); ++strIt)
    {
      if (*strIt == '-')
        var += '_';
      else
        var += ::toupper(*strIt);
    }
    var += '=';
    for (strIt = HeadIt->second.begin(); strIt != HeadIt->second.end(); ++strIt)
    {
      var += *strIt;
    }
    _env.push_back(var);
    _cenv.push_back(var.c_str());
  }
  _cenv.push_back(NULL);
  pipe(_outputPipe);
  m_cgiOutput = new CgiOutput(_outputPipe[0]);
  if (request.getMethod() == HttpMethod::HTTP_POST)
  {
    pipe(_inputPipe);
    m_cgiInput = new CgiInput(_inputPipe[1]);
  }
}

Cgi::~Cgi()
{
  if (m_cgiInput != NULL)
    delete m_cgiInput;
  if (m_cgiOutput != NULL)
    delete m_cgiOutput;
}

int Cgi::get_pid(void) {
  return _pid;
}

int Cgi::execute() {
  _pid = fork();
  if (_pid == -1)
  {
    LOG_ERROR << "fork() -> " << strerror(errno);
    return -1;
  }

  if (_pid == 0)
  {
    delete m_cgiOutput;
    if (m_cgiInput)
      delete m_cgiInput;
    dup2(_outputPipe[1], 1);
    if (m_cgiInput != NULL)
      dup2(m_cgiInput->get_fd(), 0);
    if (execve(_request.getUri().getPath().c_str(), (char **)(&_cargv[0]), (char **)(&_cenv[0])) == -1)
    {
      LOG_ERROR << "execve() " << strerror(errno);
      exit(1);
    }
  }
  close (_outputPipe[1]);
  if (m_cgiInput)
    close (_inputPipe[0]);
}

int Cgi::waiter() {
  if (_pid == -1)
  {
    LOG_WARN << "Cgi::waiter() called while _pid = -1";
  }
  int ret = waitpid(_pid, &_status, WNOHANG);
  if (ret == -1)
  {
    LOG_ERROR << "waitpid() -> " << strerror(errno);
    return -1;
  }
  else if (ret == 0)
  {
    return 0;
  }
  else
  {
    return ret;
  }
}
