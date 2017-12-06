#include "fcgi_intercept.h"
#include "ats_fcgi_client.h"
#include "ats_mod_fcgi.h"
#include "atscppapi/HttpMethod.h"
#include "atscppapi/Transaction.h"
#include "atscppapi/TransactionPlugin.h"
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/utils.h>
#include <iostream>
#include <iterator>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <string>

using namespace fcgiGlobal;
using namespace atscppapi;

FastCGIIntercept::~FastCGIIntercept()
{
  TSDebug(PLUGIN_NAME, "~FastCGIIntercept : Shutting down server intercept");
  FCGIServer::server()->removeIntercept(_request_id, contp_);
  TSContDestroy(contp_);
  server->closeServer();
}

void
FastCGIIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_HEADER) {
    cout << "Read request header data" << endl << data;
    streamReqHeader(data);

  } else {
    streamReqBody(data);
  }
}

void
FastCGIIntercept::streamReqHeader(const string &data)
{
  cout << "streamReqHeader: " << headCount++ << endl;
  FCGIServer::server()->writeRequestHeader(_request_id);
}
void
FastCGIIntercept::streamReqBody(const string &data)
{
  cout << "streamReqBody: " << bodyCount++ << endl;
  FCGIServer::server()->writeRequestBody(_request_id, data);
}

void
FastCGIIntercept::handleInputComplete()
{
  cout << "HandleInputComplete: " << emptyCount++ << endl;
  FCGIServer::server()->writeRequestBodyComplete(_request_id);
}

void
FastCGIIntercept::writeResponseChunkToATS()
{
  InterceptPlugin::produce(server->serverResponse);
}

void
FastCGIIntercept::setResponseOutputComplete()
{
  InterceptPlugin::setOutputComplete();
}

InterceptIO::InterceptIO(int request_id, TSHttpTxn txn)
  : vc_(nullptr),
    txn_(txn),
    // contp_(nullptr),
    clientData(""),
    clientRequestBody(""),
    serverResponse(""),
    readio(),
    writeio(),
    fcgiRequest(nullptr),
    request_id(request_id)
{
  fcgiRequest = new FCGIClientRequest(request_id, txn);
}

void
InterceptIO::closeServer()
{
  TSDebug(PLUGIN_NAME, "InterceptIO Destructor.");
  if (this->vc_) {
    TSVConnClose(this->vc_);
  }
  this->vc_        = nullptr;
  this->readio.vio = this->writeio.vio = nullptr;
  this->txn_                           = nullptr;
  request_id                           = 0;
  delete fcgiRequest;
}
