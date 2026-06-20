#include <Client.hpp>
#include <iostream>
#include <unistd.h>

Client::~Client() {
  /*
    to do: before I close this connection, I must make sure
    that the client read everything, by checking the socket buffer.
  */
  close(m_fd);
}

Client::Client(const int &fd, const Server *server) : AFd(fd), m_server(server) {}

void Client::handdle_event(uint32_t event = EPOLLIN) {
  // to do
}
