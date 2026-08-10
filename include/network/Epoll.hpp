#ifndef EPOLL_HPP
# define EPOLL_HPP
# include <sys/epoll.h>

# define MAXEVENTS 64

class AFd;

class Epoll
{
public:
    enum EventState {
        ECONTINUE = 0,
        EFINISHED = 1,
        EERROR = 2
    };
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
