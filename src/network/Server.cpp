#include <Server.hpp>
#include <Logger.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iostream>
#include <sstream>
#include <cstdlib>

Server::Server(const std::string &key, const std::string &ip, short port, 
    const ServerConfig *config, Epoll &epoll, 
    std::list<Client *> &clientsList)

	: AFd(-1, AFd::SERVER), m_key(key), 
    m_ip(ip), m_port(port), _epoll(epoll), 
    _clientsList(clientsList)
{
    m_configs.push_back(config);
    std::memset(&this->m_addr, 0, sizeof(m_addr));
    addrinfo hints, *res;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(ip.c_str(), NULL, &hints, &res) != 0)
    {
        throw std::runtime_error(ip + " is not a valid ip address");
    }
    else
    {
        this->m_addr = *((sockaddr_in *)res->ai_addr);
    }
    m_addr.sin_port = htons(port);
    run();
}

Server::~Server() {
    if (this->m_fd != -1)
        close(this->m_fd);
}

void Server::add_config(const ServerConfig *config) {
    m_configs.push_back(config);
}

void Server::run() {
    creat_socket();
    bind_address();
    start_listening();
}

void Server::creat_socket() {
    this->m_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (this->m_fd == -1)
	{
		throw std::runtime_error("error: socket() for " + m_key + " failed");
	}
}

// TODO : make code more readable
void Server::bind_address() {
    int opt = 1;
    if (setsockopt(this->m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
	{
        throw std::runtime_error("error: setsockopt() failed on " + m_key);
	}
    if (bind(this->m_fd, reinterpret_cast<sockaddr *>(&this->m_addr), 16) != 0)
	{
        throw std::runtime_error("error: bind() failed on " + m_key);
	}
}

void Server::start_listening() {
    if (listen(this->m_fd, SOMAXCONN) != 0)
	{
        throw std::runtime_error("error: listen() for " + m_key + " failed");
    }
}

// return 0 on success -1 if failed
int Server::accept_connection() {
    int clientFd;

	// TODO : catch client infos
    clientFd = accept4(this->m_fd, NULL, NULL, SOCK_CLOEXEC);
    
    return clientFd;
}

Epoll::EventState Server::handle_event(uint32_t event) {
    (void)event;
    int clientFd = accept_connection();
    if (clientFd == -1)
    {
        LOG_WARN << "accept4() failed on " << m_key;
        return Epoll::ECONTINUE;
    }
    LOG_INFO << "Accepted a client as fd " << clientFd;
    Client *client = new Client(clientFd, _epoll, m_configs);
    if (_epoll.add_fd(clientFd, static_cast<AFd *>(client), EPOLLIN) != 0)
    {
        LOG_WARN << "epoll_ctl() failed to add fd " << clientFd << " for " << m_key;
        _epoll.del_fd(clientFd);
        delete client;
        LOG_INFO << "Ended connection with client on fd " << clientFd;
    }
    _clientsList.push_back(client);
    client->m_it = --_clientsList.end();
    return Epoll::ECONTINUE;
}

string Server::craft_key(const string &ip, int port) {
	std::stringstream ssKey;
    addrinfo hints, *res;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(ip.c_str(), NULL, &hints, &res) != 0)
    {
        throw std::runtime_error(ip + " is not a valid ip address");
    }
    else
    {
        sockaddr_in addr;
        char buffer[16];

        addr = *((sockaddr_in *)res->ai_addr);
        if (inet_ntop(AF_INET, &addr.sin_addr, buffer, INET_ADDRSTRLEN) == NULL)
            throw std::runtime_error("error: inet_ntop() failed for " + ip);
        ssKey << &buffer[0];
        ssKey << ':';
		ssKey << port;
    }
    return ssKey.str();
}
