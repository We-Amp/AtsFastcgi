
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
  if (data.size()) {
    // std::cout << "Output: " << data << std::endl;
    intercept->writeResponseChunkToATS(data);
  }
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
    server_connection->setState(ServerConnection::READY);
    TSDebug(PLUGIN_NAME, "%s: New Connection success, %p", __FUNCTION__, server_connection);
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (intercept) {
      server_connection->createFCGIClient(intercept->_txn);
      if (intercept->dataBuffered)
        intercept->resumeIntercept();
    }

  } break;

  case TS_EVENT_NET_CONNECT_FAILED: {
    // Try reconnection with new connection.
    // TODO: Have to stop trying to reconnect after some tries
    server->reConnect(server_connection->requestId());
    server->connectionClosed(server_connection);
    return TS_EVENT_NONE;
  } break;

  case TS_EVENT_VCONN_READ_READY: {
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (intercept && interceptTransferData(intercept, server_connection)) {
      server_connection->setState(ServerConnection::COMPLETE);
      intercept->setResponseOutputComplete();
      TSStatIntIncrement(InterceptGlobal::respBegId, 1);
      // TSContCall(TSVIOContGet(server_connection->readio.vio), TS_EVENT_VCONN_READ_COMPLETE, server_connection->readio.vio);
    }
  } break;

  case TS_EVENT_VCONN_READ_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: ResponseComplete...Sending Response to client stream. _request_id: %d", __FUNCTION__,
            server_connection->requestId());
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    server_connection->setState(ServerConnection::COMPLETE);
    intercept->setResponseOutputComplete();
    TSStatIntIncrement(InterceptGlobal::respBegId, 1);
  } break;

  case TS_EVENT_VCONN_WRITE_READY: {
    if (server_connection->writeio.readEnable) {
      TSContCall(TSVIOContGet(server_connection->writeio.vio), TS_EVENT_VCONN_WRITE_COMPLETE, server_connection->writeio.vio);
    }

  } break;

  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: Start Reading from Server now...", __FUNCTION__);
    TSStatIntIncrement(InterceptGlobal::reqEndId, 1);
    server_connection->readio.read(server_connection->vc_, server_connection->contp());
  } break;

  case TS_EVENT_VCONN_EOS: {
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (!server_connection->writeio.readEnable) {
      TSDebug(PLUGIN_NAME, "[%s]: EOS Request Failed. _request_id: %d, connection: %p,maxConn: %d, requestCount: %d", __FUNCTION__,
              server_connection->requestId(), server_connection, server_connection->maxRequests(),
              server_connection->requestCount());

      server->reConnect(server_connection->requestId());
      server_connection->setState(ServerConnection::CLOSED);
      server->connectionClosed(server_connection);
      break;
    }

    if (intercept && !intercept->getOutputCompleteState()) {
      TSDebug(PLUGIN_NAME, "[%s]: EOS intercept->setResponseOutputComplete", __FUNCTION__);
      server_connection->setState(ServerConnection::CLOSED);
      Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
      transaction.error("Internal server error");
    }

    server->connectionClosed(server_connection);
  } break;

  case TS_EVENT_ERROR: {
    TSVConnAbort(server_connection->vc_, 1);
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (intercept) {
      TSDebug(PLUGIN_NAME, "[%s]:ERROR  intercept->setResponseOutputComplete", __FUNCTION__);
      server_connection->setState(ServerConnection::CLOSED);
      Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
      transaction.setStatusCode(HTTP_STATUS_BAD_GATEWAY);
      transaction.error("Internal server error");
    }

    server->connectionClosed(server_connection);
  } break;

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

Server::Server() : _reqId_mutex(TSMutexCreate()), _intecept_mutex(TSMutexCreate())
{
  createConnectionPool();
  pendingReqQueue = new RequestQueue();
}

ServerIntercept *
Server::getIntercept(uint request_id)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    TSMutexUnlock(_intecept_mutex);
    return std::get<0>(itr->second);
  }
  TSMutexUnlock(_intecept_mutex);
  return nullptr;
}

ServerConnection *
Server::getServerConnection(uint request_id)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    TSMutexUnlock(_intecept_mutex);
    return std::get<1>(itr->second);
  }
  TSMutexUnlock(_intecept_mutex);
  return nullptr;
}

