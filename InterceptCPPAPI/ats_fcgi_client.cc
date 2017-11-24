#include "ats_fcgi_client.h"
#include "ats_mod_fcgi.h"
#include "fcgi_protocol.h"
#include "ts/ink_defs.h"
#include <iostream>
#include <sstream>
#include <string>
#include <ts/ts.h>
#include <map>
#include <iterator>
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/utils.h>

using namespace atscppapi;
using namespace FCGIClient;
using namespace std;

struct FCGIClient::FCGIClientState {
  FCGI_BeginRequest *request;
  FCGI_Header *header, *postHeader;
  unsigned char *buff, *pBuffInc;
  FCGIRecordList *records = nullptr;
  TSHttpTxn txn_;
  map<string, string> requestHeaders;
  int request_id_;

  FCGIClientState()
    : request(nullptr), header(nullptr), postHeader(nullptr), buff(nullptr), pBuffInc(nullptr), records(nullptr), request_id_(0){};

  ~FCGIClientState()
  {
    request_id_ = 0;
    free(request);
    free(header);
    TSfree(postHeader);
    TSfree(buff);
    TSfree(records);
  };
};

// input to the constructor will be either unique transaction id or int type
// requestId
FCGIClientRequest::FCGIClientRequest(int request_id, TSHttpTxn txn)
{
  state_                 = new FCGIClientState();
  state_->txn_           = txn;
  state_->request_id_    = request_id;
  state_->requestHeaders = GenerateFcgiRequestHeaders();
  // TODO Call printFCGIRequestHeaders() to printFCGIHeaders
  string str("POST"), value;
  if (str.compare(state_->requestHeaders["REQUEST_METHOD"]) == 0) {
    Transaction &transaction = utils::internal::getTransaction(state_->txn_);
    Headers &h               = transaction.getClientRequest().getHeaders();

    if (h.isInitialized()) {
      cout << "Header Count: " << h.size() << endl;
      string key("Content-Length");
      atscppapi::header_field_iterator it = h.find(key);
      atscppapi::HeaderField hf(*it);
      value = hf.values(value);
      cout << "Content Length Value: " << value << endl;
      state_->requestHeaders["CONTENT_LENGTH"] = value.c_str();
      // TODO atscppapi::header_field_value_iterator hfv = hf.begin();
      key = string("Content-type");
      it  = h.find(key);
      HeaderField hf1(*it);
      value = "";
      value = hf1.values(value);
      cout << "Content TYPE Value: " << value << endl;
      state_->requestHeaders["CONTENT_TYPE"] = value.c_str();
    }

    int contentLength = 0;
    string cl         = state_->requestHeaders["CONTENT_LENGTH"];
    stringstream strToInt(cl);
    strToInt >> contentLength;
    cout << "Content Length to Int" << contentLength << endl;
    state_->buff = (unsigned char *)TSmalloc(BUF_SIZE + contentLength);
  } else {
    state_->buff = (unsigned char *)TSmalloc(BUF_SIZE);
  }

  state_->pBuffInc = state_->buff;
}

// destructor will reset the client_req_id and delete the recListState_ object
// holding respose records received from fcgi server
FCGIClientRequest::~FCGIClientRequest()
{
  delete state_;
}

map<string, string>
FCGIClientRequest::GenerateFcgiRequestHeaders()
{
  map<string, string> fcgiReqHeader;
  Transaction &transaction = utils::internal::getTransaction(state_->txn_);
  // Retriving headers inside local Headers to build  request as per config
  // later

  fcgiReqHeader["SCRIPT_FILENAME"]   = "/var/www/html/" + transaction.getClientRequest().getUrl().getPath();
  fcgiReqHeader["GATEWAY_INTERFACE"] = "FastCGI/1.1";
  fcgiReqHeader["REQUEST_METHOD"]    = HTTP_METHOD_STRINGS[transaction.getClientRequest().getMethod()];

  fcgiReqHeader["SCRIPT_NAME"]     = transaction.getClientRequest().getUrl().getPath();
  fcgiReqHeader["QUERY_STRING"]    = transaction.getClientRequest().getUrl().getQuery();
  fcgiReqHeader["REQUEST_URI"]     = transaction.getClientRequest().getUrl().getPath();
  fcgiReqHeader["DOCUMET_ROOT"]    = "/";
  fcgiReqHeader["SERVER_SOFTWARE"] = "ATS 7.1.1";
  fcgiReqHeader["REMOTE_ADDR"]     = "127.0.0.1";
  fcgiReqHeader["REMOTE_PORT"]     = "";
  fcgiReqHeader["SERVER_ADDR"]     = "127.0.0.1";
  fcgiReqHeader["SERVER_PORT"]     = "60000";
  fcgiReqHeader["SERVER_NAME"]     = "SimpleServer";
  fcgiReqHeader["SERVER_PROTOCOL"] = "HTTP/1.1";
  fcgiReqHeader["CONTENT_TYPE"]    = "application/x-www-form-urlencoded";
  fcgiReqHeader["FCGI_ROLE"]       = "RESPONDER";
  return fcgiReqHeader;
}

