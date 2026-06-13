#include <Client.hpp>
#include <iostream>
#include <unistd.h>

Client::Client() : m_fd(-1) {}

Client::Client(const int &fd) : m_fd(fd) {}

Client::~Client() {
  if (this->m_fd != -1)
  {
    /*
      to do: before I close this connection, I must make sure
      that the client read everything, by checking the socket buffer.
    */
    close(m_fd);
  }
}