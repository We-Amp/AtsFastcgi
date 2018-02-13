#ifndef __SERVER_H_
#define __SERVER_H_

#include "server_intercept.h"
#include "request_queue.h"
#include <map>
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

class Server
{
public:
  static Server *server();

  Server();
  ~Server();

  const uint connect(ServerIntercept *intercept);

  ServerConnection *getServerConnection(uint request_id);

  ServerIntercept *getIntercept(uint request_id);
  void removeIntercept(uint request_id);

  bool writeRequestHeader(uint request_id);
  bool writeRequestBody(uint request_id, const std::string &data);
  bool writeRequestBodyComplete(uint request_id);

  Server(Server const &) = delete;
  void operator=(Server const &) = delete;

  RequestQueue *pendingReqQueue;
  void reuseConnection(ServerConnection *server_conn);
  void connectionClosed(ServerConnection *server_conn);

  void reConnect(ServerConnection *server_conn, uint request_id);

  ConnectionPool *
  getConnectionPool()
  {
    return _connection_pool;
  }

private:
  void createConnectionPool();
  void initiateBackendConnection(ServerIntercept *intercept, ServerConnection *conn);

  std::map<uint, std::tuple<ServerIntercept *, ServerConnection *>> _intercept_list;

  ConnectionPool *_connection_pool;
  TSMutex _reqId_mutex;
  TSMutex _intecept_mutex;
};
}
#endif /*__SERVER_H_*/
