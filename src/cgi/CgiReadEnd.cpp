#include <CgiReadEnd.hpp>

CgiReadEnd::CgiReadEnd(int fd)
  : AFd(fd)
{
}

CgiReadEnd::~CgiReadEnd()
{
}

int CgiReadEnd::handdle_event(uint32_t event) {

}
