
#include "server_connection.h"

#include "ats_fcgi_client.h"
#include "ats_mod_intercept.h"

InterceptIOChannel::InterceptIOChannel() : vio(nullptr), iobuf(nullptr), reader(nullptr), total_bytes_written(0)
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
  TSReleaseAssert(this->vio == nullptr);
  TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
  TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));

  this->vio = TSVConnRead(vc, contp, this->iobuf, INT64_MAX);
}

void
InterceptIOChannel::write(TSVConn vc, TSCont contp)
{
  TSReleaseAssert(this->vio == nullptr);
  TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
  TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));

  this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
}

void
InterceptIOChannel::phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size)
{
  if (!this->iobuf) {
    this->iobuf  = TSIOBufferCreate();
    this->reader = TSIOBufferReaderAlloc(this->iobuf);
    this->vio    = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }
  int num_bytes_written = TSIOBufferWrite(this->iobuf, (const void *)buf, data_size);
  if (num_bytes_written != data_size) {
    TSError("%s Error while writing to buffer! Attempted %d bytes but only "
            "wrote %d bytes",
            PLUGIN_NAME, data_size, num_bytes_written);
  }
  total_bytes_written += data_size;
  cout << "Wrote " << total_bytes_written << " bytes on PHP side" << endl;
  TSVIOReenable(this->vio);
}

ServerConnection::ServerConnection(Server *server, TSEventFunc funcp)
  : _state(INITIATED), _server(server), _funcp(funcp), _requestId(0)
{
  createConnection();
}

ServerConnection::~ServerConnection()
{
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
  char *arr  = InterceptGlobal::plugin_data->global_config->server_ip;
  char *port = InterceptGlobal::plugin_data->global_config->server_port;
  cout << "arr : " << arr << endl;
  sscanf(arr, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
  sscanf(port, "%hu", &p);
  printf("%d %d %d %d : %d", a, b, c, d, p);

  int new_ip = (a << 24) | (b << 16) | (c << 8) | (d);
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family      = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(new_ip); /* Should be in network byte order */
  ip_addr.sin_port        = htons(p);      // server_port;

  // contp is a global netconnect handler  which will be used to connect with
  // php server
  _contp = TSContCreate(_funcp, TSMutexCreate());
  TSContDataSet(_contp, new ServerConnectionInfo(_server, this));
  TSNetConnect(_contp, (struct sockaddr const *)&ip_addr);
  TSDebug(PLUGIN_NAME, "FastCGIIntercept::initServer : Data Set Contp: %p", _contp);
}
