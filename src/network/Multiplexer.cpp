#include <Multiplexer.hpp>
#include <Epoll.hpp>
#include <sys/epoll.h>
#include <iostream>


void Multiplexer::startup() {
	Epoll epoll;
	for (size_t i = 0; i < this->v_servers.size(); i++)
  {
    	epoll.add_fd(v_servers[i]->get_fd(), v_servers[i], EPOLLIN);
		  std::cout << "added " << v_servers[i]->get_fd() << std::endl;
	}
}

Multiplexer::Multiplexer(const vector<ServerConfig*> &v_configs) {
  for (size_t confs = 0; confs < v_configs.size(); confs++)
    {
      for (size_t hosts = 0; hosts < v_configs[confs]->hosts.size(); hosts++)
      {
        for (size_t ports = 0; ports < v_configs[confs]->listen.size(); ports++)
        {
          this->v_servers.push_back(new Server(v_configs[confs]->hosts[hosts], v_configs[confs]->listen[ports], v_configs[confs]));
        }
      }
    }
}

Multiplexer::~Multiplexer() {
  for (size_t i = 0; i < this->v_servers.size(); i++)
  {
    if (v_servers[i])
    	delete v_servers[i];
	}
}
