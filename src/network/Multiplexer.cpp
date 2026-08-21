#include <Multiplexer.hpp>
#include <timeout.hpp>
#include <Logger.hpp>
#include <Epoll.hpp>
#include <sys/epoll.h>
#include <string.h>
#include <stdexcept>
#include <iostream>
#include <map>

Multiplexer::Multiplexer(const std::vector<ServerConfig> &v_configs)
{
    std::string key;
    for (size_t confs = 0; confs < v_configs.size(); confs++)
    {
        for (size_t hosts = 0; hosts < v_configs[confs].hosts.size(); hosts++)
        {
            for (size_t ports = 0; ports < v_configs[confs].listen.size(); ports++)
            {
                key = Server::craft_key(v_configs[confs].hosts[hosts], v_configs[confs].listen[ports]);
                LOG_DEBUG << "crafted key: " << key;

                ServersMap::iterator it = m_servers.find(key);
                if (it == m_servers.end())
                {
                    try
                    {
                        this->m_servers[key] =
                            new Server(key, v_configs[confs].hosts[hosts], v_configs[confs].listen[ports],
                                       &v_configs[confs], _epoll, _clientsList);
                        this->m_servers.at(key)->run();
                    }
                    catch (...)
                    {
                        _deleteServers();
                        throw;
                    }
                }
                else
                    it->second->m_configs.push_back(&v_configs[confs]);
            }
        }
    }
}

void Multiplexer::startup()
{
    if (m_servers.empty())
        throw std::runtime_error("Error: This Multiplexer's servers list is empty");
    ServersMap::iterator it;
    for (it = m_servers.begin(); it != this->m_servers.end(); ++it)
    {
        if (_epoll.add_fd(it->second->get_fd(), it->second, EPOLLIN))
            throw std::runtime_error("failed to add the server " + it->second->m_key +
                                     " in the epoll instance");
        LOG_INFO << "added " << it->second->m_key << " as " << it->second->get_fd();
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
        if (readyFds < 0)
            continue;

        for (int i = 0; i < readyFds; i++)
        {
            fdObj = static_cast<AFd *>(events[i].data.ptr);
            // if (events[i].events & EPOLLIN)
            //     LOG_DEBUG << "event " << "EPOLLIN" << " came on fd " << fdObj->get_fd();
            // else if (events[i].events & EPOLLOUT)
            //     LOG_DEBUG << "event " << "EPOLLOUT" << " came on fd " << fdObj->get_fd();
            // else
            //     LOG_DEBUG << "some event came on fd " << fdObj->get_fd();
            if (fdObj->handle_event(events[i].events) != Epoll::ECONTINUE)
            {
                if (fdObj->get_type() == AFd::CLIENT)
                {
                    Client *client = static_cast<Client *>(fdObj);
                    _clientsList.erase(client->m_it);
                }
                _epoll.del_fd(fdObj->get_fd());
                LOG_DEBUG << "Ended connection with the client on fd " << fdObj->get_fd();
                delete fdObj;
            }
            else if (fdObj->get_type() == AFd::CLIENT)
            {
                Client *client = static_cast<Client *>(fdObj);
                client->m_lastActivity = time(NULL);
                _clientsList.erase(client->m_it);
                _clientsList.push_back(client);
                client->m_it = --_clientsList.end();
            }
        }
        _handle_timeout();
    }
}

void Multiplexer::_handle_timeout()
{
    time_t now = time(NULL);
    time_t timeout;
    Client *client;
    while (true)
    {
        if (_clientsList.empty())
            return;
        client = _clientsList.front();
        if (client->m_state == Client::CKEEPT_ALIVE)
            timeout = KEEPTALIVE_TIMEOUT;
        else
            timeout = MAIN_TIMEOUT;
        if (now - client->m_lastActivity > timeout)
        {
            if (client->m_state == Client::CRECEVING ||
                client->m_state == Client::CEXECUTING_CGI)
            {
                client->handleTimeout();
                _clientsList.pop_front();
                client->m_lastActivity = time(NULL);
                _clientsList.push_back(client);
                client->m_it = --_clientsList.end();
            }
            else
            {
                _clientsList.pop_front();
                _epoll.del_fd(client->get_fd());
                LOG_INFO << "Client with fd " << client->get_fd() << " timed out";
                delete client;
            }
        }
        else
        {
            break;
        }
    }
}

void Multiplexer::_deleteServers()
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

void Multiplexer::_deleteClientsList()
{
    Client *client;

    while (!_clientsList.empty())
    {
        client = _clientsList.front();
        _epoll.del_fd(client->get_fd());
        delete client;
        _clientsList.pop_front();
    }
}

Multiplexer::~Multiplexer()
{
    _deleteClientsList();
    _deleteServers();
}
