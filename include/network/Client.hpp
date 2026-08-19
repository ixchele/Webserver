#ifndef CLIENT_HPP
# define CLIENT_HPP
#include <RequestHandler.hpp>
#include <ServerConfig.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <Epoll.hpp>
#include <AFd.hpp>
#include <Cgi.hpp>
#include <vector>
#include <ctime>
#include <list>

#define APP_BUFFER_SIZE 8192 // 8Kb


class Client : public AFd
{
  public:
    enum e_state {CKEEPT_ALIVE, CRECEVING, CSENDING_HEADERS, CSENDING_BODY, CEXECUTING_CGI, CFINISHED, CTIMEDOUT};

    time_t m_lastActivity;
    e_state m_state;
    std::vector<const ServerConfig *> &m_configs;
    std::list<Client *>::iterator m_it;

    Client(int fd, Epoll &epoll, std::vector<const ServerConfig *> &configs);

    virtual Epoll::EventState handle_event(uint32_t event);
    void handle_timeout();

    int startCgi(const std::string &interpreter,
                  const std::string &script_path, int body_fd);

    virtual ~Client();

  private:
    // sockaddr_in _client_addr;
    Epoll &_epoll;
    HttpRequest _request;
    // RequestHandler *_rqst_handler;
    HttpResponse _response;
    ssize_t _bytes_sent;
    off_t _file_offset;

    Cgi *_cgi;
    time_t _cgi_start;

    Epoll::EventState _receive_data();
    Epoll::EventState _send_data();
    Epoll::EventState _handle_cgi_event();
    const ServerConfig *_get_config(const std::string &host);

    void _reset();
};

#endif
