#include <Logger.hpp>
#include <Cgi.hpp>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cerrno>
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

  if (_body_fd != -1)
  {
    struct stat st;
    if (fstat(_body_fd, &st) == 0)
    {
      std::ostringstream oss;
      oss << st.st_size;
      _env.push_back("CONTENT_LENGTH=" + oss.str());
    }
  }
  std::string content_type = _request.getHeader("content-type");
  if (content_type != "")
    _env.push_back("CONTENT_TYPE=" + content_type);

  std::map<std::string, std::string>::const_iterator it;
  for (it = _request.getHeaders().begin(); it != _request.getHeaders().end(); ++it)
  {
    const std::string &nm = it->first;
    const std::string &vl = it->second;

    if (nm == "content-length" || nm == "content-type")
      continue;

    std::string env = "HTTP_";
    for (size_t i = 0; i < nm.size(); ++i)
    {
      env += (nm[i] == '-') ? '_' : static_cast<char>(::toupper(nm[i]));
    }
    _env.push_back(env + "=" + vl);
  }

  for (size_t i = 0; i < _env.size(); ++i)
    _cenv.push_back(_env[i].c_str());
  _cenv.push_back(NULL);
}

pid_t Cgi::getPid() const {
  return _pid;
}

int Cgi::execute() {
  if (pipe(_outputPipe) == -1)
  {
    LOG_ERROR << "pipe() -> " << strerror(errno);
    return -1;
  }
  if (fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK) == -1)
  {
    LOG_ERROR << "fcntl() -> " << strerror(errno);
    (void)close(_outputPipe[0]);
    (void)close(_outputPipe[1]);
    return -1;
  }
  _pid = fork();
  if (_pid == -1)
  {
    LOG_ERROR << "fork() -> " << strerror(errno);
    (void)close(_outputPipe[0]);
    (void)close(_outputPipe[1]);
    return -1;
  }

  if (_pid == 0)
  {
    (void)close(_outputPipe[0]);

    int input = (_body_fd != -1) ? _body_fd : open("/dev/null", O_RDONLY);
    if (input != -1)
    {
      (void)lseek(input, 0, SEEK_SET);
      (void)dup2(input, STDIN_FILENO);
      (void)close(input);
    }

    // I must remove this if I want to see the script errors
    int blackhole = open("/dev/null", O_WRONLY);
    if (blackhole != -1)
    {
      (void)dup2(blackhole, STDERR_FILENO);
      (void)close(blackhole);
    }

    std::string dir;
    size_t slash = _script_path.find_last_of('/');
    if (slash == std::string::npos)
      dir = ".";
    else if (slash == 0)
      dir = "/";
    else
      dir = _script_path.substr(0, slash);
    (void)chdir(dir.c_str());

    execve(_cargv[0], (char *const *)&_cargv[0], (char *const *)&_cenv[0]);
    LOG_ERROR << "execve(" << _interpreter << ") -> " << strerror(errno);
    _exit(127);
  }

  (void)close(_outputPipe[1]);

  if (_body_fd != -1)
  {
    (void)close(_body_fd);
    _body_fd = -1;
  }
  _start = time(NULL);
  return 0;
}

Cgi::e_out Cgi::readOutput() {
  char chunk[PIPE_BUFFER_SIZE];
  ssize_t bytes = read(_outputPipe[0], chunk, sizeof(chunk));
  if (bytes > 0)
  {
    _buffer.append(chunk, static_cast<size_t>(bytes));
    return MORE;
  }
  if (bytes == 0)
  {
    int res;
    do { res = waitpid(_pid, &_status, WNOHANG); }
    while (res == -1 && errno == EINTR);
    if (res == 0)
    {
      (void)kill(_pid, SIGKILL);
      while(waitpid(_pid, &_status, 0) == -1 && errno == EINTR) {}
    }
    _reaped = true;
    return DONE;
  }
  return FAIL;
}

void Cgi::killChild() {
  if (_pid > 0 && !_reaped)
  {
    (void)kill(_pid, SIGKILL);
    while(waitpid(_pid, &_status, 0) == -1 && errno == EINTR) {}
  }
}

bool Cgi::exitedCleanly() const {
  return 
  (
    _reaped &&
    WIFEXITED(_status) &&
    WEXITSTATUS(_status) == EXIT_SUCCESS
  );
}

const std::string &Cgi::getOutput() const {
  return _buffer;
}

int Cgi::getReadEnd() const {
  return _outputPipe[0];
}

Cgi::~Cgi()
{
  killChild();
}
