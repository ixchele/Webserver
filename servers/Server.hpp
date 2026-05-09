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

#define M_GET 1
#define M_POST 2
#define M_DELETE 4

typedef char methods_t;

class Server
{
  public:
    int get_sockFd();
    void creat_socket();
    void bind_address();
    void start_listening();
    void accept_client();
    void run();

  private:
    struct Location
    {
        string root;
        methods_t methods;
        string index;
        bool autoindex;
    };

    int m_sockFd;
    short m_port;  // NOTE : same as listen
    int m_address; // NOTE : same as host
		struct sockaddr_in m_sockaddr;
    string m_serverName;
    int m_uploadLimit;
    vector <Location> m_locations;
};
