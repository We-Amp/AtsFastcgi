#include <iostream>
#include <string>
#include "fcgi_intercept.h"
#include <netinet/in.h>
#include "ats_mod_fcgi.h"
using namespace fcgiGlobal;

void FastCGIIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
    server->clientData += data;
    cout<<"FastCGIIntercept::consume : Client data Length:"<<data.length()<<endl;
}

void FastCGIIntercept::handleInputComplete(){
    server->contp_ = initServer();
 }

void FastCGIIntercept::writeResponseChunkToATS(){
     InterceptPlugin::produce(server->serverResponse);
}

void FastCGIIntercept::setResponseOutputComplete(){
    InterceptPlugin::setOutputComplete();
}

int64_t
InterceptTransferData(InterceptIO *server){
  TSIOBufferBlock block;
  int64_t consumed = 0;
  server->serverResponse = "";
  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(server->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;
    ptr = TSIOBufferBlockReadStart(block, server->readio.reader, &remain);
    server->serverResponse +=string((char*)ptr,remain);
    consumed += remain;
  }
  if (consumed) {
    cout<<"InterceptTransferData: Read "<<consumed<<" Bytes from server and writing it to client side"<<endl;
    TSIOBufferReaderConsume(server->readio.reader, consumed);
  }
  TSVIONDoneSet(server->readio.vio, TSVIONDoneGet(server->readio.vio) + consumed);
  return consumed;
}

static int handlePHPConnectionEvents(TSCont contp,TSEvent event, void *edata)
{ 
  cout << "handlePHPConnectionEvents:  event( " << event <<" ). \tEventName: "<<TSHttpEventNameLookup(event)<< " \tEvent Data:" << string((char *)edata) << endl;
  FastCGIIntercept *fcgi = (FastCGIIntercept*)TSContDataGet(contp);
  InterceptIO *server = fcgi->server;
  switch (event)
  {
  case TS_EVENT_NET_CONNECT:
  {
    server->vc_ = (TSVConn)edata;
    cout << "handlePHPConnectionEvents : Connected to php server...vConn Handle." << endl;
    server->contp_= contp;
    server->writeio.phpWrite(server->vc_,contp,server->clientData);
    cout<<"handlePHPConnectionEvents: WriteIO.vio :"<<server->writeio.vio<<"\t ReadIO.vio :"<<server->readio.vio<<endl;
    server->readio.read(server->vc_,contp);
  }
  break;

  case TS_EVENT_NET_CONNECT_FAILED:
  {

    cout << "handlePHPConnectionEvents : TSNetConnect Failed." << endl;
    server->closeServer();
    return TS_EVENT_NONE;
  }
  case TS_EVENT_VCONN_READ_READY:
  {
    cout << "handlePHPConnectionEvents: Inside Read Ready...VConn Open " << endl;
    InterceptTransferData(server);
    fcgi->writeResponseChunkToATS();
    break;
  }

  case TS_EVENT_VCONN_WRITE_READY:
  {
    cout << "handlePHPConnectionEvents: Inside Write Ready...VConn Open " << endl;
    return TS_EVENT_NONE;
  }
  break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_READ_COMPLETE:
        return TS_EVENT_NONE;

  case TS_EVENT_VCONN_EOS:{
    cout<<"handlePHPConnectionEvents: Sending Response to client side"<<endl;
    InterceptTransferData(server);
    fcgi->writeResponseChunkToATS();
    fcgi->setResponseOutputComplete();
    return TS_EVENT_NONE;
  }break;
  case TS_EVENT_ERROR:
  {
    cout << "handlePHPConnectionEvents: Error..." << endl;
    return TS_EVENT_NONE;
  }

  default:
    break;
  }
  return 0;
}

TSCont FastCGIIntercept::initServer(){
    TSCont contp;
    struct sockaddr_in ip_addr;
    // server_ip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
    // server_ip = htonl(server_ip);
    // server_port = htons(PORT);
    unsigned short int a,b,c,d,p;
    char *arr = plugin_data->global_config->server_ip;
    char *port = plugin_data->global_config->server_port;
    cout<<"arr : "<<arr<<endl;
    sscanf(arr, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
    sscanf(port,"%hu",&p);
    printf("%d %d %d %d : %d",a,b,c,d,p);
    
    int new_ip = (a << 24) | (b << 16) | (c << 8) | (d);
    memset(&ip_addr, 0, sizeof(ip_addr));
    ip_addr.sin_family = AF_INET;
    ip_addr.sin_addr.s_addr = htonl(new_ip); /* Should be in network byte order */
    ip_addr.sin_port = htons(p);      //server_port;
  
    //contp is a global netconnect handler  which will be used to connect with php server
    contp = TSContCreate(handlePHPConnectionEvents, TSMutexCreate());
    TSContDataSet(contp,this);
    TSNetConnect(contp, (struct sockaddr const *)&ip_addr);
    cout<<"FastCGIIntercept::initServer : Data Set Contp:"<<contp<<endl;
    return contp;
}

void InterceptIO::closeServer(){
    if (this->vc_)
    {
        TSVConnClose(this->vc_);
    }
    TSContDestroy(this->contp_);
    this->vc_ = nullptr;
    this->readio.vio = this->writeio.vio = nullptr;
    this->txn_ = nullptr;
}