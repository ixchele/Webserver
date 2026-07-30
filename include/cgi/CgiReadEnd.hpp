#include <AFd.hpp>

class CgiReadEnd : public AFd
{
public:
  CgiReadEnd(int fd);
  ~CgiReadEnd();

  virtual int handdle_event(uint32_t event);

private:
  /* data */
};
