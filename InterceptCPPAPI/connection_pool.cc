
#include "connection_pool.h"

#include "server.h"
#include "server_connection.h"

#include "ats_mod_intercept.h"
#include "ts/ts.h"

using namespace ats_plugin;
ConnectionPool::ConnectionPool(Server *server, TSEventFunc funcp) : _server(server), _funcp(funcp)
{
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
  ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  uint maxConn                          = gConfig->getMaxConnLength();

  if (!_available_connections.empty() && _connections.size() == maxConn) {
    TSDebug(PLUGIN_NAME, "%s: available connections %ld", __FUNCTION__, _available_connections.size());
    ServerConnection *conn = _available_connections.front();
    _available_connections.pop_front();
    // TODO ASSERT(conn->getState() == ServerConnection::READY)
    conn->setState(ServerConnection::INUSE);
    TSDebug(PLUGIN_NAME, "%s: Connection from available pool, %p", __FUNCTION__, conn);
    return conn;
  }

  if (_connections.size() >= maxConn) {
    TSDebug(PLUGIN_NAME, "%s: Max conn reached, need to queue, maxConn: %d", __FUNCTION__, maxConn);
    return nullptr;
  }

  ServerConnection *conn = new ServerConnection(_server, _funcp);
  addConnection(conn);
  TSDebug(PLUGIN_NAME, "%s: New Connection created, %p", __FUNCTION__, conn);
  return conn;
}

void
ConnectionPool::addConnection(ServerConnection *connection)
{
  _connections.push_back(connection);
}

void
ConnectionPool::reuseConnection(ServerConnection *connection)
{
  connection->readio.readEnable  = false;
  connection->writeio.readEnable = false;

  connection->setState(ServerConnection::READY);
  _available_connections.push_back(connection);
  TSDebug(PLUGIN_NAME, "%s: Connection added, available connections %ld", __FUNCTION__, _available_connections.size());
}

void
ConnectionPool::connectionClosed(ServerConnection *connection)
{
  _available_connections.remove(connection);
  _connections.remove(connection);
  delete connection;
}
