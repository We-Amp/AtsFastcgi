/**
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/PluginInit.h>
#include <iostream>
#include <string.h>
#include <netinet/in.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"
#include "utils_internal.h"

using namespace atscppapi;

using std::cout;
using std::endl;
using std::string;

static in_addr_t server_ip;
static int server_port;
TSVConn server_vc;
TSCont contp;
string clientData;
int handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata);

namespace
{
GlobalPlugin *plugin;
}
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
  phpWrite(TSVConn vc, TSCont contp,string &clientData)
  {
   
    TSReleaseAssert(this->vio == nullptr);
    TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
    TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));
    cout << "Writing string:" << clientData.c_str() << " of Length:" << clientData.length() << " bytes data to internal php server socket.." << endl;
    TSIOBufferWrite(this->iobuf, (void *)clientData.c_str(), clientData.length());
    this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }
};
struct InterceptIO {
  TSVConn vc;
  TSHttpTxn txn;
  
  InterceptIOChannel readio;
  InterceptIOChannel writeio;

  void
  close()
  {
    if (this->vc) {
      TSVConnClose(this->vc);
    }
    this->vc         = nullptr;
    this->readio.vio = this->writeio.vio = nullptr;
  }
};
struct InterceptIO server;

class FastCGIIntercept : public InterceptPlugin
{
public:
  //TSVConn server_vc_;
  TSCont contp_;
  //string clientData;
  //struct InterceptIO server;
  FastCGIIntercept(Transaction &transaction, TSVConn &server_vc, TSCont &contp) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
  {
    server.vc = server_vc;
    contp_ = contp;
    server.txn = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    //TSContDataSet(contp,&server);
    cout << "Added Server intercept" << endl;
  }
  void consume(const string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;

  // TSVIO serverRead();
  // TSVIO serverWrite(string &clientData);
  ~FastCGIIntercept() override
  {
    cout << "Shutting down server intercept" << endl;
    //TSVConnClose(server_vc_);
    server.close();
    TSContDestroy(contp_);
    clientData= nullptr;
  }
};

class FastCGIGlobalPlugin : public GlobalPlugin
{
public:
  FastCGIGlobalPlugin() : GlobalPlugin(true)
  {
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS);
  }
  void
  handleReadRequestHeaders(Transaction &transaction) override
  {
    if (transaction.getClientRequest().getUrl().getPath().find(".php") != string::npos)
    {
      transaction.addPlugin(new FastCGIIntercept(transaction, server_vc, contp));
    }
    transaction.resume();
  }
};

int handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata)
{

  cout << "Handing  php server events: " << event <<"EventName:"<<TSHttpEventNameLookup(event)<< "Event Data:" << string((char *)edata) << endl;
  if (TSVConnClosedGet(contp)) {
    TSContDestroy(contp);
    cout<<"Connection seems to be closed"<<endl;
    return 0;
  }
  switch (event)
  {
  case TS_EVENT_NET_CONNECT:
  {
    server_vc = (TSVConn)edata;
    //TSContDataset(contp,server_vc);
    cout << "Connected to php server...vConn Handle." << endl;
    cout << "Server VConn: " << server_vc << "\t ContP: " << contp << endl;

    server.writeio.phpWrite(server_vc,contp,clientData);
    cout<<"WriteIO.vio :"<<server.writeio.vio<<endl;
    server.readio.read(server_vc,contp);
    cout<<"ReadIO.vio :"<<server.readio.vio<<endl;
  }
  break;

  case TS_EVENT_NET_CONNECT_FAILED:
  {

    cout << "TSNetConnect Failed " << endl;
    return TS_EVENT_NONE;
  }
  case TS_EVENT_VCONN_READ_READY:
  {
    cout << "Inside Read Ready...VConn Open " << endl;
    //cout<<"ReadIO.vio Bytes:"<<server.readio.vio->nbytes<<"ntodo: "<<server.readio.vio->ntodo<<endl;
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_COMPLETE:
  {
    // We write data forever, so we should never get a
    // WRITE_COMPLETE.
    cout << "Read complete...VConn still Open " << endl;
    return TS_EVENT_NONE;
  }
  case TS_EVENT_VCONN_WRITE_READY:
  {
    cout << "Inside Write Ready...VConn Open " << endl;
    cout<<"WriteIO.vio :"<<server.writeio.vio<<endl;
    TSVIOReenable(server.writeio.vio);
    // InterceptIO *tempServer = (InterceptIO*)TSContDataGet(contp);
    // printf("\n\nserverVIO : %s ,nbytes Read:", tempServer->writeio.vio);//,tempServer->writeio.vio->nbytes);
    return TS_EVENT_NONE;
  }
  break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
  {
    // We write data forever, so we should never get a
    // WRITE_COMPLETE.
    cout << "Write complete...VConn still Open " << endl;
  }
  break;
  case TS_EVENT_ERROR:
  {
    cout << "Error..." << endl;
    TSVIO input_vio;

    /* Get the write VIO for the write operation that was
      * performed on ourself. This VIO contains the continuation of
      * our parent transformation. This is the input VIO.
      */
    //input_vio = TSVConnWriteVIOGet(contp);

    /* Call back the write VIO continuation to let it know that we
      * have completed the write operation.
      */
    //TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    return TS_EVENT_NONE;
  }

  default:
    break;
  }
  return 0;
}

void TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  

  RegisterGlobalPlugin("CPP_Example_FastCGI", "apache", "dev@trafficserver.apache.org");
  cout << "Registered and invoked the FastCGI Plugin into ATS." << endl;
  
  plugin = new FastCGIGlobalPlugin();
}

void FastCGIIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  // if (type == InterceptPlugin::REQUEST_HEADER)
  // {
  //   cout << "Read request header data" << data << endl;
  // }
  // else
  // {
  //   cout << "Read request body data" << endl
  //        << data << endl;
  // }
  clientData = data;
}


void FastCGIIntercept::handleInputComplete()
{
  
  // string response("HTTP/1.1 200 OK\r\n"
  //                 "Content-Length: 7\r\n"
  //                 "\r\n1234567");
  // TSVIO writeIO = FastCGIIntercept::serverWrite(clientData);
  // TSVIO readIO = FastCGIIntercept::serverRead();

    //TSContDataSet(contp_,clientData);
    TSAction action;
    struct sockaddr_in ip_addr;
    server_ip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
    server_ip = htonl(server_ip); //htonl(server_ip);
    server_port = htons(60000);
  
    memset(&ip_addr, 0, sizeof(ip_addr));
    ip_addr.sin_family = AF_INET;
    ip_addr.sin_addr.s_addr = server_ip; /* Should be in network byte order */
    ip_addr.sin_port = server_port;      //server_port;
  
    cout << "Setting up the connection to localhost:" << server_ip << ":" << server_port << endl;
  
    //contp is a global netconnect handler  which will be used to connect with php server
    contp = TSContCreate(handlePHPConnectionEvents, TSMutexCreate());
    action = TSNetConnect(contp, (struct sockaddr const *)&ip_addr);





  //cout<<"Transaction Intercept State URL: "<< txn.getClientRequest().getUrl()<<endl;
  //  InterceptPlugin::produce(response);
  //  cout << "Sending Autogenerated Response back to client." << endl;
  //  InterceptPlugin::setOutputComplete();
}