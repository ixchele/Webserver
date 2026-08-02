#include <AFd.hpp>

class CgiInput : public AFd
{
public:
  CgiInput(int fd);
  ~CgiInput();
  
  virtual int handle_event(uint32_t event);

private:
  /* data */
};
