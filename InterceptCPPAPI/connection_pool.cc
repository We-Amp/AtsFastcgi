
#include "connection_pool.h"

#include "server.h"
#include "server_connection.h"

#include "ats_mod_intercept.h"
#include "ts/ts.h"

using namespace ats_plugin;
ConnectionPool::ConnectionPool(Server *server, TSEventFunc funcp) : _server(server), _funcp(funcp)
{
  createConnections();
}

ConnectionPool::~ConnectionPool()
{
  TSDebug(PLUGIN_NAME, "Destroying connectionPool Obj...");
}

int
ConnectionPool::checkAvailability()
{
  return _available_connections.size();
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
      _available_connections.remove(conn);
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
ConnectionPool::setupNewConnection()
{
  ServerConnection *sConn;
  sConn = new ServerConnection(_server, _funcp);
  addConnection(sConn);
}
void
ConnectionPool::createConnections()
{
  int i = 0;
  ServerConnection *sConn;
  // TODO: Read config and available connections and servers and create the connections accordingly
  ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  int maxConn                           = gConfig->getMaxConnLength();
  // int minConn                           = gConfig->getMinConnLength();
  // int reqQueueLen                       = gConfig->getRequestQueueSize();

  while (i < maxConn) {
    sConn = new ServerConnection(_server, _funcp);
    addConnection(sConn);
    i++;
  }
}
