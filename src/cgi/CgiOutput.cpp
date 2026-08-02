#include <CgiOutput.hpp>
#include <unistd.h>

CgiOutput::CgiOutput(int fd)
  : AFd(fd, AFd::CGI_READ_END)
{
}

CgiOutput::~CgiOutput()
{
  close(m_fd);
}

int CgiOutput::handle_event(uint32_t event) {
  (void)event;
}
