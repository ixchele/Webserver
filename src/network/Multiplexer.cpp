#include <Multiplexer.hpp>

void Multiplexer::run() {
  for (size_t i = 0; i < this->m_servers.size(); i++)
  {
    m_servers[i].run();
  }
}
