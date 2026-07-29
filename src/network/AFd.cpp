#include <AFd.hpp>
#include <unistd.h>

AFd::AFd(int fd, e_type type) : m_fd(fd), _type(type)
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

AFd::e_type AFd::get_type() const
{
    return this->_type;
}
