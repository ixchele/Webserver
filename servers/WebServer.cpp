#include "WebServer.hpp"

void WebServer::run() {
  for (size_t i = 0; i < this->m_servers.size(); i++)
  {
    m_servers[i].run();
  }
}

void WebServer::response_loop() {
  for (size_t i = 0; i < this->m_servers.size(); i++)
  {
    m_servers[i].accept_client();
  }
}