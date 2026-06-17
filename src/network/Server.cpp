#include <Server.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <stdexcept>

Server::Server()
	: m_fd(-1)
{
    std::memset(&this->m_addr, 0, sizeof(m_addr));
}

Server::Server(const std::string &ip, const short &port, const ServerConfig *config)
	: m_fd(-1)
{
    std::memset(&this->m_addr, 0, sizeof(m_addr));
    m_config = config;
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
    if (creat_socket() == -1)
    {
        std::string warning = "Waring: Couldn't create";
        throw std::runtime_error(" is not a valid ip address");
    }
    if (bind_address() == -1)
    {
        
    }
    if (start_listening() == -1)
    {
        
    }
}

int Server::creat_socket() {
    this->m_fd = socket(AF_INET, SOCK_STREAM, 0);
    return this->m_fd;
}

int Server::bind_address() {
    return bind(this->m_fd, reinterpret_cast<sockaddr *>(&this->m_addr), 16);
}

int Server::start_listening() {
    return listen(this->m_fd, BACKLOG);
}

Client Server::accept_connection() {
    int clientFd;

    clientFd = accept(this->m_fd, NULL, NULL);
    return Client(clientFd);
}

int Server::get_fd() {
    return this->m_fd;
}
