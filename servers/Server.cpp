#include "Server.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

Server::Server()
	: m_sockFd(-1)
{
}

Server::Server(const ServerConfig& serverConfig)
	: m_sockFd(-1), m_serverConfig(serverConfig)
{
    
}

Server::~Server() {
    if (this->m_sockFd != -1)
        close(this->m_sockFd);
}

void Server::run() {
    creat_socket();
    bind_address();
    start_listening();
}

int Server::creat_socket() {
    this->m_sockFd = socket(AF_INET, SOCK_STREAM, 0);
    return this->m_sockFd;
}

int Server::bind_address() {
    return bind(m_sockFd, reinterpret_cast<sockaddr *>(), 16);
}

int Server::start_listening() {
    return listen(this->m_sockFd, BACKLOG);
}

int Server::get_sockFd() {
    return this->m_sockFd;
}

void Server::set_addr() {
	this->m_sockaddr = sockaddr;
}
