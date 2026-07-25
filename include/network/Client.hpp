#ifndef CLIENT_HPP
# define CLIENT_HPP
#include <ServerConfig.hpp>
#include <Request.hpp>
// #include <Response.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <vector>

#define APP_BUFFER_SIZE 8192 // 8Kb


class Client : public AFd
{
  public:
    enum e_state {RCEVING, SENDING};

    e_state m_state;
    std::string m_buffer;
    std::vector<const ServerConfig *> &m_configs;

    Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs);

    virtual int handdle_event(uint32_t event);

    virtual ~Client();

  private:
    // sockaddr_in _client_addr;
    Epoll &_epoll;
    HttpRequest _request;
    std::string _config;
    // HttpResponse _response;

    int _receive_data();
    const ServerConfig *_get_config(const std::string &host);
};

#endif
