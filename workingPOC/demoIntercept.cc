/** @file

  an example hello world plugin

  @section license License

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
#include <string>
#include <iostream>
#include <ts/ts.h>
#include <cstdlib>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

extern "C" {
#include "fcgi-client/include/fcgi_defs.h"
#include "fcgi-client/include/fcgi_header.h"
}

// intercept plugin
//
// This plugin primarily demonstrates the use of server interceptions to allow a
// plugin to act as an origin server. It also demonstrates how to use
// TSVConnFdCreate to wrap a TCP connection to another server, and the how to use
// the TSVConn APIs to transfer data between virtual connections.
//
// This plugin intercepts all cache misses and proxies them to a separate server
// that is assumed to be running on localhost:60000. The plugin does no HTTP
// processing at all, it simply shuffles data until the client closes the
// request. The TSQA test test-server-intercept exercises this plugin. You can
// enable extensive logging with the "intercept" diagnostic tag.

#define PLUGIN "demoIntercept"
#define PORT 60000

#define VDEBUG(fmt, ...) TSDebug(PLUGIN, fmt, ##__VA_ARGS__)

#if DEBUG
#define VERROR(fmt, ...) TSDebug(PLUGIN, fmt, ##__VA_ARGS__)
#else
#define VERROR(fmt, ...) TSError("[%s] %s: " fmt, PLUGIN, __FUNCTION__, ##__VA_ARGS__)
#endif

#define VIODEBUG(vio, fmt, ...)                                                                                              \
  VDEBUG("vio=%p vio.cont=%p, vio.cont.data=%p, vio.vc=%p " fmt, (vio), TSVIOContGet(vio), TSContDataGet(TSVIOContGet(vio)), \
         TSVIOVConnGet(vio), ##__VA_ARGS__)

#define BUF_SIZE 5000
#define FCGI_PORT "60000"
#define FCGI_SERVER "127.0.0.1"
#define MAXDATASIZE 1000

#define N_NameValue 27
fcgi_name_value nvs[N_NameValue] = {
  {"SCRIPT_FILENAME","/var/www/html/abc.php" },
  {"SCRIPT_NAME", "/abc.php"},
  {"DOCUMENT_ROOT", "/"},
  {"REQUEST_URI", "/abc.php"},
  {"PHP_SELF", "/abc.php"},
  {"TERM", "linux"},
  {"PATH", ""},
  {"PHP_FCGI_CHILDREN", "2"},
  {"PHP_FCGI_MAX_REQUESTS", "1000"},
  {"FCGI_ROLE", "RESPONDER"},
  {"SERVER_SOFTWARE", "ATS 7.1.1"},
  {"SERVER_NAME", "SimpleServer"},
  {"GATEWAY_INTERFACE", "CGI/1.1"},
  {"SERVER_PORT", FCGI_PORT},
  {"SERVER_ADDR", FCGI_SERVER},
  {"REMOTE_PORT", ""},
  {"REMOTE_ADDR", "127.0.0.1"},
  {"PATH_INFO", "no value"},
  {"QUERY_STRING", "no value"},
  {"REQUEST_METHOD", "GET"},
  {"REDIRECT_STATUS", "200"},
  {"SERVER_PROTOCOL", "HTTP/1.1"},
  {"HTTP_HOST", "localhost:8090"},
  {"HTTP_CONNECTION", "keep-alive"},
  {"HTTP_USER_AGENT", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.83 Safari/535.11"},
  {"HTTP_ACCEPT", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
  {"HTTP_ACCEPT_LANGUAGE", "en-US,en;q=0.8"},
};

static TSCont TxnHook;
static TSCont InterceptHook;

TSVConn clientvc;
TSHttpTxn originalTxn;

static int InterceptInterceptionHook(TSCont contp, TSEvent event, void *edata);
static int InterceptTxnHook(TSCont contp, TSEvent event, void *edata);

// We are going to stream data between Traffic Server and an
// external server. This structure represents the state of a
// streaming I/O request. Is is directional (ie. either a read or
// a write). We need two of these for each TSVConn; one to push
// data into the TSVConn and one to pull data out.
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
  phpWrite(TSVConn vc, TSCont contp)
  {
    /*******FCGI Request Generate Start Point   ********/
    uint16_t req_id = 1;
    uint16_t len    = 0;
    int nb, i;
    unsigned char *p, *buf;
    fcgi_header *head;
    fcgi_begin_request *begin_req = create_begin_request(req_id);

    
    buf  = (unsigned char *)malloc(BUF_SIZE);
    p    = buf;
    serialize(p, begin_req->header, sizeof(fcgi_header));
    p += sizeof(fcgi_header);
    serialize(p, begin_req->body, sizeof(fcgi_begin_request_body));
    p += sizeof(fcgi_begin_request_body);

    /* Sending fcgi_params */
    head = create_header(FCGI_PARAMS, req_id);

    len = 0;
    /* print_bytes(buf, p-buf); */
    for (i = 0; i < N_NameValue; i++) {
      nb = serialize_name_value(p, &nvs[i]);
      len += nb;
    }

    head->content_len_lo = BYTE_0(len);
    head->content_len_hi = BYTE_1(len);

    serialize(p, head, sizeof(fcgi_header));
    p += sizeof(fcgi_header);

    for (i = 0; i < N_NameValue; i++) {
      nb = serialize_name_value(p, &nvs[i]);
      p += nb;
    }

    head->content_len_lo = 0;
    head->content_len_hi = 0;

    serialize(p, head, sizeof(fcgi_header));
    p += sizeof(fcgi_header);

    /********FCGI Request Generation end  Point     *********/
    TSReleaseAssert(this->vio == nullptr);
    TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
    TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));

    TSIOBufferWrite(this->iobuf, (const void *)buf, p - buf);
    this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }
};

