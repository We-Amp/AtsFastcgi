#ifndef _SERVERCONNECTION_H_
#define _SERVERCONNECTION_H_

#include <string>

#include "ts/ts.h"

class Server;
namespace FCGIClient
{
class FCGIClientRequest;
}

struct InterceptIOChannel {
  TSVIO vio;
  TSIOBuffer iobuf;
  TSIOBufferReader reader;
  int total_bytes_written;

  InterceptIOChannel();
  ~InterceptIOChannel();

  void read(TSVConn vc, TSCont contp);
  void write(TSVConn vc, TSCont contp);
  void phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size);
};

class ServerConnection
{
public:
  ServerConnection(Server *server, TSEventFunc funcp);
  ~ServerConnection();

  enum State { INITIATED, READY, INUSE };

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

  FCGIClient::FCGIClientRequest *
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
  FCGIClient::FCGIClientRequest *_fcgiRequest;

private:
  void createConnection();
  State _state;
  Server *_server;
  TSEventFunc _funcp;
  TSCont _contp;
  uint _requestId;
};

#endif /*_SERVERCONNECTION_H_*/
