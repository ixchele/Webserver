#include <Response.hpp>
#include <Client.hpp>
#include <Server.hpp>
#include <Epoll.hpp>
#include <unistd.h>
#include <iostream>

Client::~Client()
{
  close(m_fd);
}

Client::Client(int fd, Server *server)
    : AFd(fd), m_server(server)
{

}

void Client::receive_data() {
    char buffer[APP_BUFFER_SIZE + 1];

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == -1 || bytes == 0)
    {
        std::cerr << "warning: read() failed with " << bytes << " in " << m_server->m_key << std::endl;
        return;
    }
    for (int i = 0; i < bytes; bytes++)
    {
      v_buffer.push_back(buffer[i]);
    }
}

void Client::handdle_event(uint32_t event)
{
  // to do
  if (event == EPOLLIN)
  {
    receive_data();
    m_requst.parse("vector");
  }
  else if (event == EPOLLOUT)
  {
    m_response.response();
  }
  else
    m_server->end_connection(m_fd);
}
