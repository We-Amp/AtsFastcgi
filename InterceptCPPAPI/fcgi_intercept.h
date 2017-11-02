#include <iostream>
#include <string.h>
#include "ts/ts.h"
#include "ts/ink_defs.h"
#include <atscppapi/InterceptPlugin.h>
#include <netinet/in.h>

#define PORT 60000

using std::cout;
using std::endl;
using std::string;
using namespace atscppapi;

static in_addr_t server_ip;
static int server_port;

struct InterceptIOChannel
{
    TSVIO vio;
    TSIOBuffer iobuf;
    TSIOBufferReader reader;

    InterceptIOChannel() : vio(nullptr), iobuf(nullptr), reader(nullptr) {}
    ~InterceptIOChannel()
    {
        if (this->reader)
        {
            TSIOBufferReaderFree(this->reader);
        }

        if (this->iobuf)
        {
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
    phpWrite(TSVConn vc, TSCont contp, string &clientData)
    {

        TSReleaseAssert(this->vio == nullptr);
        TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
        TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));
        TSIOBufferWrite(this->iobuf, (void *)clientData.c_str(), clientData.length());
        this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
    }
};

struct InterceptIO
{
    TSVConn vc_;
    TSHttpTxn txn_;
    TSCont contp_;
    string clientData,serverResponse;

    InterceptIOChannel readio;
    InterceptIOChannel writeio;
    InterceptIO():vc_(nullptr),
                  txn_(nullptr),
                  contp_(nullptr),
                  clientData(""),
                  serverResponse(""),
                  readio(),
                  writeio(){
    };
    void   closeServer();
};


class FastCGIIntercept : public InterceptPlugin
{
public:
  struct InterceptIO *server ;
  FastCGIIntercept(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
  {
    server = new InterceptIO();
    server->txn_ = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    cout <<"FastCGIIntercept : Added Server intercept" << endl;
  }
  void consume(const string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;
  TSCont initServer();
  void writeResponseChunkToATS();
  
  void setResponseOutputComplete();
  ~FastCGIIntercept() override
  {
    cout <<"~FastCGIIntercept : Shutting down server intercept" << endl;
    server->closeServer();

  }
};

static int handlePHPConnectionEvents(TSCont contp,TSEvent event, void *edata);
int64_t InterceptTransferData(InterceptIO *server);