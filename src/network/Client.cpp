#include <Client.hpp>
#include <iostream>
#include <unistd.h>
#include <Server.hpp>

Client::~Client() {
  /*
    to do: before I close this connection, I must make sure
    that the client read everything, by checking the socket buffer.
  */
  close(m_fd);
}

Client::Client(const int &fd, Server *server) : AFd(fd), m_server(server) {}

void Client::handdle_event(uint32_t event) {
  // to do
  (void)event;
  write (m_fd, "Accepted\n", 9);
  m_server->end_connection(m_fd);
}