// A simple encapsulation of the IO state of a TSVConn. We need the TSVConn itself, and the
// IO metadata for the read side and the write side.
struct InterceptIO {
  TSVConn vc;
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

// Interception proxy state block. From our perspective, Traffic
// Server is the client, and the origin server on whose behalf we
// are intercepting is the server. Hence the "client" and
// "server" nomenclature here.
struct InterceptState {
  TSHttpTxn txn; // The transaction on whose behalf we are intercepting.

  InterceptIO client; // Server intercept VC state.
  InterceptIO server; // Intercept origin VC state.

  InterceptState() : txn(nullptr) {}
  ~InterceptState() {}
};

// Return the InterceptIO control block that owns the given VC.
static InterceptIO *
InterceptGetThisSide(InterceptState *istate, TSVConn vc)
{
  return (istate->client.vc == vc) ? &istate->client : &istate->server;
}

// Return the InterceptIO control block that doesn't own the given VC.
static InterceptIO *
InterceptGetOtherSide(InterceptState *istate, TSVConn vc)
{
  return (istate->client.vc == vc) ? &istate->server : &istate->client;
}

// Evaluates to a human-readable name for a TSVConn in the
// intercept proxy state.
static const char *
InterceptProxySide(const InterceptState *istate, const InterceptIO *io)
{
  return (io == &istate->client) ? "<client>" : (io == &istate->server) ? "<server>" : "<unknown>";
}

static const char *
InterceptProxySideVC(const InterceptState *istate, TSVConn vc)
{
  return (istate->client.vc && vc == istate->client.vc) ? "<client>" :
                                                          (istate->server.vc && vc == istate->server.vc) ? "<server>" : "<unknown>";
}

static bool
InterceptAttemptDestroy(InterceptState *istate, TSCont contp)
{
  if (istate->server.vc == nullptr && istate->client.vc == nullptr) {
    VDEBUG("destroying server intercept state istate=%p contp=%p", istate, contp);
    TSContDataSet(contp, nullptr); // Force a crash if we get additional events.
    TSContDestroy(contp);
    delete istate;
    return true;
  }

  return false;
}

union socket_type {
  struct sockaddr_storage storage;
  struct sockaddr sa;
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
};

union argument_type {
  void *ptr;
  intptr_t ecode;
  TSVConn vc;
  TSVIO vio;
  TSHttpTxn txn;
  InterceptState *istate;

