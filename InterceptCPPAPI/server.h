#ifndef __SERVER_H_
#define __SERVER_H_

#include "server_intercept.h"
#include <map>
#include <queue>
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

  void writeRequestHeader(uint request_id);
  void writeRequestBody(uint request_id, const std::string &data);
  void writeRequestBodyComplete(uint request_id);

  Server(Server const &) = delete;
  void operator=(Server const &) = delete;

  int checkAvailability();
  std::queue<ServerIntercept *> pending_list;

  void removeConnection(ServerConnection *server_conn);
  void reuseConnection(ServerConnection *server_conn);
  void connectionClosed(ServerConnection *server_conn);

  void reConnect(uint request_id);

private:
  void createConnectionPool();
  void initiateBackendConnection(ServerIntercept *intercept, uint request_id);

  std::map<uint, std::tuple<ServerIntercept *, ServerConnection *>> _intercept_list;

  ConnectionPool *_connection_pool;
};
}
#endif /*__SERVER_H_*/
