#include <Cgi.hpp>
#include <algorithm>
#include <string>
#include <map>

Cgi::Cgi(HttpRequest &request)
  : _request(request)
{
  std::map<std::string, std::string>::const_iterator HeadIt;
  std::string var;

  for (HeadIt = request.getHeaders().begin(); HeadIt != request.getHeaders().end(); ++HeadIt)
  {
    var.clear();
    std::string::const_iterator strIt;
    for (strIt = HeadIt->first.begin(); strIt != HeadIt->first.end(); ++strIt)
    {
      var += *strIt;
    }
    var += '=';
    for (strIt = HeadIt->second.begin(); strIt != HeadIt->second.end(); ++strIt)
    {
      var += *strIt;
    }
    _env.push_back(var);
    _cenv.push_back(var.c_str());
  }
}

Cgi::~Cgi()
{
}

int Cgi::get_pid(void) {
  return _pid;
}
