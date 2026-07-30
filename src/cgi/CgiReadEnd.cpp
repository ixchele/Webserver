#include <CgiReadEnd.hpp>

CgiReadEnd::CgiReadEnd(int fd)
  : AFd(fd, AFd::CGI_READ_END)
{
}

CgiReadEnd::~CgiReadEnd()
{
}

int CgiReadEnd::handdle_event(uint32_t event) {

}
