#pragma once
#include <string>
#include <map>
#include <vector>
#include <Client.hpp>

#define APP_BUFFER_SIZE 8192

class Client;

class Request
{
public:
    struct Line
    {
        int method;
        std::string uri;
        std::string version;

        Line();

        void clear();
    };

    char m_buffer[APP_BUFFER_SIZE];
    Client *m_client;
    Line m_requestLine;
    std::map<std::string, std::string> m_headers;
    std::vector<std::string> m_bodies;
    std::vector<std::string> m_chunks;
    std::string m_sbuffer;

    Request(Client *client);
    ~Request();

    int receive_data();
    void clear();

private:
    /* data */
};
