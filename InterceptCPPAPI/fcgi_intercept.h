#ifndef _FCGI_INTERCEPT_H_
#define _FCGI_INTERCEPT_H_

#include "ats_fcgi_client.h"
#include "ats_mod_fcgi.h"
#include "atscppapi/Transaction.h"
#include "atscppapi/TransactionPlugin.h"
#include "ts/ink_defs.h"
#include "ts/ts.h"
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/utils.h>
#include <iostream>
#include <iterator>
#include <map>
#include <netinet/in.h>
#include <string.h>

#define PORT 60000

using std::cout;
using std::endl;
using std::string;
using namespace atscppapi;

using namespace FCGIClient;

struct InterceptIOChannel {
  TSVIO vio;
  TSIOBuffer iobuf;
  TSIOBufferReader reader;

  InterceptIOChannel() : vio(nullptr), iobuf(nullptr), reader(nullptr) {}
  ~InterceptIOChannel()
  {
    if (this->reader) {
      TSIOBufferReaderFree(this->reader);
    }

    if (this->iobuf) {
      TSIOBufferDestroy(this->iobuf);
    }
  }

  void
  read(TSVConn vc, TSCont contp)
  {
    TSReleaseAssert(this->vio == nullptr);
    TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
    TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));

    this->vio = TSVConnRead(vc, contp, this->iobuf, INT64_MAX);
  }

  void
  write(TSVConn vc, TSCont contp)
  {
    TSReleaseAssert(this->vio == nullptr);
    TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
    TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));

    this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }

  void
  phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size)
  {
    TSReleaseAssert(this->vio == nullptr);
    TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
    TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));
    int num_bytes_written = TSIOBufferWrite(this->iobuf, (const void *)buf, data_size);
    if (num_bytes_written != data_size) {
      TSError("%s Error while writing to buffer! Attempted %d bytes but only wrote %d bytes", PLUGIN_NAME, data_size,
              num_bytes_written);
    }
    this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }
};

class InterceptIO
{
public:
  TSVConn vc_;
  TSHttpTxn txn_;
  // TSCont contp_;
  string clientData, clientRequestBody, serverResponse;
  InterceptIOChannel readio;
  InterceptIOChannel writeio;
  FCGIClientRequest *fcgiRequest;
  int request_id;

  InterceptIO(int request_id, TSHttpTxn txn);
  void closeServer();
};

class FastCGIIntercept : public InterceptPlugin
{
public:
  class InterceptIO *server;
  // TODO Check when to release it
  TSCont contp_;

  FastCGIIntercept(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
  {
    int request_id = 1;
    TSHttpTxn txn  = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    server         = new InterceptIO(request_id, txn);
    TSDebug(PLUGIN_NAME, "FastCGIIntercept : Added Server intercept");
  }

  ~FastCGIIntercept() override
  {
    TSDebug(PLUGIN_NAME, "~FastCGIIntercept : Shutting down server intercept");
    server->closeServer();
  }

  void consume(const string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;

  void initServer();

  void writeResponseChunkToATS();
  void setResponseOutputComplete();

  void
  setRequestId(uint request_id)
  {
    _request_id = request_id;
  }
  uint
  requestId()
  {
    return _request_id;
  }

private:
  uint _request_id;
};

#endif
