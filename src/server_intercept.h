#ifndef _SERVER_INTERCEPT_H_
#define _SERVER_INTERCEPT_H_

#include "ats_fcgi_client.h"
#include "fcgi_config.h"
#include "atscppapi/Transaction.h"
#include "atscppapi/TransactionPlugin.h"
#include "ts/ink_defs.h"
#include "ts/ts.h"
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/utils.h>
#include <iostream>
#include <iterator>
#include <map>
#include <netinet/in.h>
#include <string.h>

#define PORT 60000

using std::cout;
using std::endl;
using std::string;

using namespace atscppapi;

namespace ats_plugin
{
class ServerConnection;
class ServerIntercept : public InterceptPlugin
{
public:
  int headCount = 0, bodyCount = 0, emptyCount = 0;
  bool dataBuffered, clientAborted = false;
  bool serverDataBuffered;
  string serverResponse;
  TSHttpTxn _txn;
  ServerIntercept(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
  {
    _txn                = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    clientAborted       = false;
    dataBuffered        = false;
    serverDataBuffered  = false;
    inputCompleteState  = false;
    outputCompleteState = false;
    TSDebug(PLUGIN_NAME, "ServerIntercept : Added Server intercept");
  }

  ~ServerIntercept() override;

  void consume(const string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;
  void streamReqHeader(const string &data);
  void streamReqBody(const string &data);

  bool writeResponseChunkToATS(std::string &data);
  bool setResponseOutputComplete();

  void
  setRequestId(uint request_id)
  {
    _request_id = request_id;
  }

  uint
  requestId()
  {
    return _request_id;
  }

  bool
  getOutputCompleteState()
  {
    return outputCompleteState;
  }

private:
  uint _request_id;
  string clientHeader, clientBody;

  bool inputCompleteState, outputCompleteState;
};
}
#endif /*_SERVER_INTERCEPT_H_*/
