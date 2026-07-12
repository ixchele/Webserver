#include <Request.hpp>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

Request::Request(Client *client)
    : m_step(ERequestLine), m_client(client)
{
}

Request::~Request()
{
}

int Request::receive_data() {
    int bytes = read(m_client->get_fd(), m_buffer, APP_BUFFER_SIZE);
    if (bytes == -1 || bytes == 0)
    {
        std::cerr << "warning: read() failed with " << bytes << " in " << m_client->m_server->m_key << std::endl;
        return bytes;
    }
    m_buffer[bytes] = '\0';
    m_sbuffer += m_buffer;
    return bytes;
}

void Request::extruction() {
    if (m_step == ERequestLine)
    {
        extruct_request_line();
    }
    if (m_step == EHeaders)
    {
        extruct_headers();
    }
    if (m_step == EBody)
    {
        extruct_bodies();
    }
}

void Request::extruct_request_line() {
    std::string requestLine;
    size_t pos;

    pos = m_sbuffer.find("\r\n");
    if (pos != std::string::npos)
    {
        requestLine = m_sbuffer.substr(0, pos + 1);
        std::cout << "Request line: " << requestLine << std::endl;

        m_step = EHeaders;
    }
}

void Request::extruct_headers() {

}

void Request::extruct_bodies() {

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
