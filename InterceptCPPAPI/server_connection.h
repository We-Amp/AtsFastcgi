#ifndef _SERVERCONNECTION_H_
#define _SERVERCONNECTION_H_

#include <string>

#include "ts/ts.h"

namespace ats_plugin
{
class Server;
class FCGIClientRequest;
struct ServerConnectionInfo;
struct InterceptIOChannel {
  TSVIO vio;
  TSIOBuffer iobuf;
  TSIOBufferReader reader;
  int total_bytes_written;
  bool readEnable;
  InterceptIOChannel();
  ~InterceptIOChannel();

  void read(TSVConn vc, TSCont contp);
  void write(TSVConn vc, TSCont contp);
  void phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size, bool endflag);
};

class ServerConnection
{
public:
  ServerConnection(Server *server, TSEventFunc funcp);
  ~ServerConnection();

  enum State { INITIATED, READY, INUSE, CLOSED };

  void
  setState(State state)
  {
    _state = state;
  }
  State
  getState()
  {
    return _state;
  }

  void
  setRequestId(uint requestId)
  {
    _requestId = requestId;
  }
  uint
  requestId()
  {
    return _requestId;
  }

  void createFCGIClient(TSHttpTxn txn);
  void releaseFCGIClient();
  FCGIClientRequest *
  fcgiRequest()
  {
    return _fcgiRequest;
  }

  TSCont &
  contp()
  {
    return _contp;
  }

public:
  TSVConn vc_;
  std::string clientData, clientRequestBody, serverResponse;
  InterceptIOChannel readio;
  InterceptIOChannel writeio;
  FCGIClientRequest *_fcgiRequest;

private:
  void createConnection();
  State _state;
  Server *_server;
  TSEventFunc _funcp;
  TSCont _contp;
  ServerConnectionInfo *_sConnInfo;
  uint _requestId;
};
}

#endif /*_SERVERCONNECTION_H_*/
