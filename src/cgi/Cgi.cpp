#include <Logger.hpp>
#include <Cgi.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <map>

Cgi::Cgi(HttpRequest &request)
  : AFd(-1, AFd::CGI), _request(request), _pid(-1), _status(0)
{
  _set_env();
  if (pipe(_outputPipe) == -1)
  {
    LOG_ERROR << "pipe() -> " << strerror(errno);
  }
  if (request.getMethod() == HttpMethod::HTTP_POST)
  {
    
  }
}

Cgi::~Cgi()
{
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
    dup2(_outputPipe[1], 1);
    if (execve(_request.getUri().getPath().c_str(), (char **)(&_cargv[0]), environ) == -1)
    {
      LOG_ERROR << "execve() " << strerror(errno);
      exit(1);
    }
  }
  close (_outputPipe[1]);
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

Epoll::EventState Cgi::handle_event(uint32_t event) {
  if (event & EPOLLIN)
  {
    char cbuffer[PIPE_BUFFER_SIZE + 1];
    int bytes = recv(m_fd, cbuffer,PIPE_BUFFER_SIZE, 0);
    if (bytes == 0)
    {
      LOG_WARN << "recv() from fd " << m_fd << " returnred " << bytes;
      return Epoll::EVENT_ERROR;
    }
    else if (bytes == -1)
    {
      LOG_ERROR << "recv() from fd " << m_fd << " returnred " << bytes;
      return Epoll::EVENT_ERROR;
    }
    _buffer += cbuffer;
    LOG_DEBUG << "Cgi with fd " << m_fd << " wrote:\n" << _buffer;
  }
  return Epoll::EVENT_FINISHED;
}

void Cgi::_set_env() {
  std::map<std::string, std::string>::const_iterator HeadIt;
  std::string var;

  for (HeadIt = _request.getHeaders().begin(); HeadIt != _request.getHeaders().end(); ++HeadIt)
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
}
