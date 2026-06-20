#include <Multiplexer.hpp>

void Multiplexer::startup() {
  for (size_t i = 0; i < this->v_servers.size(); i++)
  {
    v_servers[i]->run();
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
