
#include "connection_pool.h"

#include "server.h"
#include "server_connection.h"

#include "ts/ts.h"

ConnectionPool::ConnectionPool(Server *server, TSEventFunc funcp) : _server(server), _funcp(funcp)
{
}

ConnectionPool::~ConnectionPool()
{
}

ServerConnection *
ConnectionPool::getAvailableConnection()
{
  if (_available_connections.empty())
    return nullptr;

  // TODO: CHeck if we need this loop, for now connections are in list only if READY
  ServerConnection *conn = nullptr;
  for (auto itr = _available_connections.begin(); itr != _available_connections.end(); itr++) {
    conn = *itr;
    if (conn->getState() == ServerConnection::READY) {
      return conn;
    }
  }

  return conn;
}

void
ConnectionPool::addConnection(ServerConnection *connection)
{
  _available_connections.push_back(connection);
}

void
ConnectionPool::createConnections()
{
  int i = 0;
  // TODO: Read config and available connections and servers and create the connections accordingly
  while (i < 10) {
    new ServerConnection(_server, _funcp);
  }
}
