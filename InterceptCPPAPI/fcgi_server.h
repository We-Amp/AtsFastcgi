#ifndef _FCGI_SERVER_H_
#define _FCGI_SERVER_H_

#include "fcgi_intercept.h"
#include <map>
#include <queue>
#include <atscppapi/Transaction.h>

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
  int64_t max_connections, min_connections, request_queue_size;
  static FCGIServer *server();

  const uint connect(FastCGIIntercept *intercept);

  FastCGIIntercept *getIntercept(uint request_id);
  void removeIntercept(uint request_id, TSCont contp);

  void writeRequestHeader(uint request_id);
  void writeRequestBody(uint request_id, const std::string &data);
  void writeRequestBodyComplete(uint request_id);
  void setDefaultLimits(int64_t min, int64_t max, int64_t queueSize);
  void addToRequestQueue(FastCGIIntercept *intercept);

  FastCGIIntercept *popFromRequestQueue();
  std::queue<FastCGIIntercept *> request_queue;

private:
  std::map<uint, FastCGIIntercept *> request_list;
};

#endif