void
FCGIClientRequest::printFCGIRequestHeaders()
{
  std::map<string, string>::iterator it;
  for (it = state_->requestHeaders.begin(); it != state_->requestHeaders.end(); ++it) {
    cout << it->first << " => " << it->second << endl;
  }
}

FCGI_Header *
FCGIClientRequest::createHeader(uchar type)
{
  FCGI_Header *tmp = (FCGI_Header *)calloc(1, sizeof(FCGI_Header));
  tmp->version     = FCGI_VERSION_1;
  tmp->type        = type;
  fcgiHeaderSetRequestId(tmp, state_->request_id_);
  return tmp;
}

FCGI_BeginRequest *
FCGIClientRequest::createBeginRequest()
{
  state_->request = (FCGI_BeginRequest *)TSmalloc(sizeof(FCGI_BeginRequest));
  // TODO send the request id here
  state_->request->header       = createHeader(FCGI_BEGIN_REQUEST);
  state_->request->body         = (FCGI_BeginRequestBody *)calloc(1, sizeof(FCGI_BeginRequestBody));
  state_->request->body->roleB0 = FCGI_RESPONDER;

  state_->request->header->contentLengthB0 = sizeof(FCGI_BeginRequestBody);

  // serialize request header
  serialize(state_->pBuffInc, state_->request->header, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);
  serialize(state_->pBuffInc, state_->request->body, sizeof(FCGI_BeginRequestBody));
  state_->pBuffInc += sizeof(FCGI_BeginRequestBody);
  TSDebug(PLUGIN_NAME, "Header Len: %ld ", state_->pBuffInc - state_->buff);
  // FCGI Params headers
  state_->header = createHeader(FCGI_PARAMS);
  int len = 0, nb = 0;
  std::map<string, string>::iterator it;
  for (it = state_->requestHeaders.begin(); it != state_->requestHeaders.end(); ++it) {
    nb = serializeNameValue(state_->pBuffInc, it);
    len += nb;
  }
  state_->header->contentLengthB0 = BYTE_0(len);
  state_->header->contentLengthB1 = BYTE_1(len);
  TSDebug(PLUGIN_NAME, "ParamsLen: %d ContLenB0: %d ContLenB1: %d", len, state_->header->contentLengthB0,
          state_->header->contentLengthB1);
  serialize(state_->pBuffInc, state_->header, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);

  for (it = state_->requestHeaders.begin(); it != state_->requestHeaders.end(); ++it) {
    nb = serializeNameValue(state_->pBuffInc, it);
    state_->pBuffInc += nb;
  }

  state_->header->contentLengthB0 = 0;
  state_->header->contentLengthB1 = 0;
  serialize(state_->pBuffInc, state_->header, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);

  string str("POST");
  if (str.compare(state_->requestHeaders["REQUEST_METHOD"]) == 0) {
    TSDebug(PLUGIN_NAME, "serializing post data");
    int dataLen        = 0;
    state_->postHeader = createHeader(FCGI_STDIN);
    dataLen            = postData.length();

    state_->postHeader->contentLengthB0 = BYTE_0(dataLen);
    state_->postHeader->contentLengthB1 = BYTE_1(dataLen);
    serialize(state_->pBuffInc, state_->postHeader, sizeof(FCGI_Header));
    state_->pBuffInc += sizeof(FCGI_Header);
    // serializePostData(state_->pBuffInc,postData);
    memcpy(state_->pBuffInc, postData.c_str(), dataLen);
    state_->pBuffInc += dataLen;

    state_->postHeader->contentLengthB0 = 0;
    state_->postHeader->contentLengthB1 = 0;
    serialize(state_->pBuffInc, state_->postHeader, sizeof(FCGI_Header));
    state_->pBuffInc += sizeof(FCGI_Header);
    TSDebug(PLUGIN_NAME, "Post Header Len: %ld ", state_->pBuffInc - state_->buff);
  }

  return state_->request;
}

