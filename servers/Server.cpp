#include "Server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>

void Server::run()
{
    creat_socket();
    bind_address();
    start_listening();
}

void Server::creat_socket()
{
	this->m_sockFd = socket(AF_INET, SOCK_STREAM, 0);
}

void Server::bind_address()
{
	bind(m_sockFd, reinterpret_cast<sockaddr*>(&this->m_sockaddr), 16);
}

void Server::start_listening()
{
	listen(this->m_sockFd, BACKLOG);
}

void Server::accept_client()
{
	int cfd = accept(this->m_sockFd, NULL, NULL);
}
