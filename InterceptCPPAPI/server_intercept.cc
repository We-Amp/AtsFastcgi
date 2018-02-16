
#include "server_intercept.h"

#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

#include <netinet/in.h>
#include "atscppapi/HttpMethod.h"
#include "atscppapi/Transaction.h"
#include "atscppapi/TransactionPlugin.h"
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/utils.h>

#include "ats_fcgi_client.h"
#include "fcgi_config.h"
#include "server.h"
#include "server_connection.h"
#include "ats_mod_intercept.h"
#include <stdexcept>

using std::runtime_error;
class SignalException : public runtime_error
{
public:
  SignalException(const std::string &_message) : std::runtime_error(_message) {}
};

using namespace atscppapi;
using namespace ats_plugin;

ServerIntercept::~ServerIntercept()
{
  TSDebug(PLUGIN_NAME, "~ServerIntercept : Shutting down server intercept. _request_id: %d", _request_id);
  _txn = nullptr;
  if (!outputCompleteState) {
    clientAborted = true;
    Server::server()->removeIntercept(_request_id);
  }

  TSStatIntIncrement(InterceptGlobal::respEndId, 1);
}

void
ServerIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_HEADER) {
    TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] Read request header data.", __FUNCTION__);
    streamReqHeader(data);
  } else {
    streamReqBody(data);
  }
}

void
ServerIntercept::streamReqHeader(const string &data)
{
  TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] headCount: %d \t_request_id:%d", __FUNCTION__, headCount++, _request_id);
  if (!Server::server()->writeRequestHeader(_request_id)) {
    dataBuffered = true;
    clientHeader += data;
  }
}

void
ServerIntercept::streamReqBody(const string &data)
{
  TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] bodyCount: %d", __FUNCTION__, bodyCount++);
  if (!Server::server()->writeRequestBody(_request_id, data)) {
    dataBuffered = true;
    clientBody += data;
  }
}

void
ServerIntercept::handleInputComplete()
{
  TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] Count : %d \t_request_id: %d", __FUNCTION__, emptyCount++, _request_id);
  if (!Server::server()->writeRequestBodyComplete(_request_id)) {
    return;
  }
  inputCompleteState = true;
}

void
ServerIntercept::resumeIntercept()
{
  Server::server()->writeRequestHeader(_request_id);
  Server::server()->writeRequestBody(_request_id, clientBody);
  if (inputCompleteState)
    Server::server()->writeRequestBodyComplete(_request_id);
}

void
ServerIntercept::writeResponseChunkToATS(std::string &data)
{
  try {
    InterceptPlugin::produce(data);
  } catch (SignalException &ex) {
    std::cout << "SignalException: " << ex.what() << std::endl;
  }
}

void
ServerIntercept::setResponseOutputComplete()
{
  InterceptPlugin::setOutputComplete();
  outputCompleteState = true;
  Server::server()->removeIntercept(_request_id);
}
