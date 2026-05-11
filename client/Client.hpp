#pragma once

class Client
{
    public:
        void close_connection();

    private:
        int m_fd;

};
