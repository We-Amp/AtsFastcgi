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
  TSReturnCode status;
  if (!isQueueFull()) {
    status = TSMutexLockTry(mutex);
    if (status == TS_SUCCESS) {
      pending_list.push(intercept);
      TSMutexUnlock(mutex);
      return 1;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}
ServerIntercept *
RequestQueue::removeFromQueue()
{
  TSReturnCode status;
  if (!isQueueEmpty()) {
    status = TSMutexLockTry(mutex);
    if (status == TS_SUCCESS) {
      ServerIntercept *intercept = pending_list.front();
      pending_list.pop();
      TSMutexUnlock(mutex);
      return intercept;
    } else {
      return nullptr;
    }
  } else {
    return nullptr;
  }
}