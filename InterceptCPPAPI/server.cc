
#include "server.h"

#include <sstream>

#include "server_intercept.h"
#include "server_connection.h"
#include "connection_pool.h"
#include "ats_mod_intercept.h"
using namespace ats_plugin;

uint UniqueRequesID::_id = 1;

bool
interceptTransferData(ServerIntercept *intercept, ServerConnection *server_conn)
{
  TSIOBufferBlock block;
  int64_t consumed    = 0;
  bool responseStatus = false;
  std::ostringstream output;

  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(server_conn->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;
    ptr = TSIOBufferBlockReadStart(block, server_conn->readio.reader, &remain);
    if (remain) {
      responseStatus = server_conn->fcgiRequest()->fcgiDecodeRecordChunk((uchar *)ptr, remain, output);
    }
    consumed += remain;
  }

  if (consumed) {
    TSDebug(PLUGIN_NAME, "[%s] Read %ld bytes from server and writing it to client side.", __FUNCTION__, consumed);
    TSIOBufferReaderConsume(server_conn->readio.reader, consumed);
  }
  TSVIONDoneSet(server_conn->readio.vio, TSVIONDoneGet(server_conn->readio.vio) + consumed);

  std::string data = std::move(output.str());
  intercept->writeResponseChunkToATS(data);
  return responseStatus;
}

static int
handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "[%s]:  event( %d )\tEventName: %s\tContp: %p ", __FUNCTION__, event, TSHttpEventNameLookup(event), contp);

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

    TSDebug(PLUGIN_NAME, "Connected to FCGI Server. vc_ : %p \t _contp: %p \tWriteIO.vio :%p \t ReadIO.vio :%p ",
            server_connection->vc_, server_connection->contp(), server_connection->writeio.vio, server_connection->readio.vio);
    break;
  }

  case TS_EVENT_NET_CONNECT_FAILED: {
    TSDebug(PLUGIN_NAME, "[%s] : TSNetConnect Failed.", __FUNCTION__);

    // TODO: Check what to do
    // server->closeServer();
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_READY: {
    TSDebug(PLUGIN_NAME, "[%s]: Inside Read Ready...VConn Open vc_: %p", __FUNCTION__, server_connection->vc_);
    bool responseStatus        = false;
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    responseStatus             = interceptTransferData(intercept, server_connection);

    if (responseStatus == true) {
      TSDebug(PLUGIN_NAME, "[%s]: ResponseComplete...Sending Response to client stream. _request_id: %d", __FUNCTION__,
              server_connection->requestId());
      intercept->setResponseOutputComplete();
      server_connection->setState(ServerConnection::CLOSED);
    }
    break;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    TSDebug(PLUGIN_NAME, "[%s]: Write Ready.WriteIO.vio. Wrote : %ld bytes. vc_ : %p", __FUNCTION__,
            TSVIONDoneGet(server_connection->writeio.vio), server_connection->vc_);

    // if(server_connection->getState()==ServerConnection::WRITE_COMPLETE){
    //   TSContCall(TSVIOContGet(server_connection->writeio.vio), TS_EVENT_VCONN_WRITE_COMPLETE, server_connection->writeio.vio);
    // }

  } break;
  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: Start Reading from Server now...", __FUNCTION__);
    // server_connection->setState(ServerConnection::READ);
    // server_connection->readio.read(server_connection->vc_, server_connection->contp());
  } break;
  case TS_EVENT_VCONN_READ_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: Server Read Complete...", __FUNCTION__);
    // TSVConnClose(server_connection->vc_);;
  } break;
  case TS_EVENT_VCONN_EOS: {
    TSDebug(PLUGIN_NAME, "[%s]: EOS reached.", __FUNCTION__);
    bool responseStatus        = false;
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    interceptTransferData(intercept, server_connection);
    if (responseStatus == true) {
      TSDebug(PLUGIN_NAME, "[%s]: EOS => Sending Response to client side", __FUNCTION__);
      intercept->setResponseOutputComplete();
    }

  } break;
  case TS_EVENT_ERROR: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Connection Disconnected...Error...");
    TSVConnAbort(server_connection->vc_, 1);
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    // sending output complete as no support function provided to abort client connection
    intercept->setResponseOutputComplete();
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
  return InterceptGlobal::gServer;
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
    // ServerIntercept *intercept = std::get<0>(itr->second);
    ServerConnection *serv_conn = std::get<1>(itr->second);
    _intercept_list.erase(itr);

    TSDebug(PLUGIN_NAME, "Reset  server Connection Obj");
    serv_conn->setRequestId(0);
    TSDebug(PLUGIN_NAME, "[Server:%s] Adding connection back to connection pool. QueueLength:%lu", __FUNCTION__,
            pending_list.size());
    serv_conn->setState(ServerConnection::READY);
    _connection_pool->addConnection(serv_conn);
    if (!pending_list.empty()) {
      int size = _connection_pool->checkAvailability();
      TSDebug(PLUGIN_NAME, "[Server:%s] Connection Pool Available Size:%d", __FUNCTION__, size);
      if (size) {
        TSDebug(PLUGIN_NAME, "[Server:%s] Processing pending list", __FUNCTION__);
        ServerIntercept *intercept = pending_list.front();
        pending_list.pop();
        Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
        transaction.addPlugin(intercept);
        server()->connect(intercept);
        transaction.resume();
      } else {
        TSDebug(PLUGIN_NAME, "[Server:%s] Connection Not available. QueueSize: %lu", __FUNCTION__, pending_list.size());
      }
    }
    // delete serv_conn;
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
  // server_conn->setState(ServerConnection::WRITE);
  bool endflag = false;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
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
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = false;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
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
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = true;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
  // server_conn->setState(ServerConnection::WRITE_COMPLETE);
  // TSContCall(TSVIOContGet(server_conn->writeio.vio), TS_EVENT_VCONN_WRITE_COMPLETE, server_conn->writeio.vio);
  server_conn->readio.read(server_conn->vc_, server_conn->contp());
}

