#pragma once
# include <sys/epoll.h>
# include <AFd.hpp>

class Epoll
{
public:
    Epoll();
    ~Epoll();

	int add_fd(int fd, AFd *ptr, int events);
	void del_fd(int fd);
    int wait(epoll_event *events, int maxevents);

private:
    int m_fd;
};
