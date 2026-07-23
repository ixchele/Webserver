#include <Multiplexer.hpp>
#include <Epoll.hpp>
#include <sys/epoll.h>
#include <string.h>
#include <stdexcept>
#include <iostream>
#include <map>

void Multiplexer::startup()
{
    ServersMap::iterator it;
    for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
    {
        if (_epoll.add_fd(it->second->get_fd(), it->second, EPOLLIN))
            throw std::runtime_error("failed to add the server " + it->second->m_key +
                                     " in the epoll instance");
        std::cout << "added " << it->second->m_key << " as " << it->second->get_fd() << std::endl;
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
        readyFds = _epoll.wait(events);
        if (readyFds == -1)
            
        for (int i = 0; i < readyFds; i++)
        {
            fdObj = static_cast<AFd *>(events[i].data.ptr);
            std::cerr << "event came on " << fdObj->get_fd() << std::endl;
            try
            {
                fdObj->handdle_event(events[i].events);
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << std::endl;
            }
        }
    }
}

Multiplexer::Multiplexer(const vector<ServerConfig *> &v_configs)
{
    std::string key;
    for (size_t confs = 0; confs < v_configs.size(); confs++)
    {
        for (size_t hosts = 0; hosts < v_configs[confs]->hosts.size(); hosts++)
        {
            for (size_t ports = 0; ports < v_configs[confs]->listen.size(); ports++)
            {
                key = Server::craft_key(v_configs[confs]->hosts[hosts], v_configs[confs]->listen[ports]);
                std::cout << "key: " << key << std::endl;

                ServersMap::iterator it = m_servers.find(key);
                if (it == m_servers.end())
                {
                    this->m_servers[key] = new Server(
                        key, 
                        v_configs[confs]->hosts[hosts],
                        v_configs[confs]->listen[ports],
                        v_configs[confs],
                        _epoll
                    );
                }
                else
                    it->second->m_configs.push_back(v_configs[confs]);
            }
        }
    }
}

Multiplexer::~Multiplexer()
{
    ServersMap::iterator it;
    for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
    {
        if (it->second != NULL)
        {
            _epoll.del_fd(it->second->get_fd());
            delete it->second;
        }
    }
}
