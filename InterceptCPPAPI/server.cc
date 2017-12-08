
#include "server.h"

#include <sstream>

#include "server_intercept.h"
#include "server_connection.h"
#include "connection_pool.h"
#include "ats_mod_intercept.h"

uint UniqueRequesID::_id = 1;

void
interceptTransferData(ServerIntercept *intercept, ServerConnection *server_conn)
{
  TSIOBufferBlock block;
  int64_t consumed = 0;

  std::ostringstream output;

  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(server_conn->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;
    ptr = TSIOBufferBlockReadStart(block, server_conn->readio.reader, &remain);
    if (remain) {
      server_conn->fcgiRequest()->fcgiDecodeRecordChunk((uchar *)ptr, remain, output);
    }
    consumed += remain;
  }

  if (consumed) {
    cout << "InterceptTransferData: Read " << consumed << " Bytes from server and writing it to client side" << endl;
    TSIOBufferReaderConsume(server_conn->readio.reader, consumed);
  }
  TSVIONDoneSet(server_conn->readio.vio, TSVIONDoneGet(server_conn->readio.vio) + consumed);

  std::string data = std::move(output.str());
  intercept->writeResponseChunkToATS(data);
}

static int
handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents:  event( %d )\tEventName: %s", event, TSHttpEventNameLookup(event));

  ServerConnectionInfo *conn_info     = (ServerConnectionInfo *)TSContDataGet(contp);
  Server *server                      = conn_info->server;
  ServerConnection *server_connection = conn_info->server_connection;

  // ServerIntercept *fcgi         = fcgi_server->getIntercept(request_id);
  // InterceptIO *server            = fcgi->server;
  // FCGIClientRequest *fcgiRequest = fcgi->server->fcgiRequest;

  switch (event) {
  case TS_EVENT_NET_CONNECT: {
    server_connection->vc_ = (TSVConn)edata;
    server_connection->setState(ServerConnection::READY);

    TSDebug(PLUGIN_NAME, "Connected to FCGI Server. WriteIO.vio :%p \t ReadIO.vio :%p ", server_connection->writeio.vio,
            server_connection->readio.vio);
    break;
  }

  case TS_EVENT_NET_CONNECT_FAILED: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents : TSNetConnect Failed.");

    // TODO: Check what to do
    // server->closeServer();
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_READY: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Inside Read Ready...VConn Open ");
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    interceptTransferData(intercept, server_connection);

    break;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    TSDebug(PLUGIN_NAME, "Write Ready.WriteIO.vio yet to write : %ld bytes.", TSVIONTodoGet(server_connection->writeio.vio));
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_READ_COMPLETE:
    return TS_EVENT_NONE;

  case TS_EVENT_VCONN_EOS: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Sending Response to client side");
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    interceptTransferData(intercept, server_connection);

    intercept->setResponseOutputComplete();
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

Server *
Server::server()
{
  return InterceptGlobal::fcgi_server;
}

Server::Server()
{
  createConnections();
}

ServerIntercept *
Server::getIntercept(uint request_id)
{
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    return std::get<0>(itr->second);
  }
  return nullptr;
}

ServerConnection *
Server::getServerConnection(uint request_id)
{
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    return std::get<1>(itr->second);
  }
  return nullptr;
}

void
Server::removeIntercept(uint request_id)
{
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    _intercept_list.erase(itr);
  }

  // RequestIntercept *req_intercept = (RequestIntercept *)TSContDataGet(contp);
  // delete req_intercept;
}

void
Server::writeRequestHeader(uint request_id)
{
  ServerConnection *server_conn  = getServerConnection(request_id);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
  unsigned char *clientReq;
  int reqLen = 0;

  // TODO: possibly move all this as one function in server_connection
  fcgiRequest->createBeginRequest();
  clientReq = fcgiRequest->addClientRequest(reqLen);
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen);
}

void
Server::writeRequestBody(uint request_id, const string &data)
{
  ServerConnection *server_conn  = getServerConnection(request_id);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();

  // TODO: possibly move all this as one function in server_connection
  unsigned char *clientReq;
  int reqLen            = 0;
  fcgiRequest->postData = data;
  fcgiRequest->postBodyChunk();
  clientReq = fcgiRequest->addClientRequest(reqLen);
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen);
}

void
Server::writeRequestBodyComplete(uint request_id)
{
  ServerConnection *server_conn  = getServerConnection(request_id);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();

  // TODO: possibly move all this as one function in server_connection
  unsigned char *clientReq;
  int reqLen = 0;
  fcgiRequest->emptyParam();
  clientReq = fcgiRequest->addClientRequest(reqLen);
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen);
  server_conn->readio.read(server_conn->vc_, server_conn->contp());
}

const uint
Server::connect(ServerIntercept *intercept)
{
  const uint request_id = UniqueRequesID::getNext();

  intercept->setRequestId(request_id);

  ServerConnection *conn = _connection_pool->getAvailableConnection();

  if (conn) {
    conn->setRequestId(request_id);

    // TODO: Check better way to do it
    conn->createFCGIClient(intercept->_txn);

    _intercept_list[request_id] = std::make_tuple(intercept, conn);
  } else {
    TSDebug(PLUGIN_NAME, "Server::connect: Adding to pending list.");
    // TODO: put in pending list
  }

  return request_id;
}

void
Server::createConnections()
{
  _connection_pool = new ConnectionPool(this, handlePHPConnectionEvents);
}
