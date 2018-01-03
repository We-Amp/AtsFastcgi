
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

  switch (event) {
  case TS_EVENT_NET_CONNECT: {
    server_connection->vc_ = (TSVConn)edata;

    server_connection->setState(ServerConnection::INUSE);

    TSDebug(PLUGIN_NAME, "%s: New Connection succesful, %p", __FUNCTION__, server_connection);
    TSDebug(PLUGIN_NAME, "Connected to FCGI Server. vc_ : %p \t _contp: %p \tWriteIO.vio :%p \t ReadIO.vio :%p ",
            server_connection->vc_, server_connection->contp(), server_connection->writeio.vio, server_connection->readio.vio);
    break;
  }

  case TS_EVENT_NET_CONNECT_FAILED: {
    TSDebug(PLUGIN_NAME, "[%s] : TSNetConnect Failed.", __FUNCTION__);

    // Try reconnection with new connection.
    // TODO: Have to stop trying to reconnect after some tries
    server->reConnect(server_connection, server_connection->requestId());

    server->removeConnection(server_connection);

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

      TSStatIntIncrement(InterceptGlobal::respId, 1);
    }
    break;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    TSDebug(PLUGIN_NAME, "[%s] : Write Ready", __FUNCTION__);
    if (server_connection->writeio.readEnable) {
      TSContCall(TSVIOContGet(server_connection->writeio.vio), TS_EVENT_VCONN_WRITE_COMPLETE, server_connection->writeio.vio);
    }

  } break;
  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: Start Reading from Server now...", __FUNCTION__);
    server_connection->readio.read(server_connection->vc_, server_connection->contp());
  } break;
  case TS_EVENT_VCONN_READ_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: Server Read Complete...", __FUNCTION__);
    // TSVConnClose(server_connection->vc_);;
  } break;
  case TS_EVENT_VCONN_EOS: {
    TSDebug(PLUGIN_NAME, "[%s]: EOS reached.", __FUNCTION__);
    server->connectionClosed(server_connection);
  } break;
  case TS_EVENT_ERROR: {
    TSDebug(PLUGIN_NAME, "HandlePHPConnectionEvents: Connection Disconnected...Error...");
    TSVConnAbort(server_connection->vc_, 1);
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    // sending output complete as no support function provided to abort client connection
    if (intercept)
      intercept->setResponseOutputComplete();

    server->connectionClosed(server_connection);

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

Server::Server() : _reqId_mutex(TSMutexCreate()), _conn_mutex(TSMutexCreate())
{
  createConnectionPool();
  pendingReqQueue = new RequestQueue();
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
    ServerConnection *serv_conn = std::get<1>(itr->second);
    _intercept_list.erase(itr);
    serv_conn->setRequestId(0);
    TSDebug(PLUGIN_NAME, "[Server:%s] Resetting and Adding connection back to connection pool. ReqQueueLength:%d", __FUNCTION__,
            pendingReqQueue->getSize());
    _connection_pool->reuseConnection(serv_conn);
  }

  if (!pendingReqQueue->isQueueEmpty()) {
    ServerIntercept *intercept = pendingReqQueue->removeFromQueue();
    TSDebug(PLUGIN_NAME, "[Server:%s] Processing pending list", __FUNCTION__);
    connect(intercept);
  }
}

void
Server::writeRequestHeader(uint request_id)
{
  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Header: request_id: %d", __FUNCTION__, request_id);
  ServerConnection *server_conn = getServerConnection(request_id);

  if (server_conn && server_conn->getState() == ServerConnection::INUSE) {
    FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
    unsigned char *clientReq;
    int reqLen = 0;

    // TODO: possibly move all this as one function in server_connection
    fcgiRequest->createBeginRequest();
    clientReq = fcgiRequest->addClientRequest(reqLen);
    // server_conn->setState(ServerConnection::WRITE);
    bool endflag = false;
    server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
    return;
  } else {
    TSDebug(PLUGIN_NAME, "%s: Should write to buffer", __FUNCTION__);
  }
}

