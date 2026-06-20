#include <Server.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <iostream>

Server::Server(const std::string &ip, const short &port, const ServerConfig *config)
	: AFd(-1), m_currentClient(NULL), m_ip(ip), m_port(port), m_config(config)
{
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
}

Server::~Server() {
    if (this->m_fd != -1)
        close(this->m_fd);
}

void Server::run() {
    creat_socket();
    bind_address();
    start_listening();
}

void Server::creat_socket() {
    this->m_fd = socket(AF_INET, SOCK_STREAM, 0);
        throw("error: socket() for " + m_ip + ":" + std::to_string(m_port) + " failed");
}

void Server::bind_address() {
    if (bind(this->m_fd, reinterpret_cast<sockaddr *>(&this->m_addr), 16) != 0)
        throw("error: bind() failed on " + m_ip + ":" + std::to_string(m_port));
}

void Server::start_listening() {
    if (listen(this->m_fd, BACKLOG) != 0)
        throw("error: listen() failed on " + m_ip + ":" + std::to_string(m_port));
}

// return 0 on success -1 if failed
void Server::accept_connection() {
    int clientFd;

    clientFd = accept(this->m_fd, NULL, NULL);
    if (clientFd != -1)
        m_currentClient = new Client(clientFd, this);
}

void Server::handdle_event(uint32_t event = EPOLLIN) {
    accept_connection();
    if (m_currentClient == NULL)
    {
        std::cerr << "warning: accept() failed on " + m_ip + ":" + std::to_string(m_port) << std::endl;
        return ;
    }
    // add client to epoll
}
