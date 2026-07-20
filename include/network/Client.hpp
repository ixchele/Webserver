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
    enum e_state {RCEV, SEND};

    e_state m_state;
    Server *m_server;
    Epoll *m_epoll;
    HttpRequest m_requst;
    HttpResponse m_response;
    std::vector<uint8_t> v_buffer;
    vector<const ServerConfig *> &m_configs;

    Client(int fd, Server *server);

    virtual void handdle_event(uint32_t event);

    virtual ~Client();

  private:
    sockaddr_in _client_addr;

    void receive_data();
};

#endif
