#include <HttpRequest.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <vector>
#include <string>

#define PIPE_BUFFER_SIZE 65536 // 64 Kb

class Cgi : public AFd
{
public:
  enum e_state {BUILDING, EXECUTED, FINISHED, ERROR};

  Cgi(HttpRequest &request);
  ~Cgi();

  int get_pid(void);

  int execute();
  int waiter();

  virtual Epoll::EventState handle_event(uint32_t event);

private:
  HttpRequest &_request;
  pid_t _pid;
  int _status;
  int _outputPipe[2];
  std::string _buffer;
  std::vector <std::string> _env;
  std::vector <const char *> _cenv;
  std::vector <const char *> _cargv;

  void _set_env();
};
