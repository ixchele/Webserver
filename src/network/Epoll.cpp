#include <Epoll.hpp>
#include <sys/epoll.h>
#include <stdexcept>
#include <iostream>
#include <string>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sstream>

Epoll::Epoll() {
	m_fd = epoll_create1(EPOLL_CLOEXEC);
	if (m_fd == -1)
		throw std::runtime_error("error: epoll_create1() failed");
}

Epoll::~Epoll() {
	if (m_fd != -1)
		close(m_fd);
}

int Epoll::add_fd(int fd, AFd *ptr, int events) {
	epoll_event ev;
	int ret;

	ev.data.ptr = ptr;
	ev.events = events;
	ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &ev);

	if(ret != 0)
		perror("Error from epoll_ctl()");
	return ret;
}

int Epoll::edit_fd(int fd, AFd *ptr, int events) {
	epoll_event ev;

	ev.data.ptr = ptr;
	ev.events = events;
	return epoll_ctl(m_fd, EPOLL_CTL_MOD, fd, &ev);
}

void Epoll::del_fd(int fd) {
	if (epoll_ctl(m_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
		std::cerr << "warning: epoll_ctl() failed to delete fd " << fd << std::endl;
}

int	Epoll::wait(epoll_event *events) {
	int readyFds = epoll_wait(m_fd, events, MAXEVENTS, 1000);
	if (readyFds == -1)
		std::cerr << "warning: epoll_wait() failed" << std::endl;
	return readyFds;
}
