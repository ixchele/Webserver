#include <Multiplexer.hpp>

void Multiplexer::run_all_servers() {
  for (size_t i = 0; i < this->v_servers.size(); i++)
  {
    v_servers[i].run();
  }
}

Multiplexer::Multiplexer(const vector<ServerConfig*> &v_configs) {
  for (size_t i = 0; i < v_configs.size(); i++)
    {
        for (size_t j = 0; j < v_configs[i]->listen.size(); j++)
        {
            this->v_servers.push_back(Server(v_configs[i]->listen[j], v_configs[i]));
        }
    }
}

void Multiplexer::generate_servers(const vector<ServerConfig*> &v_configs) {
    for (size_t i = 0; i < v_configs.size(); i++)
    {
        for (size_t j = 0; j < v_configs[i]->listen.size(); j++)
        {
            this->v_servers.push_back(Server(v_configs[i]->listen[j], v_configs[i]));
        }
    }
}
