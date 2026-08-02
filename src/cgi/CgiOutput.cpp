#include <CgiOutput.hpp>

CgiOutput::CgiOutput(int fd)
  : AFd(fd, AFd::CGI_READ_END)
{
}

CgiOutput::~CgiOutput()
{
}

int CgiOutput::handdle_event(uint32_t event) {

}
