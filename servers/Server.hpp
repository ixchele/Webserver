#pragma once
#include <sys/socket.h>

// each server in config file will be inistiated from this class
class Server {
public:
	int get_sockFd();
	void run();


private:
	int m_sockFd;
};