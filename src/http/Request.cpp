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

Request::Line::Line() : method(0) {}

void Request::Line::clear() {
    this->method = 0;
    this->uri.clear();
    this->version.clear();
}

void Request::clear() {
    this->m_bodies.clear();
    this->m_chunks.clear();
    this->m_headers.clear();
    this->m_sbuffer.clear();
    this->m_requestLine.clear();
}