unsigned char *
FCGIClientRequest::addClientRequest(string data, int &dataLen)
{
  dataLen = state_->pBuffInc - state_->buff;
  return state_->buff;
}

void
FCGIClientRequest::serialize(uchar *buffer, void *st, size_t size)
{
  memcpy(buffer, st, size);
}

uint32_t
FCGIClientRequest::serializeNameValue(uchar *buffer, std::map<string, string>::iterator it)
{
  uchar *p = buffer;
  uint32_t nl, vl;
  nl = it->first.length();
  vl = it->second.length();

  if (nl < 128) {
    *p++ = BYTE_0(nl);
  } else {
    *p++ = BYTE_0(nl);
    *p++ = BYTE_1(nl);
    *p++ = BYTE_2(nl);
    *p++ = BYTE_3(nl);
  }

  if (vl < 128) {
    *p++ = BYTE_0(vl);
  } else {
    *p++ = BYTE_0(vl);
    *p++ = BYTE_1(vl);
    *p++ = BYTE_2(vl);
    *p++ = BYTE_3(vl);
  }
  memcpy(p, it->first.c_str(), nl);
  p += nl;
  memcpy(p, it->second.c_str(), vl);
  p += vl;
  return p - buffer;
}

uint32_t
FCGIClientRequest::serializePostData(uchar *buffer, std::string str)
{
  uchar *p = buffer;
  uint32_t nl;
  nl = str.length();
  if (nl < 128)
    *p++ = BYTE_0(nl);
  else {
    *p++ = BYTE_0(nl);
    *p++ = BYTE_1(nl);
    *p++ = BYTE_2(nl);
    *p++ = BYTE_3(nl);
  }
  memcpy(p, str.c_str(), nl);
  p += nl;
  return p - buffer;
  // return 0;
}

void
FCGIClientRequest::fcgiHeaderSetRequestId(FCGI_Header *h, int request_id)
{
  h->requestIdB0 = BYTE_0(request_id);
  h->requestIdB1 = BYTE_1(request_id);
}

void
FCGIClientRequest::fcgiHeaderSetContentLen(FCGI_Header *h, uint16_t len)
{
  h->contentLengthB0 = BYTE_0(len);
  h->contentLengthB1 = BYTE_1(len);
}

uint32_t
FCGIClientRequest::fcgiHeaderGetContentLen(FCGI_Header *h)
{
  printf("\ncontentLengthB1: %d ,contentLengthB0 : %d ,After shieft "
         "content_len_hi: %d",
         h->contentLengthB1, h->contentLengthB0, (h->contentLengthB1 << 8));
  return (h->contentLengthB1 << 8) + h->contentLengthB0;
}

FCGIRecordList *
FCGIClientRequest::fcgiRecordCreate()
{
  FCGIRecordList *tmp = (FCGIRecordList *)TSmalloc(sizeof(FCGIRecordList));
  tmp->header         = (FCGI_Header *)calloc(sizeof(FCGI_Header), 1);
  tmp->next           = nullptr;
  tmp->state          = (FCGI_State)0;
  tmp->length         = 0;
  tmp->offset         = 0;
  return tmp;
}

int
FCGIClientRequest::fcgiProcessHeader(uchar ch, FCGIRecordList *rec)
{
  FCGI_Header *h;
  FCGI_State *state = &(rec->state);
  h                 = rec->header;

  switch (*state) {
  case fcgi_state_version:
    h->version = ch;
    *state     = fcgi_state_type;
    break;
  case fcgi_state_type:
    h->type = ch;
    *state  = fcgi_state_request_id_hi;
    break;
  case fcgi_state_request_id_hi:
    h->requestIdB1 = ch;
    *state         = fcgi_state_request_id_lo;
    break;
  case fcgi_state_request_id_lo:
    h->requestIdB0 = ch;
    *state         = fcgi_state_content_len_hi;
    break;
  case fcgi_state_content_len_hi:
    h->contentLengthB1 = ch;
    *state             = fcgi_state_content_len_lo;
    break;
  case fcgi_state_content_len_lo:
    h->contentLengthB0 = ch;
    *state             = fcgi_state_padding_len;
    break;
  case fcgi_state_padding_len:
    h->paddingLength = ch;
    *state           = fcgi_state_reserved;
    break;
  case fcgi_state_reserved:
    h->reserved = ch;
    *state      = fcgi_state_content_begin;
    break;

  case fcgi_state_content_begin:
  case fcgi_state_content_proc:
  case fcgi_state_padding:
  case fcgi_state_done:
    return FCGI_PROCESS_DONE;
  }
  return FCGI_PROCESS_AGAIN;
}

