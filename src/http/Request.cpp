#include <Request.hpp>
#include <unistd.h>
#include <stdexcept>

Request::Request(Client *client) : m_client(client)
{
}

Request::~Request()
{
}

int Request::receive_data(int ClientFd) {
    int bytes = read(ClientFd, m_buffer, APP_BUFFER_SIZE - 1);
    if (bytes == -1)
        throw std::runtime_error("warning: read() failed");
    else if (bytes == 0)
        return 0;
    m_buffer[APP_BUFFER_SIZE - 1] = '\0';
    m_sbuffer += m_buffer;
    return bytes;
}
