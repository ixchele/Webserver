#ifndef CLIENT_HPP
# define CLIENT_HPP
#include <AFd.hpp>
#include <Epoll.hpp>
#include <Request.hpp>
#include <Server.hpp>

#define APP_BUFFER_SIZE 8192 // 8Kb

class Server;
class Request;

class Client : public AFd
{
  public:
    Server *m_server;
    // TODO : use server epoll
    Epoll *m_epoll;
    HttpRequest *m_requst;
    HttpResponse *m_response;

    Client(int fd, Server *server, Epoll *epoll);

    virtual void handdle_event(uint32_t event);

    virtual ~Client();

  private:
    sockaddr_in client_addr;

    vector<uint8_t> receive_data();
};

#endif
