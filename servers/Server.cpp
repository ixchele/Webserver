#include "Server.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

Server::Server()
	: m_sockFd(-1), m_numberOfClients(0)
{
}

void Server::run() {
    creat_socket();
    bind_address();
    start_listening();
}

void Server::creat_socket() {
    this->m_sockFd = socket(AF_INET, SOCK_STREAM, 0);
}

void Server::bind_address() {
    bind(m_sockFd, reinterpret_cast<sockaddr *>(&this->m_sockaddr), 16);
}

void Server::start_listening() {
    listen(this->m_sockFd, BACKLOG);
}

void Server::accept_client() {
    int cfd = accept(this->m_sockFd, NULL, NULL);
	this->m_clientsList[this->m_numberOfClients++] = cfd;
}

void Server::response() {
    write(this->m_clientsList[this->m_numberOfClients - 1], "Accepted\n", 9);
}

void Server::close_connection() {
    close(this->m_clientsList[this->m_numberOfClients - 1]);
}

int Server::get_sockFd() {
    return this->m_sockFd;
}

void Server::set_sockaddr(sockaddr_in &sockaddr) {
	this->m_sockaddr = sockaddr;
}
