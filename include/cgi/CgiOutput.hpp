#include <AFd.hpp>

class CgiOutput : public AFd
{
public:
  CgiOutput(int fd);
  ~CgiOutput();

  virtual int handdle_event(uint32_t event);

private:
  /* data */
};
