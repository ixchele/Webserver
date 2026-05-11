#include <iostream>
#include <cstring>
#include "servers/Server.hpp"
#include "servers/WebServer.hpp"

int main()
{
    vector<Server> servers;
    Server server;
    sockaddr_in sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(9090);
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    server.set_sockaddr(sockaddr);
    server.run();
    server.accept_client();
    server.response();
    server.close_connection();
    std::cout << "webserv!" << std::endl;
    return 0;
}
