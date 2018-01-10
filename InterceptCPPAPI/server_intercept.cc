
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

using namespace atscppapi;
using namespace ats_plugin;
ServerIntercept::~ServerIntercept()
{
  TSDebug(PLUGIN_NAME, "~ServerIntercept : Shutting down server intercept. _request_id: %d", _request_id);
  Server::server()->removeIntercept(_request_id);
  _txn = nullptr;
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
  if (_server_conn && _server_conn->getState() == ServerConnection::INUSE) {
    TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] headCount: %d \t_request_id:%d", __FUNCTION__, headCount++, _request_id);
    Server::server()->writeRequestHeader(_request_id, _server_conn);
  } else {
    TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] Buffering client Req Header.", __FUNCTION__);
    clientHeader += data;
  }
}
void
ServerIntercept::streamReqBody(const string &data)
{
  if (_server_conn && _server_conn->getState() == ServerConnection::INUSE) {
    TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] bodyCount: %d", __FUNCTION__, bodyCount++);
    Server::server()->writeRequestBody(_request_id, _server_conn, data);
  } else {
    TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] Buffering client Req Body.", __FUNCTION__);
    clientBody += data;
  }
}

void
ServerIntercept::handleInputComplete()
{
  if (_server_conn && _server_conn->getState() == ServerConnection::INUSE) {
    TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] Count : %d \t_request_id: %d", __FUNCTION__, emptyCount++, _request_id);
    Server::server()->writeRequestBodyComplete(_request_id, _server_conn);
  } else {
    inputCompleteState = true;
  }
}

void
ServerIntercept::resumeIntercept()
{
  Server::server()->writeRequestHeader(_request_id, _server_conn);
  Server::server()->writeRequestBody(_request_id, _server_conn, clientBody);
  if (inputCompleteState)
    Server::server()->writeRequestBodyComplete(_request_id, _server_conn);
}
void
ServerIntercept::writeResponseChunkToATS(std::string &data)
{
  InterceptPlugin::produce(data);
}

void
ServerIntercept::setResponseOutputComplete()
{
  InterceptPlugin::setOutputComplete();
  outputCompleteState = true;
}
