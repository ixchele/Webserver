#pragma once
# include <sys/epoll.h>
# include <AFd.hpp>

class Epoll
{
public:
    Epoll();
    ~Epoll();

	int add_fd(const int &fd, AFd *ptr, int events);
	void del_fd(const int &fd);

private:
    int m_fd;
};
