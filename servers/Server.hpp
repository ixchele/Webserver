#pragma once
#include <sys/socket.h>
#include <string>
using namespace std;

// each server in config file will be inistiated from this class
class Server {
public:
	int get_sockFd();
	void run();


private:
	int 		m_sockFd;
	short 	m_port;
	int			m_address;
	string	m_name;
};