void
Server::removeIntercept(uint request_id)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    ServerConnection *serv_conn = std::get<1>(itr->second);
    _intercept_list.erase(itr);
    TSMutexUnlock(_intecept_mutex);

    serv_conn->releaseFCGIClient();
    TSDebug(PLUGIN_NAME, "[Server:%s] Reset and Add connection back to connection pool. ReqQueueLength:%d", __FUNCTION__,
            pendingReqQueue->getSize());

    if (serv_conn->maxRequests() > serv_conn->requestCount()) {
      TSDebug(PLUGIN_NAME, "[Server:%s] Queue Empty. Reusing connection.Max_req: %d ,Req_count: %d", __FUNCTION__,
              serv_conn->maxRequests(), serv_conn->requestCount());
      _connection_pool->reuseConnection(serv_conn);
    } else {
      // destroy the connection and setup new conn to process pending list
      TSDebug(PLUGIN_NAME, "[Server:%s] Max Requests reached. Max_req: %d ,Req_count: %d", __FUNCTION__, serv_conn->maxRequests(),
              serv_conn->requestCount());
      serv_conn->setState(ServerConnection::CLOSED);
      connectionClosed(serv_conn);
    }
    ServerIntercept *intercept = pendingReqQueue->popFromQueue();
    if (intercept) {
      TSDebug(PLUGIN_NAME, "[Server:%s] Processing pending list", __FUNCTION__);
      connect(intercept);
    }
    return;
  }
  TSMutexLock(_intecept_mutex);
  return;
}

bool
Server::writeRequestHeader(uint request_id)
{
  ServerConnection *server_conn = getServerConnection(request_id);
  if (!server_conn) {
    return false;
  }
  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Header: request_id: %d,ServerConn: %p", __FUNCTION__, request_id, server_conn);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
  unsigned char *clientReq;
  int reqLen = 0;

  // TODO: possibly move all this as one function in server_connection
  fcgiRequest->createBeginRequest();
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = false;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);

  return true;
}

bool
Server::writeRequestBody(uint request_id, const string &data)
{
  ServerConnection *server_conn = getServerConnection(request_id);
  if (!server_conn) {
    return false;
  }
  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Body: request_id: %d,Server_conn: %p", __FUNCTION__, request_id, server_conn);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
  // TODO: possibly move all this as one function in server_connection
  unsigned char *clientReq;
  int reqLen            = 0;
  fcgiRequest->postData = data;
  fcgiRequest->postBodyChunk();
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = false;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
  return true;
}

bool
Server::writeRequestBodyComplete(uint request_id)
{
  ServerConnection *server_conn = getServerConnection(request_id);
  if (!server_conn) {
    return false;
  }
  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Complete: request_id: %d,Server_conn: %p", __FUNCTION__, request_id,
          server_conn);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();

  // TODO: possibly move all this as one function in server_connection
  unsigned char *clientReq;
  int reqLen = 0;
  fcgiRequest->emptyParam();
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = true;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);

  return true;
}

const uint
Server::connect(ServerIntercept *intercept)
{
  ServerConnection *conn = nullptr;
  conn                   = _connection_pool->getAvailableConnection();
  if (conn) {
    initiateBackendConnection(intercept, conn);
  } else {
    pendingReqQueue->addToQueue(intercept);
    TSDebug(PLUGIN_NAME, "[Server:%s] : Added to RequestQueue. QueueSize: %d", __FUNCTION__, pendingReqQueue->getSize());
  }

  return 0;
}

void
Server::reConnect(uint request_id)
{
  ServerIntercept *intercept = getIntercept(request_id);

  TSDebug(PLUGIN_NAME, "[Server:%s]: Initiating reconnection...", __FUNCTION__);
  if (intercept) {
    TSMutexLock(_intecept_mutex);
    _intercept_list.erase(request_id);
    TSMutexUnlock(_intecept_mutex);
    if (!intercept->clientAborted)
      connect(intercept);
  }
}

void
Server::initiateBackendConnection(ServerIntercept *intercept, ServerConnection *conn)
{
  TSMutexLock(_reqId_mutex);
  const uint request_id = UniqueRequesID::getNext();
  TSMutexUnlock(_reqId_mutex);

  intercept->setRequestId(request_id);
  conn->setRequestId(request_id);
  TSMutexLock(_intecept_mutex);
  _intercept_list[request_id] = std::make_tuple(intercept, conn);
  TSMutexUnlock(_intecept_mutex);

  if (conn->getState() != ServerConnection::READY) {
    TSDebug(PLUGIN_NAME, "[Server: %s] Setting up a new php Connection..", __FUNCTION__);
    conn->createConnection();
  } else {
    conn->createFCGIClient(intercept->_txn);
    if (intercept->dataBuffered)
      intercept->resumeIntercept();
  }
  Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
  transaction.addPlugin(intercept);
  transaction.resume();
}

void
Server::createConnectionPool()
{
  _connection_pool = new ConnectionPool(this, handlePHPConnectionEvents);
}

void
Server::reuseConnection(ServerConnection *server_conn)
{
  _connection_pool->reuseConnection(server_conn);
}

void
Server::connectionClosed(ServerConnection *server_conn)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(server_conn->requestId());
  if (itr != _intercept_list.end()) {
    _intercept_list.erase(itr);
  }
  TSMutexUnlock(_intecept_mutex);

  _connection_pool->connectionClosed(server_conn);
}
