
#include "fcgi_server.h"

#include "fcgi_intercept.h"
#include "ats_mod_fcgi.h"

uint UniqueRequesID::_id = 1;

struct RequestIntercept {
public:
  RequestIntercept(uint id, FCGIServer *fServer) : request_id(id), server(fServer) {}
  uint request_id;
  FCGIServer *server;
};

void
interceptTransferData(InterceptIO *server, FCGIClientRequest *fcgiRequest)
{
  TSIOBufferBlock block;
  int64_t consumed       = 0;
  server->serverResponse = "";

  std::string output;

  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(server->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;
    ptr = TSIOBufferBlockReadStart(block, server->readio.reader, &remain);
    if (remain) {
      fcgiRequest->fcgiDecodeRecordChunk((uchar *)ptr, remain, output);
    }
    consumed += remain;
  }

  if (consumed) {
    cout << "InterceptTransferData: Read " << consumed << " Bytes from server and writing it to client side" << endl;
    TSIOBufferReaderConsume(server->readio.reader, consumed);
  }
  TSVIONDoneSet(server->readio.vio, TSVIONDoneGet(server->readio.vio) + consumed);

  server->serverResponse = std::move(output);
  cout << "Server Response:" << endl << server->serverResponse << endl;
}

static int
handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents:  event( %d )\tEventName: %s", event, TSHttpEventNameLookup(event));

  RequestIntercept *req_intercept = (RequestIntercept *)TSContDataGet(contp);
  FCGIServer *fcgi_server         = req_intercept->server;
  uint request_id                 = req_intercept->request_id;

  FastCGIIntercept *fcgi         = fcgi_server->getIntercept(request_id);
  InterceptIO *server            = fcgi->server;
  FCGIClientRequest *fcgiRequest = fcgi->server->fcgiRequest;

  switch (event) {
  case TS_EVENT_NET_CONNECT: {
    // TODO Move this to intercept
    server->vc_ = (TSVConn)edata;

    TSDebug(PLUGIN_NAME, "Connected to FCGI Server. WriteIO.vio :%p \t ReadIO.vio :%p ", server->writeio.vio, server->readio.vio);
    break;
  }

  case TS_EVENT_NET_CONNECT_FAILED: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents : TSNetConnect Failed.");
    server->closeServer();
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_READY: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Inside Read Ready...VConn Open ");
    interceptTransferData(server, fcgiRequest);

    fcgi->writeResponseChunkToATS();
    break;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    TSDebug(PLUGIN_NAME, "Write Ready.WriteIO.vio yet to write : %ld bytes.", TSVIONTodoGet(server->writeio.vio));

    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_READ_COMPLETE:
    return TS_EVENT_NONE;

  case TS_EVENT_VCONN_EOS: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Sending Response to client side");
    interceptTransferData(server, fcgiRequest);

    fcgi->writeResponseChunkToATS();

    fcgi->setResponseOutputComplete();
    return TS_EVENT_NONE;
  }

  case TS_EVENT_ERROR: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Error...");
    return TS_EVENT_NONE;
  }

  default:
    break;
  }

  return TS_EVENT_NONE;
}

FCGIServer *
FCGIServer::server()
{
  return fcgiGlobal::fcgi_server;
}

FastCGIIntercept *
FCGIServer::getIntercept(uint request_id)
{
  auto itr = request_list.find(request_id);
  if (itr != request_list.end()) {
    return itr->second;
  }
  return nullptr;
}

void
FCGIServer::writeToServer(uint request_id)
{
  FastCGIIntercept *fcgi         = getIntercept(request_id);
  InterceptIO *server            = fcgi->server;
  FCGIClientRequest *fcgiRequest = fcgi->server->fcgiRequest;

  // create a php request according to client requested appropriate headers
  string clientData;
  unsigned char *clientReq;
  int reqLen            = 0;
  fcgiRequest->postData = server->clientRequestBody;
  // TODO send request id here
  fcgiRequest->createBeginRequest();
  clientReq = fcgiRequest->addClientRequest(server->clientData, reqLen);
  // To Print FCGIRequest Headers
  // fcgiRequest->print_bytes(clientReq,reqLen);
  server->writeio.phpWrite(server->vc_, fcgi->contp_, clientReq, reqLen);
  server->readio.read(server->vc_, fcgi->contp_);
}

const uint
FCGIServer::connect(FastCGIIntercept *intercept)
{
  const uint request_id = UniqueRequesID::getNext();

  request_list[request_id] = intercept;

  intercept->setRequestId(request_id);

  TSCont contp;
  struct sockaddr_in ip_addr;

  unsigned short int a, b, c, d, p;
  char *arr  = fcgiGlobal::plugin_data->global_config->server_ip;
  char *port = fcgiGlobal::plugin_data->global_config->server_port;
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
  TSContDataSet(contp, new RequestIntercept(request_id, this));
  TSNetConnect(contp, (struct sockaddr const *)&ip_addr);
  TSDebug(PLUGIN_NAME, "FastCGIIntercept::initServer : Data Set Contp: %p", contp);

  intercept->contp_ = contp;

  return request_id;
}