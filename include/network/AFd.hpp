#ifndef AFD_HPP
# define AFD_HPP
# include <Epoll.hpp>
# include <string>

class AFd
{
  public:
    enum Type {SERVER, CLIENT, CGI};
    AFd(int fd, Type type);

    int get_fd() const;
    Type get_type() const;

    virtual Epoll::EventState handle_event(uint32_t event) = 0;

    virtual ~AFd();

  protected:
    int m_fd;
    Type _type;
};

#endif
