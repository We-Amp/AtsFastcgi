#ifndef _CONNECTION_POOL_H_
#define _CONNECTION_POOL_H_

#include <list>
#include "ts/ts.h"

namespace ats_plugin
{
class Server;
class ServerConnection;

// Connection Pool class which creates a pool of connections when certain
// threshold is reached. Possibly used connections also can be re-added to pool
// if connection does not close.

class ConnectionPool
{
public:
  ConnectionPool(Server *server, TSEventFunc funcp);
  ~ConnectionPool();

  ServerConnection *getAvailableConnection();
  int checkAvailability();

  void addConnection(ServerConnection *);
  void reuseConnection(ServerConnection *connection);
  void connectionClosed(ServerConnection *connection);

private:
  void createConnections();
  uint _maxConn;
  Server *_server;
  TSEventFunc _funcp;
  TSMutex _availableConn_mutex, _conn_mutex;
  std::list<ServerConnection *> _available_connections;
  std::list<ServerConnection *> _connections;
};
}
#endif /*_CONNECTION_POOL_H_*/
