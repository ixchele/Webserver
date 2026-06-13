#pragma once
#include "Utils.hpp"
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

class Socket {
	private:
		int                 socketFd;
		struct sockaddr_in  socketAddr;

	public:
		Socket(int domain, int type, int protocol);

		// NOTE : for client socket
		Socket(int fd, const struct sockaddr_in& addr);

		~Socket();

		void bind(int port, const std::string& host);
		void listen(int backlog);

		void setNonBlocking(); 

		int getFd() const;
		int getPort() const;

		// TODO : experiment a stream Exception 
		struct SocketException : public std::runtime_error {
			SocketException(const std::string &error)
				: std::runtime_error("[x] Socket Error: " + error) {};
		};
};
