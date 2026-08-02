#include <CgiInput.hpp>
#include <unistd.h>

CgiInput::CgiInput(int fd)
  : AFd(fd, AFd::CGI_WRITE_END)
{
}

CgiInput::~CgiInput()
{
  close(m_fd);
}

int CgiInput::handle_event(uint32_t event) {
  (void)event;
}
