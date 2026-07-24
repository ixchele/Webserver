#ifndef EPOLL_HPP
# define EPOLL_HPP
# include <sys/epoll.h>
# include <AFd.hpp>

# define MAXEVENTS 64
# define EVENT_CONTINUE 0
# define EVENT_FINISHED 1
# define EVENT_ERROR 2

class Epoll
{
public:
    Epoll();
    ~Epoll();

	int add_fd(int fd, AFd *ptr, int events);
	int edit_fd(int fd, AFd *ptr, int events);
	void del_fd(int fd);
    int wait(epoll_event *events);

private:
    int m_fd;
};

#endif
