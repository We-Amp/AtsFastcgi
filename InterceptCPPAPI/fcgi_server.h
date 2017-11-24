#ifndef _FCGI_SERVER_H_
#define _FCGI_SERVER_H_

#include "fcgi_intercept.h"
#include <map>

class FastCGIIntercept;

class UniqueRequesID
{
public:
  static const uint
  getNext()
  {
    // TODO add mutext here
    return _id++;
  }

private:
  static uint _id;
};

class FCGIServer
{
public:
  static FCGIServer *server();

  const uint connect(FastCGIIntercept *intercept);

  FastCGIIntercept *getIntercept(uint request_id);

  void writeToServer(uint request_id);

private:
  std::map<uint, FastCGIIntercept *> request_list;
};

#endif
