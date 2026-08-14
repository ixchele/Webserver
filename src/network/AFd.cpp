#include <AFd.hpp>
#include <unistd.h>

AFd::AFd(int fd, Type type) : m_fd(fd), _type(type)
{
}

AFd::~AFd()
{
    if (m_fd > 2)
        close(m_fd);
}

int AFd::get_fd() const
{
    return this->m_fd;
}

AFd::Type AFd::get_type() const
{
    return this->_type;
}
