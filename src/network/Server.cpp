#include <Server.hpp>
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

Server::Server(const std::string &key, const std::string &ip, short port, const ServerConfig *config, Epoll &epoll)
	: AFd(-1), m_key(key), m_ip(ip), m_port(port), _epoll(epoll)
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
    std::cerr << m_fd << std::endl;
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
    // if (clientFd != -1)
    //     m_clients.insert(std::make_pair(clientFd, Client(clientFd, this->_epoll, m_configs)));
    return clientFd;
}

// void Server::end_connection(int fd) {
//     if (m_clients.find(fd) != m_clients.end())
//     {
//         _epoll.del_fd(fd);
//         m_clients.erase(fd);
//     }
// }

int Server::handdle_event(uint32_t event) {
    (void)event;
    int clientFd = accept_connection();
    if (clientFd == -1)
    {
        std::cerr << "warning: accept4() failed on " << m_key << std::endl;
        return EVENT_CONTINUE;
    }
    // m_clients[clientFd] = new Client(clientFd, this, _epoll);
    std::cout << "Accepted " << clientFd << std::endl;
    Client *client = new Client(clientFd, _epoll, m_configs);
    if (_epoll.add_fd(clientFd, static_cast<AFd *>(client), EPOLLIN) != 0)
    {
        std::cerr << "warning: epoll_ctl() failed to add fd " << clientFd << " for " << m_key << std::endl;
        _epoll.del_fd(clientFd);
        delete client;
        std::cerr << "Ended connection with " << clientFd << std::endl;
    }
    return EVENT_CONTINUE;
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
