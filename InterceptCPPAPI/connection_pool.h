#ifndef _CONNECTION_POOL_H_
#define _CONNECTION_POOL_H_

#include <list>

#include "ts/ts.h"

class Server;
class ServerConnection;

// Connection Pool class which creates a pool of connections when certain
// threshold is reached. Possibly used connections also can be readded to pool
// if connection does not close.

class ConnectionPool
{
public:
  ConnectionPool(Server *server, TSEventFunc funcp);
  ~ConnectionPool();

  ServerConnection *getAvailableConnection();
  void addConnection(ServerConnection *);

private:
  void createConnections();

  Server *_server;
  TSEventFunc _funcp;
  std::list<ServerConnection *> _available_connections;
};

#endif /*_CONNECTION_POOL_H_*/