const uint
Server::connect(ServerIntercept *intercept)
{
  const uint request_id = UniqueRequesID::getNext();

  intercept->setRequestId(request_id);

  ServerConnection *conn = _connection_pool->getAvailableConnection();

  TSDebug(PLUGIN_NAME, "[Server:%s]: Connection Available...vc_: %p", __FUNCTION__, conn->vc_);
  conn->setRequestId(request_id);

  // TODO: Check better way to do it
  _intercept_list[request_id] = std::make_tuple(intercept, conn);
  conn->setState(ServerConnection::INUSE);
  conn->createFCGIClient(intercept->_txn);

  return request_id;
}

// const uint
// Server::connect(ServerIntercept *intercept)
// {
//   const uint request_id = UniqueRequesID::getNext();

//   intercept->setRequestId(request_id);

//   ServerConnection *conn = _connection_pool->getAvailableConnection();

//   if (conn) {
//     TSDebug(PLUGIN_NAME, "[Server:%s]: Connection Available...vc_: %p", __FUNCTION__, conn->vc_);
//     conn->setRequestId(request_id);

//     // TODO: Check better way to do it
//     _intercept_list[request_id] = std::make_tuple(intercept, conn);
//     conn->createFCGIClient(intercept->_txn);

//   } else {
//     TSDebug(PLUGIN_NAME, "[Server:%s]: Adding to pending list.", __FUNCTION__);
//     // TODO: put in pending list
//     pending_list.push(intercept);
//     // _connection_pool->setupNewConnection();
//     // ServerConnection *conn = _connection_pool->getAvailableConnection();
//     // conn->setRequestId(request_id);
//     // _intercept_list[request_id] = std::make_tuple(intercept, conn);
//     // conn->createFCGIClient(intercept->_txn);
//   }

//   return request_id;
// }

int
Server::checkAvailability()
{
  int size = _connection_pool->checkAvailability();
  return size;
}
void
Server::createConnections()
{
  _connection_pool = new ConnectionPool(this, handlePHPConnectionEvents);
}
