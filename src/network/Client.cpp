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

void Client::handle_event() {
  // 
}
