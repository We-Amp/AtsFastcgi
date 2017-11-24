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

void
FastCGIIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_HEADER) {
    cout << "Read request header data" << endl << data;
    server->clientData += data;
  } else {
    server->clientRequestBody += data;
  }
}

void
FastCGIIntercept::handleInputComplete()
{
  FCGIServer::server()->writeToServer(_request_id);
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

void
FastCGIIntercept::initServer()
{
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
  // TSContDestroy(this->contp_);
  this->vc_        = nullptr;
  this->readio.vio = this->writeio.vio = nullptr;
  this->txn_                           = nullptr;
  request_id                           = 0;
  delete fcgiRequest;
}
