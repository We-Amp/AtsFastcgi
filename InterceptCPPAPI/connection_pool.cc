#include "ts/ts.h"
#include "ats_mod_intercept.h"
#include "connection_pool.h"
#include "server_connection.h"
#include "server.h"

using namespace ats_plugin;
ConnectionPool::ConnectionPool(Server *server, TSEventFunc funcp)
  : _server(server), _funcp(funcp), _availableConn_mutex(TSMutexCreate()), _conn_mutex(TSMutexCreate())
{
  // TODO: For now we are setting maxConn as hard coded values
  ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  _maxConn                              = gConfig->getMaxConnLength() / 6;
}

ConnectionPool::~ConnectionPool()
{
  TSDebug(PLUGIN_NAME, "Destroying connectionPool Obj...");
  TSMutexDestroy(_availableConn_mutex);
  TSMutexDestroy(_conn_mutex);
}

int
ConnectionPool::checkAvailability()
{
  return _available_connections.size();
}

ServerConnection *
ConnectionPool::getAvailableConnection()
{
  ServerConnection *conn = nullptr;
  if (!_available_connections.empty() && _connections.size() >= _maxConn) {
    TSDebug(PLUGIN_NAME, "%s: available connections %ld", __FUNCTION__, _available_connections.size());
    conn = _available_connections.front();
    _available_connections.pop_front();
    conn->setState(ServerConnection::READY);
    TSDebug(PLUGIN_NAME, "%s: available connections %ld. Connection from available pool, %p", __FUNCTION__,
            _available_connections.size(), conn);
  }

  if (_connections.size() < _maxConn) {
    TSDebug(PLUGIN_NAME, "%s: Setting up new connection, maxConn: %d", __FUNCTION__, _maxConn);
    conn = new ServerConnection(_server, _funcp);
    addConnection(conn);
  }
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
