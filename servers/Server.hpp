#pragma once
#include <string>
#include <sys/socket.h>
#include <vector>
#include <netinet/in.h>

using std::string;
using std::vector;

// each server in config file will be inistiated from this class
//
//

#define BACKLOG 5
#define MAX_CLIENTS 1024

#define M_GET 1
#define M_POST 2
#define M_DELETE 4

typedef char methods_t;

class Server
{
  public:
		Server();

    int get_sockFd();
    
    void set_sockaddr(sockaddr_in &sockaddr);

    void creat_socket();
    void bind_address();
    void start_listening();
    void accept_client();
    void response();
    void close_connection();
    void run();
		//~Server();

  private:
    struct Location
    {
        string root;
        methods_t methods;
        string index;
        bool autoindex;
    };

    int m_sockFd;
    // short m_port;  // NOTE : same as listen
    // int m_address; // NOTE : same as host
		sockaddr_in m_sockaddr;
    string m_serverName;
    // int m_uploadLimit;
    vector <Location> m_locations;
		int m_clientsList[MAX_CLIENTS];
		int m_numberOfClients;
};
