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
    cout << "Read request body data" << endl << data << endl;
    server->clientRequestBody += data;
  }
}

void
FastCGIIntercept::handleInputComplete()
{
  server->contp_ = initServer();
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

int64_t
interceptTransferData(InterceptIO *server, FCGIClientRequest *fcgiRequest)
{
  TSIOBufferBlock block;
  int64_t consumed       = 0;
  server->serverResponse = "";
  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(server->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;
    ptr = TSIOBufferBlockReadStart(block, server->readio.reader, &remain);
    if (remain) {
      fcgiRequest->fcgiDecodeRecordChunk((uchar *)ptr, remain);
    }
    consumed += remain;
  }
  if (consumed) {
    cout << "InterceptTransferData: Read " << consumed << " Bytes from server and writing it to client side" << endl;
    TSIOBufferReaderConsume(server->readio.reader, consumed);
  }
  TSVIONDoneSet(server->readio.vio, TSVIONDoneGet(server->readio.vio) + consumed);
  return consumed;
}

static int
handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents:  event( %d )\tEventName: %s", event, TSHttpEventNameLookup(event));

  FastCGIIntercept *fcgi         = (FastCGIIntercept *)TSContDataGet(contp);
  InterceptIO *server            = fcgi->server;
  FCGIClientRequest *fcgiRequest = fcgi->server->fcgiRequest;
  switch (event) {
  case TS_EVENT_NET_CONNECT: {
    server->vc_    = (TSVConn)edata;
    server->contp_ = contp;
    // create a php request according to client requested appropriate headers
    string clientData;
    unsigned char *clientReq;
    int reqLen            = 0;
    fcgiRequest->postData = server->clientRequestBody;
    fcgiRequest->createBeginRequest();
    clientReq = fcgiRequest->addClientRequest(server->clientData, reqLen);
    // To Print FCGIRequest Headers
    // fcgiRequest->print_bytes(clientReq,reqLen);
    server->writeio.phpWrite(server->vc_, contp, clientReq, reqLen);
    server->readio.read(server->vc_, contp);
    TSDebug(PLUGIN_NAME, "Connected to FCGI Server. WriteIO.vio :%p \t ReadIO.vio :%p ", server->writeio.vio, server->readio.vio);
  } break;

  case TS_EVENT_NET_CONNECT_FAILED: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents : TSNetConnect Failed.");
    server->closeServer();
    return TS_EVENT_NONE;
  }
  case TS_EVENT_VCONN_READ_READY: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Inside Read Ready...VConn Open ");
    interceptTransferData(server, fcgiRequest);
    break;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    TSDebug(PLUGIN_NAME, "Write Ready.WriteIO.vio yet to write : %ld bytes.", TSVIONTodoGet(server->writeio.vio));

    return TS_EVENT_NONE;
  } break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_READ_COMPLETE:
    return TS_EVENT_NONE;

  case TS_EVENT_VCONN_EOS: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Sending Response to client side");
    interceptTransferData(server, fcgiRequest);
    server->serverResponse = fcgiRequest->writeToServerObj();

    fcgi->writeResponseChunkToATS();
    fcgi->setResponseOutputComplete();
    return TS_EVENT_NONE;
  } break;
  case TS_EVENT_ERROR: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Error...");
    return TS_EVENT_NONE;
  }

  default:
    break;
  }
  return 0;
}

TSCont
FastCGIIntercept::initServer()
{
  TSCont contp;
  struct sockaddr_in ip_addr;
  // server_ip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  // server_ip = htonl(server_ip);
  // server_port = htons(PORT);
  unsigned short int a, b, c, d, p;
  char *arr  = plugin_data->global_config->server_ip;
  char *port = plugin_data->global_config->server_port;
  cout << "arr : " << arr << endl;
  sscanf(arr, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
  sscanf(port, "%hu", &p);
  printf("%d %d %d %d : %d", a, b, c, d, p);

  int new_ip = (a << 24) | (b << 16) | (c << 8) | (d);
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family      = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(new_ip); /* Should be in network byte order */
  ip_addr.sin_port        = htons(p);      // server_port;

  // contp is a global netconnect handler  which will be used to connect with
  // php server
  contp = TSContCreate(handlePHPConnectionEvents, TSMutexCreate());
  TSContDataSet(contp, this);
  TSNetConnect(contp, (struct sockaddr const *)&ip_addr);
  TSDebug(PLUGIN_NAME, "FastCGIIntercept::initServer : Data Set Contp: %p", contp);
  return contp;
}

void
InterceptIO::closeServer()
{
  TSDebug(PLUGIN_NAME, "InterceptIO Destructor.");
  if (this->vc_) {
    TSVConnClose(this->vc_);
  }
  TSContDestroy(this->contp_);
  this->vc_        = nullptr;
  this->readio.vio = this->writeio.vio = nullptr;
  this->txn_                           = nullptr;
  request_id                           = 0;
  delete fcgiRequest;
}
