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
  void removeIntercept(uint request_id, TSCont contp);

  void writeRequestHeader(uint request_id);
  void writeRequestBody(uint request_id, const std::string &data);
  void writeRequestBodyComplete(uint request_id);

private:
  std::map<uint, FastCGIIntercept *> request_list;
};

#endif
