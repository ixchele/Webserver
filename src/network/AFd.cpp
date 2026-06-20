#include <AFd.hpp>
#include <unistd.h>

AFd::AFd(const int &fd) : m_fd(fd)
{
}

AFd::~AFd()
{
    close(m_fd);
}

int AFd::get_fd() const
{
    return this->m_fd;
}
