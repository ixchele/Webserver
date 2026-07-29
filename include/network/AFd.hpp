#ifndef AFD_HPP
# define AFD_HPP
# include <ServerConfig.hpp>
# include <netinet/in.h>
# include <string>
# include <sys/socket.h>
# include <vector>
# include <exception>
# include <sys/epoll.h>

class AFd
{
  public:
    enum e_type {SERVER, CLIENT, CGI_READ_END, CGI_WRITE_END};
    AFd(int fd, e_type type);

    int get_fd() const;

    virtual int handle_event(uint32_t event) = 0;

    virtual ~AFd();

  protected:
    int m_fd;
    e_type _type;
};

#endif
