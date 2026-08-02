#include <CgiInput.hpp>

CgiInput::CgiInput(int fd)
  : AFd(fd, AFd::CGI_WRITE_END)
{
}

CgiInput::~CgiInput()
{
}

int CgiInput::handdle_event(uint32_t event) {

}
