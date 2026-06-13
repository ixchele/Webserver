#pragma once

class Client
{
  public:
    Client();
    Client(const int &fd);
    ~Client();

  private:
    int m_fd;
};