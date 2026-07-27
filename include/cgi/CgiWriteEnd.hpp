#include <AFd.hpp>

class CgiWriteEnd : public AFd
{
public:
  CgiWriteEnd(int fd);
  ~CgiWriteEnd();
  
  virtual int handdle_event(uint32_t event);

private:
  /* data */
};

