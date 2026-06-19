#pragma once
# include <AFd.hpp>

class Client : public AFd
{
  public:
    virtual void handle_event();

    ~Client();

  private:
    int m_fd;
};