int
FCGIClientRequest::fcgiProcessContent(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec)
{
  size_t tot_len, con_len, cpy_len, offset, nb = end_buf - *beg_buf;
  FCGI_State *state = &(rec->state);
  FCGI_Header *h    = rec->header;
  offset            = rec->offset;

  if (*state == fcgi_state_padding) {
    *state = fcgi_state_done;
    *beg_buf += (size_t)((int)rec->length - (int)offset + (int)h->paddingLength);
    return FCGI_PROCESS_DONE;
  }

  con_len = rec->length - offset;
  tot_len = con_len + h->paddingLength;

  if (con_len <= nb)
    cpy_len = con_len;
  else {
    cpy_len = nb;
  }

  memcpy(rec->content + offset, *beg_buf, cpy_len);

  if (tot_len <= nb) {
    rec->offset += tot_len;
    *state = fcgi_state_done;
    *beg_buf += tot_len;
    return FCGI_PROCESS_DONE;
  } else if (con_len <= nb) {
    /* Have to still skip all or some of padding */
    *state = fcgi_state_padding;
    rec->offset += nb;
    *beg_buf += nb;
    return FCGI_PROCESS_AGAIN;
  } else {
    rec->offset += nb;
    *beg_buf += nb;
    return FCGI_PROCESS_AGAIN;
  }

  return 0;
}

int
FCGIClientRequest::fcgiProcessRecord(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec)
{
  int rv;
  while (rec->state < fcgi_state_content_begin) {
    if ((rv = fcgiProcessHeader(**beg_buf, rec)) == FCGI_PROCESS_ERR)
      return FCGI_PROCESS_ERR;
    (*beg_buf)++;
    if (*beg_buf == end_buf)
      return FCGI_PROCESS_AGAIN;
  }
  if (rec->state == fcgi_state_content_begin) {
    rec->length  = fcgiHeaderGetContentLen(rec->header);
    rec->content = (uchar *)TSmalloc(rec->length);
    rec->state   = (FCGI_State)(int(rec->state) + 1);
  }

  return fcgiProcessContent(beg_buf, end_buf, rec);
}

void
FCGIClientRequest::fcgiProcessBuffer(uchar *beg_buf, uchar *end_buf, FCGIRecordList **head)
{
  FCGIRecordList *tmp, *h;
  if (*head == nullptr)
    *head = fcgiRecordCreate();
  h       = *head;

  while (1) {
    if (h->state == fcgi_state_done) {
      tmp     = h;
      *head   = fcgiRecordCreate();
      h       = *head;
      h->next = tmp;
    }

    if (fcgiProcessRecord(&beg_buf, end_buf, h) == FCGI_PROCESS_DONE) {
      if (h->header->type == FCGI_STDOUT) {
        printf("\n\nwriting to stdout stream\n\n");
      }
    }

    if (beg_buf == end_buf)
      return;
  }
}

void
FCGIClientRequest::fcgiDecodeRecordChunk(uchar *beg_buf, size_t remain)
{
  fcgiProcessBuffer((uchar *)beg_buf, (uchar *)beg_buf + (size_t)remain, &state_->records);
}

string
FCGIClientRequest::writeToServerObj()
{
  FCGIRecordList *rec;
  std::map<int, string> res;
  string serverResponse;
  int i = 0;

  for (rec = state_->records; rec != nullptr; rec = rec->next) {
    if (rec->header->type == FCGI_STDOUT) {
      string str1((const char *)rec->content, rec->length);
      res[i] = str1;
      i++;
    }
  }
  serverResponse = "HTTP/1.0 200 OK\r\n";
  while (i >= 0) {
    serverResponse += res[i];
    i--;
  }
  return serverResponse;
}

void
FCGIClientRequest::print_bytes(uchar *buf, int n)
{
  int i;
  printf("{");
  for (i = 0; i < n; i++)
    printf("%02x", buf[i]);
  printf("}\n");
}
