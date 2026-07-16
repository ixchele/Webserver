#include <Client.hpp>
#include <Epoll.hpp>
#include <iostream>
#include <unistd.h>
#include <Server.hpp>

Client::~Client()
{
  close(m_fd);
  if (m_requst != NULL)
    delete m_requst;
}

Client::Client(int fd, Server *server, Epoll *epoll)
    : AFd(fd), m_server(server), m_epoll(epoll)
{
  m_requst = new Request(m_fd);
}

vector<uint8_t> Client::receive_data() {
    char buffer[APP_BUFFER_SIZE + 1];
    vector<uint8_t> v_buffer;

    size_t bytes = recv(m_fd, buffer, APP_BUFFER_SIZE, 0);
    if (bytes == -1 || bytes == 0)
    {
        std::cerr << "warning: read() failed with " << bytes << " in " << m_server->m_key << std::endl;
        return v_buffer;
    }
    for (int i = 0; i < bytes; bytes++)
    {
      v_buffer.push_back(buffer[i]);
    }
    return v_buffer;
}

void Client::handdle_event(uint32_t event)
{
  // to do
  if (event == EPOLLIN)
  {
    receive_data();
      
  }
  else if (event == EPOLLOUT)
  {
    write (m_fd, "All readed\n", 11);
    m_server->end_connection(m_fd);
  }
  else
    m_server->end_connection(m_fd);
}
