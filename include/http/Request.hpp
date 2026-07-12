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
    enum Step {ERequestLine, EHeaders, EBody};

    struct Line
    {
        int method;
        std::string uri;
        std::string version;

        Line();

        void clear();
    };

    Step m_step;
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
    void extruction();
    void extruct_request_line();
    void extruct_headers();
    void extruct_bodies();
    void clear();

private:
    /* data */
};
