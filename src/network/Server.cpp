#include <Server.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>

Server::Server()
	: m_fd(-1)
{
    std::memset(&this->m_addr, 0, sizeof(m_addr));
}

Server::Server(const short &port, const ServerConfig *config)
	: m_fd(-1), m_config(config)
{
    std::memset(&this->m_addr, 0, sizeof(m_addr));
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

void Server::set_addr() {
	// addrinfo hints, *res;

    // std::memset(&hints, 0, sizeof(m_addr));
    // hints.ai_family = AF_INET;
    // hints.ai_socktype = SOCK_STREAM;
    
}

void Server::generate_servers(vector <Server> &v_servers, vector<ServerConfig*> &v_configs) {
    for (size_t i = 0; i < v_configs.size(); i++)
    {
        for (size_t j = 0; j < v_configs[i]->listen.size(); j++)
        {
            v_servers.push_back(Server(v_configs[i]->listen[j], v_configs[i]));
        }
    }
}