  argument_type(void *_p) : ptr(_p) {}
};

static TSCont
InterceptContCreate(TSEventFunc hook, TSMutex mutexp, void *data)
{
  TSCont contp;

  TSReleaseAssert((contp = TSContCreate(hook, mutexp)));
  TSContDataSet(contp, data);
  return contp;
}

static int
findSubstr(char *inpText, char *pattern)
{
  int inplen = strlen(inpText);
  while (inpText != NULL) {
    char *remTxt = inpText;
    char *remPat = pattern;
    if (strlen(remTxt) < strlen(remPat)) {
      return -1;
    }
    while (*remTxt++ == *remPat++) {
      // printf("remTxt %s \nremPath %s \n", remTxt, remPat);
      if (*remPat == '\0') {
        // printf ("match found \n");
        return inplen - strlen(inpText + 1);
      }
      if (remTxt == NULL) {
        return -1;
      }
    }
    remPat = pattern;
    inpText++;
  }
}
static bool
InterceptShouldInterceptRequest(TSHttpTxn txnp)
{
  // Normally, this function would inspect the request and
  // determine whether it should be intercepted. We might examine
  // the URL path, or some headers. For the sake of this example,
  // we will intercept urls pointing to php fastCGI only.
  TSMBuffer bufp;
  TSMLoc offset_loc, url_loc;
  TSReturnCode req_status;
  int retv = 0, url_length;
  char *url_str, *php_str = ".php";

  if (TSHttpTxnClientReqGet(txnp, &bufp, &offset_loc) == TS_SUCCESS) {
    req_status = TSHttpHdrUrlGet(bufp, offset_loc, &url_loc);
    if (req_status == TS_SUCCESS) {
      url_str = TSUrlStringGet(bufp, url_loc, &url_length);
      // check wheather to intercept the request
      retv = findSubstr(url_str, php_str);
      if (retv > -1) {
        VDEBUG("Found the substring at position %d Intercepting URL: %s\n", retv, url_str);
        const char *urlPath = TSUrlPathGet(bufp,url_loc,&url_length);
      } else {
        retv = 0;
        VDEBUG("You are forbidden from intercepting url: \"%s\"\n", url_str);
      }
      TSfree(url_str);
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, offset_loc);
  }
  return retv;
}

unsigned char *tempBuf = (unsigned char*)malloc(BUF_SIZE*100);
size_t tempLength(0);

// This function is called in response to a READ_READY event. We
// should transfer any data we find from one side of the transfer
// to the other.
static int64_t
InterceptTransferData(InterceptIO *from, InterceptIO *to, const char *hostSide)
{
  TSIOBufferBlock block;
  int64_t consumed = 0;
  
  
  std::string string_output;

  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(from->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;

    VDEBUG("attempting to transfer %" PRId64 " available bytes", TSIOBufferBlockReadAvail(block, from->readio.reader));
    // Take the data from each buffer block, and write it into
    // the buffer of the write VIO.
    ptr = TSIOBufferBlockReadStart(block, from->readio.reader, &remain);
    if(strcmp(hostSide,"<server>")==0){
        if(remain==0)
          break;
        consumed = remain;
        memcpy(tempBuf+tempLength,ptr,remain);
        tempLength += remain;
  
    }else{
        std::cout<< "client transfer data"<<std::endl;
        while (ptr && remain) {
          int64_t nbytes;
          nbytes = TSIOBufferWrite(to->writeio.iobuf, ptr, remain);
          remain -= nbytes;
          ptr += nbytes;
          consumed += nbytes;
        }
  
     }
  }

  VDEBUG("consumed %" PRId64 " bytes reading from vc=%p, writing to vc=%p", consumed, from->vc, to->vc);
  if (consumed) {
    TSIOBufferReaderConsume(from->readio.reader, consumed);
    // Note that we don't have to call TSIOBufferProduce here.
    // This is because data passed into TSIOBufferWrite is
    // automatically "produced".

    if (consumed && tempLength) {
      printf("\nReenabling read state again.");
      TSVIO writeio = to->writeio.vio;
      VIODEBUG(writeio, "WRITE VIO ndone=%" PRId64 " ntodo=%" PRId64, TSVIONDoneGet(writeio), TSVIONTodoGet(writeio));
      TSVIOReenable(from->readio.vio); // Re-enable the read side.
      TSVIOReenable(to->writeio.vio);  // Reenable the write side.
      return 0;
    }
  }
  
  return consumed;
}



// Handle events from TSHttpTxnServerIntercept. The intercept
// starts with TS_EVENT_NET_ACCEPT, and then continues with
// TSVConn events.
static int
InterceptInterceptionHook(TSCont contp, TSEvent event, void *edata)
{
  argument_type arg(edata);

  VDEBUG("contp=%p, event=%s (%d), edata=%p", contp, TSHttpEventNameLookup(event), event, arg.ptr);

  switch (event) {
  case TS_EVENT_NET_CONNECT: {
    argument_type cdata(TSContDataGet(contp));
    InterceptState *istate = new InterceptState();

    istate->txn       = originalTxn;
    istate->server.vc = arg.vc;
    istate->client.vc = clientvc;

    TSContDataSet(contp, istate);

    // Start reading the request from the server intercept VC.
    istate->client.readio.read(istate->client.vc, contp);
    VIODEBUG(istate->client.readio.vio, "started %s read", InterceptProxySide(istate, &istate->client));

    // Start reading the response from the intercepted origin server VC.
    istate->server.readio.read(istate->server.vc, contp);
    VIODEBUG(istate->server.readio.vio, "started %s read", InterceptProxySide(istate, &istate->server));

    // Start writing the response to the server intercept VC.
    istate->client.writeio.write(istate->client.vc, contp);
    VIODEBUG(istate->client.writeio.vio, "started %s write", InterceptProxySide(istate, &istate->client));

    // Start writing the request to the intercepted origin server VC.
    istate->server.writeio.phpWrite(istate->server.vc, contp);
    // istate->server.writeio.write(istate->server.vc, contp);
    VIODEBUG(istate->server.writeio.vio, "started %s write", InterceptProxySide(istate, &istate->server));
    return TS_EVENT_NONE;
  }
  case TS_EVENT_NET_ACCEPT: {
    // Set up the server intercept. We have the original
    // TSHttpTxn from the continuation. We need to connect to the
    // real origin and get ready to shuffle data around.
    char buf[INET_ADDRSTRLEN];
    socket_type addr;
    argument_type cdata(TSContDataGet(contp));

    // This event is delivered by the continuation that we
    // attached in InterceptTxnHook, so the continuation data is
    // the TSHttpTxn pointer.
    // VDEBUG("allocated server intercept state istate=%p for txn=%p", istate, cdata.txn);

    // Set up a connection to our real origin, which will be
    // 127.0.0.1:$PORT.
    memset(&addr, 0, sizeof(addr));
    addr.sin.sin_family      = AF_INET;
    addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // XXX config option
    addr.sin.sin_port        = htons(PORT);            // XXX config option

    originalTxn = cdata.txn;
    clientvc    = arg.vc;
    TSAction action = TSNetConnect(contp, (const sockaddr *)&addr.sin);
    return TS_EVENT_NONE;
  }

  case TS_EVENT_NET_ACCEPT_FAILED: {
    // TS_EVENT_NET_ACCEPT_FAILED will be delivered if the
    // transaction is cancelled before we start tunnelling
    // through the server intercept. One way that this can happen
    // is if the intercept is attached early, and then we server
    // the document out of cache.
    argument_type cdata(TSContDataGet(contp));

    // There's nothing to do here except nuke the continuation
    // that was allocated in InterceptTxnHook().
    VDEBUG("cancelling server intercept request for txn=%p", cdata.txn);

    TSContDestroy(contp);
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_READY: {
    argument_type cdata = TSContDataGet(contp);
    TSVConn vc          = TSVIOVConnGet(arg.vio);
    InterceptIO *from   = InterceptGetThisSide(cdata.istate, vc);
    InterceptIO *to     = InterceptGetOtherSide(cdata.istate, vc);
    int64_t nbytes;

    VIODEBUG(arg.vio, "ndone=%" PRId64 " ntodo=%" PRId64, TSVIONDoneGet(arg.vio), TSVIONTodoGet(arg.vio));
    VDEBUG("reading vio=%p vc=%p, istate=%p is bound to client vc=%p and server vc=%p", arg.vio, TSVIOVConnGet(arg.vio),
           cdata.istate, cdata.istate->client.vc, cdata.istate->server.vc);

    if (to->vc == nullptr) {
      VDEBUG("closing %s vc=%p", InterceptProxySide(cdata.istate, from), from->vc);
      from->close();
    }

    if (from->vc == nullptr) {
      VDEBUG("closing %s vc=%p", InterceptProxySide(cdata.istate, to), to->vc);
      to->close();
    }

    if (InterceptAttemptDestroy(cdata.istate, contp)) {
      return TS_EVENT_NONE;
    }

    VDEBUG("reading from %s (vc=%p), writing to %s (vc=%p)", InterceptProxySide(cdata.istate, from), from->vc,
           InterceptProxySide(cdata.istate, to), to->vc);
    const char *hostSide = InterceptProxySide(cdata.istate, from);
    nbytes = InterceptTransferData(from, to,hostSide);

    // Reenable the read VIO to get more events.
    if (nbytes) {
      TSVIO writeio = to->writeio.vio;
      VIODEBUG(writeio, "WRITE VIO ndone=%" PRId64 " ntodo=%" PRId64, TSVIONDoneGet(writeio), TSVIONTodoGet(writeio));
      TSVIOReenable(from->readio.vio); // Re-enable the read side.
      TSVIOReenable(to->writeio.vio);  // Reenable the write side.
    }

    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    // WRITE_READY events happen all the time, when the TSVConn
    // buffer drains. There's no need to do anything with these
    // because we only fill the buffer when we have data to read.
    // The exception is where one side of the proxied connection
    // has been closed. Then we want to close the other side.
    argument_type cdata = TSContDataGet(contp);
    TSVConn vc          = TSVIOVConnGet(arg.vio);
    InterceptIO *to     = InterceptGetThisSide(cdata.istate, vc);
    InterceptIO *from   = InterceptGetOtherSide(cdata.istate, vc);

    // If the other side is closed, close this side too, but only if there
    // we have drained the write buffer.
    if (from->vc == nullptr) {
      VDEBUG("closing %s vc=%p with %" PRId64 " bytes to left", InterceptProxySide(cdata.istate, to), to->vc,
             TSIOBufferReaderAvail(to->writeio.reader));
      if (TSIOBufferReaderAvail(to->writeio.reader) == 0) {
        to->close();
      }
    }

    InterceptAttemptDestroy(cdata.istate, contp);
    return TS_EVENT_NONE;
  }

  case TS_EVENT_ERROR:
  case TS_EVENT_VCONN_EOS: {
    // If we get an EOS on one side, we should just send and EOS
    // on the other side too. The server intercept will always
    // send us an EOS after Traffic Server has finished reading
    // the response. Once that happens, we are also finished with
    // the intercepted origin server. The same reasoning applies
    // to receiving EOS from the intercepted origin server, and
    // when handling errors.

    TSVConn vc          = TSVIOVConnGet(arg.vio);
    argument_type cdata = TSContDataGet(contp);

    InterceptIO *from = InterceptGetThisSide(cdata.istate, vc);
    InterceptIO *to   = InterceptGetOtherSide(cdata.istate, vc);

    VIODEBUG(arg.vio, "received EOS or ERROR from %s side", InterceptProxySideVC(cdata.istate, vc));

    const char *temphostSide = InterceptProxySide(cdata.istate, from);
    if( strcmp(temphostSide , "<server>" ) == 0 ){
      int64_t consumed = 0;
      int64_t remain = 0;
      const char *ptr;
      uchar *rbuf;
      size_t len(0);
      fcgi_record_list *rlst = NULL;
      std::string string_output;
      string_output = "HTTP/1.0 200 OK\r\n";
      rbuf = (uchar *)malloc(sizeof(size_t)*tempLength);

      len = fcgi_process_buffer_new(tempBuf, tempBuf + (size_t)tempLength, &rlst, &rbuf,tempLength);
      string_output +=  std::string((char *)rbuf,len);
      ptr    = string_output.c_str();
      remain = string_output.length();
      std::cout<<"EOS Reached--->> Response to client: \n"<<string_output<<std::endl;
      while (ptr && remain) {
        int64_t nbytes;
        nbytes = TSIOBufferWrite(to->writeio.iobuf, ptr, remain);
        remain -= nbytes;
        ptr += nbytes;
        consumed += nbytes;
      }
      delete rbuf;
      delete rlst,tempBuf;
      tempLength = 0;
    }

    // Close the side that we received the EOS event from.
    if (from) {
      VDEBUG("%s writeio has %" PRId64 " bytes left", InterceptProxySide(cdata.istate, from),
             TSIOBufferReaderAvail(from->writeio.reader));
      from->close();
    }

    // Should we also close the other side? Well, that depends on whether the reader
    // has drained the data. If we close too early they will see a truncated read.
    if (to) {
      VDEBUG("%s writeio has %" PRId64 " bytes left", InterceptProxySide(cdata.istate, to),
             TSIOBufferReaderAvail(to->writeio.reader));
      if (TSIOBufferReaderAvail(to->writeio.reader) == 0) {
        to->close();
      }
    }

    InterceptAttemptDestroy(cdata.istate, contp);
    return event == TS_EVENT_ERROR ? TS_EVENT_ERROR : TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_COMPLETE:
    // We read data forever, so we should never get a
    // READ_COMPLETE.
    VIODEBUG(arg.vio, "unexpected TS_EVENT_VCONN_READ_COMPLETE");
    return TS_EVENT_NONE;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    // We write data forever, so we should never get a
    // WRITE_COMPLETE.
    VIODEBUG(arg.vio, "unexpected TS_EVENT_VCONN_WRITE_COMPLETE");
    return TS_EVENT_NONE;

  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    VERROR("unexpected event %s (%d) edata=%p", TSHttpEventNameLookup(event), event, arg.ptr);
    return TS_EVENT_ERROR;

  default:
    VERROR("unexpected event %s (%d) edata=%p", TSHttpEventNameLookup(event), event, arg.ptr);
    return TS_EVENT_ERROR;
  }
}

// Handle events that occur on the TSHttpTxn.
static int
InterceptTxnHook(TSCont contp, TSEvent event, void *edata)
{
  argument_type arg(edata);

  VDEBUG("contp=%p, event=%s (%d), edata=%p", contp, TSHttpEventNameLookup(event), event, arg.ptr);

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    if (InterceptShouldInterceptRequest(arg.txn)) {
      TSCont c = InterceptContCreate(InterceptInterceptionHook, TSMutexCreate(), arg.txn);

      VDEBUG("intercepting orgin server request for txn=%p, cont=%p", arg.txn, c);
      TSHttpTxnServerIntercept(c, arg.txn);
    }

    break;
  }

  default:
    VERROR("unexpected event %s (%d)", TSHttpEventNameLookup(event), event);
    break;
  }

  TSHttpTxnReenable(arg.txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

void
TSPluginInit(int /* argc */, const char * /* argv */ [])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN;
  info.vendor_name   = (char *)"MyCompany";
  info.support_email = (char *)"ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    VERROR("plugin registration failed\n");
  }

  // XXX accept hostname and port arguments

  TxnHook       = InterceptContCreate(InterceptTxnHook, nullptr, nullptr);
  InterceptHook = InterceptContCreate(InterceptInterceptionHook, nullptr, nullptr);

  // Wait until after the cache lookup to decide whether to
  // intercept a request. For cache hits we will never intercept.
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TxnHook);
}
