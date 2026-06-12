#include "Socket.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

Socket::Socket()
	: m_sockFd(-1)
{
}

Socket::Socket(const int &addr, const short &port, const ServerConfig *&config)
	: m_sockFd(-1)
{
}

Socket::~Socket() {
    if (this->m_sockFd != -1)
        close(this->m_sockFd);
}

void Socket::run() {
    creat_socket();
    bind_address();
    start_listening();
}

int Socket::creat_socket() {
    this->m_sockFd = socket(AF_INET, SOCK_STREAM, 0);
    return this->m_sockFd;
}

int Socket::bind_address() {
    return bind(m_sockFd, reinterpret_cast<sockaddr *>(), 16);
}

int Socket::start_listening() {
    return listen(this->m_sockFd, BACKLOG);
}

int Socket::get_sockFd() {
    return this->m_sockFd;
}

void Socket::set_addr() {
	this->m_sockaddr = sockaddr;
}
