#ifndef CLIENT_HPP
# define CLIENT_HPP
#include <ServerConfig.hpp>
#include <Request.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <vector>

#define APP_BUFFER_SIZE 8192 // 8Kb

class Client : public AFd
{
  public:
    enum e_state {RCEVING, SENDING};

    e_state m_state;
    // HttpRequest m_requst;
    // HttpResponse m_response;
    std::string m_buffer;
    std::vector<const ServerConfig *> &m_configs;

    Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs);

    virtual void handdle_event(uint32_t event);

    const ServerConfig *get_config(const std::string &host) const;
  
    void end_connection();

    virtual ~Client();

  private:
    // sockaddr_in _client_addr;
    Epoll &_epoll;

    void _receive_data();
};

#endif
