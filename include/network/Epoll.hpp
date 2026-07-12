#pragma once
# include <sys/epoll.h>
# include <AFd.hpp>

# define MAXEVENTS 64

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
