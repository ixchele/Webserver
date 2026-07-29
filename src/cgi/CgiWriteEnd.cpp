#include <CgiWriteEnd.hpp>

CgiWriteEnd::CgiWriteEnd(int fd)
  : AFd(fd, AFd::CGI_WRITE_END)
{
}

CgiWriteEnd::~CgiWriteEnd()
{
}

int CgiWriteEnd::handdle_event(uint32_t event) {

}
