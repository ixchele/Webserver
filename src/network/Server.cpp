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
#include <sstream>

Server::Server(const std::string &ip, const short &port, const ServerConfig *config)
	: AFd(-1), m_config(config), m_currentClient(NULL), m_ip(ip), m_port(port)
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
    run();
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
	if (this->m_fd == -1)
	{
		std::stringstream ss;
		ss << m_port;
		throw std::runtime_error("error: socket() for " + m_ip + ":" + ss.str() + " failed");
	}
}

void Server::bind_address() {
    if (bind(this->m_fd, reinterpret_cast<sockaddr *>(&this->m_addr), 16) != 0)
	{
        std::stringstream ss;
        ss << m_port;
        throw std::runtime_error("error: bind() failed on " + m_ip + ":" + ss.str());
	}
}

void Server::start_listening() {
    if (listen(this->m_fd, BACKLOG) != 0)
	{
        std::stringstream ss;
        ss << m_port;
        throw std::runtime_error("error: listen() for " + m_ip + ":" + ss.str() + " failed");
    }
}

// return 0 on success -1 if failed
void Server::accept_connection() {
    int clientFd;

    clientFd = accept4(this->m_fd, NULL, NULL, SOCK_CLOEXEC);
    if (clientFd != -1)
        m_currentClient = new Client(clientFd, this);
}

void Server::end_connection() {
    if (this->m_currentClient)
    {
        delete m_currentClient;
        m_currentClient = NULL;
    }
}

void Server::handdle_event(uint32_t event) {
    accept_connection();
    if (m_currentClient == NULL)
    {
        std::cerr << "warning: accept() failed on " << m_ip << ":" << m_port << std::endl;
        return ;
    }
    // add client to epoll
    write(m_currentClient->get_fd(), "Accepted\n", 9);
    (void)event;
    
}
