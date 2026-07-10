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

Server::Server(const std::string &ip, short port, const ServerConfig *config, Epoll *epoll)
	: AFd(-1), m_ip(ip), m_port(port), m_epoll(epoll)
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
        std::stringstream ss;
        char buffer[16];

        this->m_addr = *((sockaddr_in *)res->ai_addr);
	    ss << port;
        if (inet_ntop(AF_INET, &this->m_addr.sin_addr, buffer, INET_ADDRSTRLEN) == NULL)
            throw std::runtime_error("error: inet_ntop() failed on " + ip);
        m_key = &buffer[0];
        m_key += ':';
        m_key += ss.str();
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

const ServerConfig *Server::get_config(const string &host) const {
    for (size_t i = 0; i < m_configs.size(); i++)
    {
        for (size_t n = 0; n < m_configs[i]->names.size(); i++)
        {
            if (m_configs[i]->names[n] == host)
            {
                return m_configs[i];
            }
        }
    }
    throw std::runtime_error("Host name \"" + host + "\" is not found");
    return m_configs[0];
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
    int opt = 1;
    if (setsockopt(this->m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
	{
        std::stringstream ss;
        ss << m_port;
        throw std::runtime_error("error: setsockopt() failed on " + m_ip + ":" + ss.str());
	}
    if (bind(this->m_fd, reinterpret_cast<sockaddr *>(&this->m_addr), 16) != 0)
	{
        std::stringstream ss;
        ss << m_port;
        throw std::runtime_error("error: bind() failed on " + m_ip + ":" + ss.str());
	}
}

void Server::start_listening() {
    if (listen(this->m_fd, SOMAXCONN) != 0)
	{
        std::stringstream ss;
        ss << m_port;
        throw std::runtime_error("error: listen() for " + m_ip + ":" + ss.str() + " failed");
    }
}

// return 0 on success -1 if failed
int Server::accept_connection() {
    int clientFd;

    clientFd = accept4(this->m_fd, NULL, NULL, SOCK_CLOEXEC);
    if (clientFd != -1)
        m_clients[clientFd] = new Client(clientFd, this, this->m_epoll);
    return clientFd;
}

void Server::end_connection(int fd) {
    if (m_clients.find(fd) != m_clients.end() && m_clients[fd] != NULL)
    {
        m_epoll->del_fd(fd);
        delete m_clients[fd];
        m_clients.erase(fd);
    }
}

void Server::handdle_event(uint32_t event) {
    (void)event;
    int clientFd = accept_connection();
    if (clientFd == -1)
    {
        std::cerr << "warning: accept4() failed on " << m_ip << ":" << m_port << std::endl;
        return ;
    }
    m_clients[clientFd] = new Client(clientFd, this, m_epoll);
    std::cout << "Accepted " << m_clients[clientFd]->get_fd() << std::endl;
    if (m_epoll->add_fd(clientFd, static_cast<AFd *>(m_clients[clientFd]), EPOLLIN) != 0)
    {
        std::cerr << "warning: epoll_ctl() failed to add fd " << clientFd << std::endl;
        end_connection(clientFd);
    }
}

string Server::craft_key(const string &ip, int port) {
    std::string key;
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
        std::stringstream ss;
        sockaddr_in addr;
        char buffer[16];

        addr = *((sockaddr_in *)res->ai_addr);
	    ss << port;
        if (inet_ntop(AF_INET, &addr.sin_addr, buffer, INET_ADDRSTRLEN) == NULL)
            throw std::runtime_error("error: inet_ntop() failed on " + ip);
        key = &buffer[0];
        key += ':';
        key += ss.str();
    }
    return key;
}