void
Server::writeRequestBody(uint request_id, const string &data)
{
  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Body: request_id: %d", __FUNCTION__, request_id);
  ServerConnection *server_conn = getServerConnection(request_id);

  if (server_conn && server_conn->getState() == ServerConnection::INUSE) {
    FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();

    // TODO: possibly move all this as one function in server_connection
    unsigned char *clientReq;
    int reqLen            = 0;
    fcgiRequest->postData = data;
    fcgiRequest->postBodyChunk();
    clientReq    = fcgiRequest->addClientRequest(reqLen);
    bool endflag = false;
    server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
    return;
  } else {
    TSDebug(PLUGIN_NAME, "%s: Should write to buffer", __FUNCTION__);
  }
}

void
Server::writeRequestBodyComplete(uint request_id)
{
  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Complete: request_id: %d", __FUNCTION__, request_id);
  ServerConnection *server_conn = getServerConnection(request_id);
  if (server_conn && server_conn->getState() == ServerConnection::INUSE) {
    FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();

    // TODO: possibly move all this as one function in server_connection
    unsigned char *clientReq;
    int reqLen = 0;
    fcgiRequest->emptyParam();
    clientReq    = fcgiRequest->addClientRequest(reqLen);
    bool endflag = true;
    server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
  } else {
    TSDebug(PLUGIN_NAME, "%s: Should write to buffer", __FUNCTION__);
  }
  // server_conn->readio.read(server_conn->vc_, server_conn->contp());
}

const uint
Server::connect(ServerIntercept *intercept)
{
  TSMutexLock(_conn_mutex);
  ServerConnection *conn = _connection_pool->getAvailableConnection();
  TSMutexUnlock(_conn_mutex);

  if (conn != nullptr) {
    TSDebug(PLUGIN_NAME, "[Server:%s]: Connection Available...vc_: %p", __FUNCTION__, conn->vc_);
    initiateBackendConnection(intercept, conn);
  } else {
    pendingReqQueue->addToQueue(intercept);
    TSDebug(PLUGIN_NAME, "[Server:%s] : Added to RequestQueue. QueueSize: %d", __FUNCTION__, pendingReqQueue->getSize());
  }

  return 0;
}

void
Server::reConnect(ServerConnection *server_conn, uint request_id)
{
  ServerIntercept *intercept = getIntercept(request_id);
  _intercept_list.erase(request_id);
  TSDebug(PLUGIN_NAME, "[Server:%s]: Initiating reconnection...", __FUNCTION__);
  connect(intercept);
}

void
Server::initiateBackendConnection(ServerIntercept *intercept, ServerConnection *conn)
{
  Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
  transaction.addPlugin(intercept);

  TSMutexLock(_reqId_mutex);
  const uint request_id = UniqueRequesID::getNext();
  TSMutexUnlock(_reqId_mutex);
  intercept->setRequestId(request_id);
  conn->setRequestId(request_id);
  // TODO: Check better way to do it
  _intercept_list[request_id] = std::make_tuple(intercept, conn);
  conn->createFCGIClient(intercept->_txn);
  transaction.resume();
}

int
Server::checkAvailability()
{
  int size = _connection_pool->checkAvailability();
  return size;
}

void
Server::createConnectionPool()
{
  _connection_pool = new ConnectionPool(this, handlePHPConnectionEvents);
}

void
Server::removeConnection(ServerConnection *server_conn)
{
  auto itr = _intercept_list.find(server_conn->requestId());
  if (itr != _intercept_list.end()) {
    _intercept_list.erase(itr);
  }

  _connection_pool->removeConnection(server_conn);
}

void
Server::reuseConnection(ServerConnection *server_conn)
{
  _connection_pool->reuseConnection(server_conn);
}

void
Server::connectionClosed(ServerConnection *server_conn)
{
  _connection_pool->connectionClosed(server_conn);
}
