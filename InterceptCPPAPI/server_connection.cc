
#include "server_connection.h"

#include "ats_fcgi_client.h"
#include "ats_mod_intercept.h"
using namespace ats_plugin;
InterceptIOChannel::InterceptIOChannel() : vio(nullptr), iobuf(nullptr), reader(nullptr), total_bytes_written(0), readEnable(false)
{
}
InterceptIOChannel::~InterceptIOChannel()
{
  if (this->reader) {
    TSIOBufferReaderFree(this->reader);
  }

  if (this->iobuf) {
    TSIOBufferDestroy(this->iobuf);
  }
  total_bytes_written = 0;
}

void
InterceptIOChannel::read(TSVConn vc, TSCont contp)
{
  if (TSVConnClosedGet(vc)) {
    TSError("[InterceptIOChannel:%s] Connection Closed...", __FUNCTION__);
  }
  this->iobuf  = TSIOBufferCreate();
  this->reader = TSIOBufferReaderAlloc(this->iobuf);
  this->vio    = TSVConnRead(vc, contp, this->iobuf, INT64_MAX);
  if (this->vio == nullptr) {
    TSError("[InterceptIOChannel:%s] ERROR While reading from server", __FUNCTION__);
  }
  TSDebug(PLUGIN_NAME, "[InterceptIOChannel:%s] ReadIO.vio :%p ", __FUNCTION__, this->vio);
}

void
InterceptIOChannel::write(TSVConn vc, TSCont contp)
{
  TSReleaseAssert(this->vio == nullptr);
  TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
  TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));
  if (TSVConnClosedGet(contp)) {
    TSError("[%s] Connection Closed...", __FUNCTION__);
  } else {
    this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }
}

void
InterceptIOChannel::phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size, bool endflag)
{
  if (TSVConnClosedGet(vc)) {
    TSError("[InterceptIOChannel:%s] Connection Closed...", __FUNCTION__);
  }
  if (!this->iobuf) {
    this->iobuf  = TSIOBufferCreate();
    this->reader = TSIOBufferReaderAlloc(this->iobuf);
    this->vio    = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
    if (this->vio == nullptr) {
      TSError("[InterceptIOChannel:%s] Error TSVIO returns null. ", __FUNCTION__);
    }
  }
  int num_bytes_written = TSIOBufferWrite(this->iobuf, (const void *)buf, data_size);
  if (num_bytes_written != data_size) {
    TSError("[InterceptIOChannel:%s] Error while writing to buffer! Attempted %d bytes but only "
            "wrote %d bytes",
            PLUGIN_NAME, data_size, num_bytes_written);
  }
  total_bytes_written += data_size;
  TSDebug(PLUGIN_NAME, "writeio.vio :%p, Wrote %d bytes on PHP side", this->vio, total_bytes_written);
  if (!endflag) {
    TSVIOReenable(this->vio);
  } else {
    this->readEnable = true;
    TSDebug(PLUGIN_NAME, "[%s] Done: %d \tnBytes: %d", __FUNCTION__, TSVIONDoneGet(this->vio), TSVIONBytesGet(this->vio));
  }
}

ServerConnection::ServerConnection(Server *server, TSEventFunc funcp)
  : vc_(nullptr),
    _fcgiRequest(nullptr),
    _state(INITIATED),
    _server(server),
    _funcp(funcp),
    _contp(nullptr),
    _sConnInfo(nullptr),
    _requestId(0)
{
  createConnection();
}

ServerConnection::~ServerConnection()
{
  TSDebug(PLUGIN_NAME, "Destroying server Connection Obj");

  if (vc_) {
    TSVConnClose(vc_);
    TSDebug(PLUGIN_NAME, "Closing vc_ : %p", vc_);
  }
  vc_        = nullptr;
  readio.vio = writeio.vio = nullptr;
  _requestId               = 0;
  TSDebug(PLUGIN_NAME, "Destroying _contp : %p \n\n\n", _contp);
  TSContDestroy(_contp);
  _contp = nullptr;
  delete _fcgiRequest;
  delete _sConnInfo;
}

void
ServerConnection::createFCGIClient(TSHttpTxn txn)
{
  _fcgiRequest = new FCGIClientRequest(_requestId, txn);
}

void
ServerConnection::createConnection()
{
  struct sockaddr_in ip_addr;

  unsigned short int a, b, c, d, p;
  char *arr  = InterceptGlobal::plugin_data->getGlobalConfigObj()->getServerIp();
  char *port = InterceptGlobal::plugin_data->getGlobalConfigObj()->getServerPort();
  sscanf(arr, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
  sscanf(port, "%hu", &p);
  int new_ip = (a << 24) | (b << 16) | (c << 8) | (d);
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family      = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(new_ip); /* Should be in network byte order */
  ip_addr.sin_port        = htons(p);      // server_port;

  // contp is a global netconnect handler  which will be used to connect with
  // php server
  _contp     = TSContCreate(_funcp, TSMutexCreate());
  _sConnInfo = new ServerConnectionInfo(_server, this);
  TSContDataSet(_contp, _sConnInfo);
  TSNetConnect(_contp, (struct sockaddr const *)&ip_addr);
  TSDebug(PLUGIN_NAME, "[ServerConnection:%s] : Data Set Contp: %p", __FUNCTION__, _contp);
}
