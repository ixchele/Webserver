#ifndef CLIENT_HPP
# define CLIENT_HPP
#include <ServerConfig.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <vector>
#include <ctime>
#include <list>

#define APP_BUFFER_SIZE 8192 // 8Kb


class Client : public AFd
{
  public:
    enum e_state {IDLE, RECEVING, SENDING, TIMEDOUT};

    time_t m_lastActivity;
    e_state m_state;
    std::string m_buffer;
    std::vector<const ServerConfig *> &m_configs;
    std::list<Client *>::iterator m_it;

    Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs);

    virtual int handle_event(uint32_t event);
    void handle_timeout();

    virtual ~Client();

  private:
    // sockaddr_in _client_addr;
    Epoll &_epoll;
    HttpRequest _request;
    std::string _config;
    // HttpResponse _response;

    Epoll::EventState _receive_data();
    // Epoll::EventState _send_data();
    const ServerConfig *_get_config(const std::string &host);
};

#endif
