#include "ts/ts.h"
#include "ats_mod_intercept.h"
#include "connection_pool.h"
#include "server_connection.h"
#include "server.h"

using namespace ats_plugin;
ConnectionPool::ConnectionPool(Server *server, TSEventFunc funcp)
  : _server(server), _funcp(funcp), _availableConn_mutex(TSMutexCreate()), _conn_mutex(TSMutexCreate())
{
  ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  _maxConn                              = gConfig->getMaxConnLength();
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
  TSMutexLock(_availableConn_mutex);
  if (!_available_connections.empty() && _connections.size() >= _maxConn) {
    TSDebug(PLUGIN_NAME, "%s: available connections %ld", __FUNCTION__, _available_connections.size());
    conn = _available_connections.front();
    _available_connections.pop_front();
    // TODO ASSERT(conn->getState() == ServerConnection::READY)
    conn->setState(ServerConnection::READY);
    TSDebug(PLUGIN_NAME, "%s: Connection from available pool, %p", __FUNCTION__, conn);
  }

  if (_connections.size() < _maxConn) {
    TSDebug(PLUGIN_NAME, "%s: Max conn reached, need to queue, maxConn: %d", __FUNCTION__, _maxConn);
    conn = new ServerConnection(_server, _funcp);
    addConnection(conn);
  }
  TSMutexUnlock(_availableConn_mutex);
  return conn;
}

void
ConnectionPool::addConnection(ServerConnection *connection)
{
  TSMutexLock(_conn_mutex);
  _connections.push_back(connection);
  TSMutexUnlock(_conn_mutex);
}

void
ConnectionPool::reuseConnection(ServerConnection *connection)
{
  connection->readio.readEnable  = false;
  connection->writeio.readEnable = false;

  connection->setState(ServerConnection::READY);
  TSMutexLock(_availableConn_mutex);
  _available_connections.push_back(connection);
  TSDebug(PLUGIN_NAME, "%s: Connection added, available connections %ld", __FUNCTION__, _available_connections.size());
  TSMutexUnlock(_availableConn_mutex);
}

void
ConnectionPool::connectionClosed(ServerConnection *connection)
{
  TSMutexLock(_availableConn_mutex);
  _available_connections.remove(connection);
  TSMutexUnlock(_availableConn_mutex);

  TSMutexLock(_conn_mutex);
  _connections.remove(connection);
  TSMutexUnlock(_conn_mutex);

  delete connection;
}
