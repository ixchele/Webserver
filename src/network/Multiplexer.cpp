#include <Multiplexer.hpp>
#include <Epoll.hpp>
#include <sys/epoll.h>
#include <iostream>
#include <map>

void Multiplexer::startup()
{
  std::map<std::string, Server *>::iterator it;
  for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
  {
    m_epoll->add_fd(it->second->get_fd(), it->second, EPOLLIN);
    std::cout << "added " << it->second->get_fd() << std::endl;
  }
  events_loop();
}

void Multiplexer::events_loop()
{
  int readyFds;
  AFd *fdObj;

  while (true)
  {
    epoll_event events[MAXEVENTS];
    readyFds = m_epoll->wait(events);
    for (int i = 0; i < readyFds; i++)
    {
      fdObj = static_cast<AFd *>(events[i].data.ptr);
      std::cerr << "event came on " << fdObj->get_fd() << std::endl;
      fdObj->handdle_event(events[i].events);
    }
  }
}

Multiplexer::Multiplexer(const vector<ServerConfig *> &v_configs)
{
  std::string key;
  m_epoll = new Epoll;
  for (size_t confs = 0; confs < v_configs.size(); confs++)
  {
    for (size_t hosts = 0; hosts < v_configs[confs]->hosts.size(); hosts++)
    {
      for (size_t ports = 0; ports < v_configs[confs]->listen.size(); ports++)
      {
        key = Server::craft_key(v_configs[confs]->hosts[hosts], v_configs[confs]->listen[ports]);
        std::cout << "key: " << key << std::endl;
        if (m_servers.find(key) == m_servers.end())
          this->m_servers[key] = new Server(key, v_configs[confs]->hosts[hosts], v_configs[confs]->listen[ports], v_configs[confs], m_epoll);
        else
          this->m_servers[key]->m_configs.push_back(v_configs[confs]);
      }
    }
  }
}

Multiplexer::~Multiplexer()
{
  std::map<std::string, Server *>::iterator it;
  for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
  {
    if (it->second)
      delete it->second;
  }
  if (m_epoll)
    delete m_epoll;
}
