#include "request_queue.h"
#include "ats_mod_intercept.h"
using namespace ats_plugin;

RequestQueue::RequestQueue()
{
  mutex                     = TSMutexCreate();
  FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  max_queue_size            = gConfig->getRequestQueueSize();
}

RequestQueue::~RequestQueue()
{
  max_queue_size = 0;
  TSMutexDestroy(mutex);
}

uint
RequestQueue::isQueueFull()
{
  if (pending_list.size() >= max_queue_size) {
    return 1;
  } else {
    return 0;
  }
}

uint
RequestQueue::getSize()
{
  return pending_list.size();
}

uint
RequestQueue::isQueueEmpty()
{
  if (pending_list.empty()) {
    return 1;
  } else {
    return 0;
  }
}

uint
RequestQueue::addToQueue(ServerIntercept *intercept)
{
  TSMutexLock(mutex);
  if (!isQueueFull()) {
    pending_list.push(intercept);
  }
  // TODO: handle queue full use case
  TSMutexUnlock(mutex);
  return 1;
}

ServerIntercept *
RequestQueue::popFromQueue()
{
  ServerIntercept *intercept = nullptr;
  TSMutexLock(mutex);
  if (!pending_list.empty()) {
    intercept = pending_list.front();
    pending_list.pop();
  }
  TSMutexUnlock(mutex);
  return intercept;
}
