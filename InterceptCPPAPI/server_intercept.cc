
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
#include "ats_fcgi_config.h"
#include "server.h"

using namespace atscppapi;

ServerIntercept::~ServerIntercept()
{
  TSDebug(PLUGIN_NAME, "~ServerIntercept : Shutting down server intercept");
  Server::server()->removeIntercept(_request_id);
}

void
ServerIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_HEADER) {
    cout << "Read request header data" << endl << data;
    streamReqHeader(data);

  } else {
    streamReqBody(data);
  }
}

void
ServerIntercept::streamReqHeader(const string &data)
{
  cout << "streamReqHeader: " << headCount++ << endl;
  Server::server()->writeRequestHeader(_request_id);
}
void
ServerIntercept::streamReqBody(const string &data)
{
  cout << "streamReqBody: " << bodyCount++ << endl;
  Server::server()->writeRequestBody(_request_id, data);
}

void
ServerIntercept::handleInputComplete()
{
  cout << "HandleInputComplete: " << emptyCount++ << endl;
  Server::server()->writeRequestBodyComplete(_request_id);
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
}
