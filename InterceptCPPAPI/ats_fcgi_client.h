#ifndef ATS_FCGI_CLIENT_H
#define ATS_FCGI_CLIENT_H

#include "fcgi_protocol.h"
#include <atscppapi/noncopyable.h>
#include <iterator>
#include <map>
#include <string>
#include <ts/ts.h>
#include <cstring> //for memcpy

#define BUF_SIZE 5000

/* Bytes from LSB to MSB 0..3 */

#define BYTE_0(x) ((x)&0xff)
#define BYTE_1(x) ((x) >> 8 & 0xff)
#define BYTE_2(x) ((x) >> 16 & 0xff)
#define BYTE_3(x) ((x) >> 24 | 0x80)

typedef unsigned char uchar;

#define PRINT_OPAQUE_STRUCT(p) print_mem((p), sizeof(*(p)))

#define FCGI_PROCESS_AGAIN 1
#define FCGI_PROCESS_DONE 2
#define FCGI_PROCESS_ERR 3

namespace ats_plugin
{
using namespace atscppapi;

typedef enum {
  fcgi_state_version = 0,
  fcgi_state_type,
  fcgi_state_request_id_hi,
  fcgi_state_request_id_lo,
  fcgi_state_content_len_hi,
  fcgi_state_content_len_lo,
  fcgi_state_padding_len,
  fcgi_state_reserved,
  fcgi_state_content_begin,
  fcgi_state_content_proc,
  fcgi_state_padding,
  fcgi_state_done
} FCGI_State;

struct FCGIClientState;

struct FCGIRecordList {
  FCGI_Header *header;
  uchar *content;
  FCGI_State state;
  size_t length, offset;

  FCGIRecordList() : content(nullptr), state(FCGI_State::fcgi_state_version), length(0), offset(0)
  {
    header = (FCGI_Header *)TSmalloc(sizeof(FCGI_Header));
    memset(header, 0, sizeof(FCGI_Header));
  };

  ~FCGIRecordList()
  {
    TSfree(header);
    TSfree(content);
  }
};

class FCGIClientRequest
{
public:
  std::string postData;
  FCGIClientRequest(int request_id, TSHttpTxn txn);
  ~FCGIClientRequest();

  std::map<std::string, std::string> GenerateFcgiRequestHeaders();
  void printFCGIRequestHeaders();

  // Request Creation
  FCGI_BeginRequest *createBeginRequest();
  FCGI_Header *createHeader(unsigned char type);
  void postBodyChunk();
  void emptyParam();

  void serialize(uchar *buffer, void *st, size_t size);
  void fcgiHeaderSetRequestId(FCGI_Header *h, int request_id);
  void fcgiHeaderSetContentLen(FCGI_Header *h, uint16_t len);
  uint32_t fcgiHeaderGetContentLen(FCGI_Header *h);

  uint32_t serializeNameValue(uchar *buffer, std::map<std::string, std::string>::iterator it);
  unsigned char *addClientRequest(int &);

  // Response Decoding member functions
  bool fcgiProcessBuffer(uchar *beg_buf, uchar *end_buf, std::ostringstream &output);
  FCGIRecordList *fcgiRecordCreate();
  int fcgiProcessHeader(uchar ch, FCGIRecordList *rec);
  int fcgiProcessContent(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec);
  int fcgiProcessRecord(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec);

  bool fcgiDecodeRecordChunk(uchar *beg_buf, size_t remain, std::ostringstream &output);

  void print_bytes(uchar *buf, int n);

protected:
  struct FCGIClientState *state_;

private:
  bool first_chunk;
  FCGIRecordList *_headerRecord;
};
}

#endif
