#ifndef __SERVER_H_
#define __SERVER_H_

#include "server_intercept.h"
#include "request_queue.h"
#include <map>
#include <pthread.h>
namespace ats_plugin
{
class ServerIntercept;
class ServerConnection;
class ConnectionPool;
class Server;

class UniqueRequesID
{
public:
  static const uint
  getNext()
  {
    // TODO add mutext here
    return _id++;
  }

private:
  static uint _id;
};

struct ServerConnectionInfo {
public:
  ServerConnectionInfo(Server *fServer, ServerConnection *server_conn) : server(fServer), server_connection(server_conn) {}
  ~ServerConnectionInfo() {}
  Server *server;
  ServerConnection *server_connection;
};

class ThreadData
{
public:
  ThreadData(ThreadData const &) = delete;
  void operator=(ThreadData const &) = delete;

  ThreadData(Server *server) : _server(server)
  {
    tid = pthread_self();
    createConnectionPool(_server);
    _pendingReqQueue = new RequestQueue();
  }
  ~ThreadData()
  {
    delete _pendingReqQueue;
    // delete _connection_pool;
  }
  void createConnectionPool(Server *server);

  ConnectionPool *
  getConnectionPool()
  {
    return _connection_pool;
  }

  RequestQueue *
  getRequestQueue()
  {
    return _pendingReqQueue;
  }

private:
  pthread_t tid;
  Server *_server;
  RequestQueue *_pendingReqQueue;
  ConnectionPool *_connection_pool;
};

class Server
{
public:
  static Server *server();
  Server(Server const &) = delete;
  void operator=(Server const &) = delete;

  Server();
  ~Server();

  bool setupThreadLocalStorage();
  const uint connect(ServerIntercept *intercept);
  void reConnect(uint request_id);
  ServerConnection *getServerConnection(uint request_id);

  ServerIntercept *getIntercept(uint request_id);
  void removeIntercept(uint request_id);

  bool writeRequestHeader(uint request_id);
  bool writeRequestBody(uint request_id, const std::string &data);
  bool writeRequestBodyComplete(uint request_id);

  void connectionClosed(ServerConnection *server_conn);

private:
  void initiateBackendConnection(ServerIntercept *intercept, ServerConnection *conn);
  std::map<uint, std::tuple<ServerIntercept *, ServerConnection *>> _intercept_list;
  TSMutex _reqId_mutex;
  TSMutex _intecept_mutex;
};
}
#endif /*__SERVER_H_*/
