#include <AFd.hpp>

class CgiOutput : public AFd
{
public:
  CgiOutput(int fd);
  ~CgiOutput();

  virtual int handle_event(uint32_t event);

private:
  /* data */
};
