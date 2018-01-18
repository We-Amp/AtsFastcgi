#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H
#include <queue>
#include "server_intercept.h"
#include "fcgi_config.h"
#include <ts/ts.h>
namespace ats_plugin
{
class ServerIntercept;
class RequestQueue
{
  TSMutex mutex;
  uint max_queue_size;
  std::queue<ServerIntercept *> pending_list;

public:
  RequestQueue();
  ~RequestQueue();

  uint isQueueFull();
  uint isQueueEmpty();
  uint getSize();
  uint addToQueue(ServerIntercept *);
  ServerIntercept *removeFromQueue();
};
}
#endif /*REQUEST_QUEUE_H*/