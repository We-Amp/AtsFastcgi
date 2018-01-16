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
class ProfileTaker;
class ServerConnection;
class ServerIntercept : public InterceptPlugin
{
public:
  int headCount = 0, bodyCount = 0, emptyCount = 0;
  TSHttpTxn _txn;
  bool connInuse = false;

  ServerIntercept(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
  {
    _server_conn        = nullptr;
    _txn                = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    profileTakerInput   = nullptr;
    profileTakerOutput  = nullptr;
    profileTakerReq     = nullptr;
    inputCompleteState  = false;
    outputCompleteState = false;
    TSDebug(PLUGIN_NAME, "ServerIntercept : Added Server intercept");
  }

  ~ServerIntercept() override;

  void consume(const string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;
  void streamReqHeader(const string &data);
  void streamReqBody(const string &data);

  void writeResponseChunkToATS(std::string &data);
  void setResponseOutputComplete();

  void
  setRequestId(uint request_id)
  {
    _request_id = request_id;
  }

  void
  setServerConn(ServerConnection *conn)
  {
    _server_conn = conn;
  }

  uint
  requestId()
  {
    return _request_id;
  }

  void resumeIntercept();

  bool
  getOutputCompleteState()
  {
    return outputCompleteState;
  }

private:
  uint _request_id;
  ServerConnection *_server_conn;
  string clientHeader, clientBody;
  bool inputCompleteState, outputCompleteState;

  ProfileTaker *profileTakerInput, *profileTakerOutput, *profileTakerReq;
  bool profInputFlag = false, profOutputFlag = false;
};
}
#endif /*_SERVER_INTERCEPT_H_*/
