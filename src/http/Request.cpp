#include <Request.hpp>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

Request::Request(Client *client) : m_client(client)
{
}

Request::~Request()
{
}

int Request::receive_data() {
    int bytes = read(m_client->get_fd(), m_buffer, APP_BUFFER_SIZE - 1);
    if (bytes == -1 || bytes == 0)
    {
        std::cerr << "warning: read() failed with " << bytes << " in " << m_client->m_server->m_key << std::endl;
        return bytes;
    }
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
