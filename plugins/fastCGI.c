/** @file

  A brief file description

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

/* server-transform.c:  an example program that sends response content
 *                      to a server to be transformed, and sends the
 *                      transformed content to the client
 *
 *
 *  Usage:
 *    server-transform.so
 *
 *
 */

/* The protocol spoken with the server is simple. The plugin sends the
   content-length of the document being transformed as a 4-byte
   integer and then it sends the document itself. The first 4-bytes of
   the server response are a status code/content length. If the code
   is greater than 0 then the plugin assumes transformation was
   successful and uses the code as the content length of the
   transformed document. If the status code is less than or equal to 0
   then the plugin bypasses transformation and sends the original
   document on through.

   The plugin does a fair amount of error checking and tries to bypass
   transformation in many cases such as when it can't connect to the
   server. This example plugin simply connects to port 7 on localhost,
   which on our solaris machines (and most unix machines) is the echo
   port. One nicety about the protocol is that simply having the
   server echo back what it is sent results in a "null"
   transformation. (i.e. A transformation which does not modify the
   content). */

   #include <string.h>
   #include <stdio.h>
   
   #include <netinet/in.h>
   
   #include "ts/ts.h"
   #include "ts/ink_defs.h"
   
   #include "fcgi_defs.h"
   #include "fcgi_header.h"
   
   #define BUF_SIZE 5000
   #define FCGI_SERVER "127.0.0.1"
   #define FCGI_PORT "6000"
   #define MAXDATASIZE 1000
   
   #define N_NameValue 27
   fcgi_name_value nvs[N_NameValue] = {
   {"SCRIPT_FILENAME", "/var/www/html/abc.php"},
   {"SCRIPT_NAME", "/abc.php"},
   {"DOCUMENT_ROOT", "/var/www/"},
   {"REQUEST_URI", "/abc.php"},
   {"PHP_SELF", "/abc.php"},
   {"TERM", "linux"},
   {"PATH", ""},
   {"PHP_FCGI_CHILDREN", "2"},
   {"PHP_FCGI_MAX_REQUESTS", "1000"},
   {"FCGI_ROLE", "RESPONDER"},
   {"SERVER_SOFTWARE", "lighttpd/1.4.29"},
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
   



   #define STATE_BUFFER 1
   #define STATE_CONNECT 2
   #define STATE_WRITE 3
   #define STATE_READ_STATUS 4
   #define STATE_READ 5
   #define STATE_BYPASS 6
   
   typedef struct {
     int state;
     TSHttpTxn txn;
   
     TSIOBuffer input_buf;
     TSIOBufferReader input_reader;
   
     TSIOBuffer output_buf;
     TSIOBufferReader output_reader;
     TSVConn output_vc;
     TSVIO output_vio;
   
     TSAction pending_action;
     TSVConn server_vc;
     TSVIO server_vio;
   
     int content_length;
   } TransformData;
   
   static int transform_handler(TSCont contp, TSEvent event, void *edata);
   
   static in_addr_t server_ip;
   static int server_port;
   
   static TSCont
   transform_create(TSHttpTxn txnp)
   {
     TSCont contp;
     TransformData *data;
   
     contp = TSTransformCreate(transform_handler, txnp);
   
     data                 = (TransformData *)TSmalloc(sizeof(TransformData));
     data->state          = STATE_BUFFER;
     data->txn            = txnp;
     data->input_buf      = NULL;
     data->input_reader   = NULL;
     data->output_buf     = NULL;
     data->output_reader  = NULL;
     data->output_vio     = NULL;
     data->output_vc      = NULL;
     data->pending_action = NULL;
     data->server_vc      = NULL;
     data->server_vio     = NULL;
     data->content_length = 0;
   
     TSContDataSet(contp, data);
     return contp;
   }
   
   static void
   transform_destroy(TSCont contp)
   {
     TransformData *data;
   
     data = (TransformData *)TSContDataGet(contp);
     if (data != NULL) {
       if (data->input_buf) {
         TSIOBufferDestroy(data->input_buf);
       }
   
       if (data->output_buf) {
         TSIOBufferDestroy(data->output_buf);
       }
   
       if (data->pending_action) {
         TSActionCancel(data->pending_action);
       }
   
       if (data->server_vc) {
         TSVConnAbort(data->server_vc, 1);
       }
   
       TSfree(data);
     } else {
       TSError("[server_transform] Unable to get Continuation's Data. TSContDataGet returns NULL");
     }
   
     TSContDestroy(contp);
   }
   
   static int
   transform_connect(TSCont contp, TransformData *data)
   {
     TSAction action;
     int content_length;
     struct sockaddr_in ip_addr;
   
     data->state = STATE_CONNECT;
   
     content_length = TSIOBufferReaderAvail(data->input_reader);
     if (content_length >= 0) {
       data->content_length = content_length;
       data->content_length = htonl(data->content_length);
   
       /* Prepend the content length to the buffer.
        * If we decide to not send the content to the transforming
        * server then we need to make sure and skip input_reader
        * over the content length.
        */
   
       {
         TSIOBuffer temp;
         TSIOBufferReader tempReader;
   
         temp       = TSIOBufferCreate();
         tempReader = TSIOBufferReaderAlloc(temp);
   
        /*******FCGI Request Generate Start Point   ********/
            uint16_t req_id = 1;
            uint16_t len=0;
            int nb, i;
            unsigned char *p, *buf, *rbuf;
            fcgi_header* head;
            fcgi_begin_request* begin_req = create_begin_request(req_id);
        
            rbuf = (unsigned char *)malloc(BUF_SIZE);
            buf  = (unsigned char *)malloc(BUF_SIZE);
            p = buf;
            serialize(p, begin_req->header, sizeof(fcgi_header));
            p += sizeof(fcgi_header);
            serialize(p, begin_req->body, sizeof(fcgi_begin_request_body));
            p += sizeof(fcgi_begin_request_body);
        
            /* Sending fcgi_params */
            head = create_header(FCGI_PARAMS, req_id);
        
            len = 0;
            /* print_bytes(buf, p-buf); */
            for(i = 0; i< N_NameValue; i++) {
                nb = serialize_name_value(p, &nvs[i]);
                len += nb;
            }
        
            head->content_len_lo = BYTE_0(len);
            head->content_len_hi = BYTE_1(len);
        
        
            serialize(p, head, sizeof(fcgi_header));
            p += sizeof(fcgi_header);
        
            for(i = 0; i< N_NameValue; i++) {
                nb = serialize_name_value(p, &nvs[i]);
                p += nb;
            }
        
            head->content_len_lo = 0;
            head->content_len_hi = 0;
        
            serialize(p, head, sizeof(fcgi_header));
            p += sizeof(fcgi_header);

        /********FCGI Request Generation end  Point     *********/





         //TSIOBufferWrite(temp, (const char *)&content_length, sizeof(int));
         TSIOBufferWrite(temp, (const char *)buf, p-buf);
         TSIOBufferCopy(temp, data->input_reader, p-buf, 0);
         TSIOBufferReaderFree(data->input_reader);
         TSIOBufferDestroy(data->input_buf);
         data->input_buf    = temp;
         data->input_reader = tempReader;
         TSDebug("strans","Content Length:%d , Buff : %p , Reader : %s",content_length,temp,data);
       }
     } else {
       TSError("[server_transform] TSIOBufferReaderAvail returns TS_ERROR");
       return 0;
     }
   
     /* TODO: This only supports IPv4, probably should be changed at some point, but
        it's an example ... */
     memset(&ip_addr, 0, sizeof(ip_addr));
     ip_addr.sin_family      = AF_INET;
     ip_addr.sin_addr.s_addr = server_ip; /* Should be in network byte order */
     ip_addr.sin_port        = server_port;
     TSDebug("strans", "net connect..");
     action = TSNetConnect(contp, (struct sockaddr const *)&ip_addr);
   
     if (!TSActionDone(action)) {
       data->pending_action = action;
     }
   
     return 0;
   }
   
   static int
   transform_write(TSCont contp, TransformData *data)
   {
     int content_length;
   
     data->state = STATE_WRITE;
   
     content_length = TSIOBufferReaderAvail(data->input_reader);

     if (content_length >= 0) {
       TSDebug("strans","Writing to server vconn...ContLeng: %d,Buffer: %p",content_length,data->input_reader);
       data->server_vio = TSVConnWrite(data->server_vc, contp, TSIOBufferReaderClone(data->input_reader), content_length);
     } else {
       TSError("[server_transform] TSIOBufferReaderAvail returns TS_ERROR");
     }
     return 0;
   }
   
   static int
   transform_read_status(TSCont contp, TransformData *data)
   {
     data->state = STATE_READ_STATUS;
   
     data->output_buf    = TSIOBufferCreate();
     data->output_reader = TSIOBufferReaderAlloc(data->output_buf);
     if (data->output_reader != NULL) {
       data->server_vio = TSVConnRead(data->server_vc, contp, data->output_buf, sizeof(int));
     } else {
       TSError("[server_transform] Error in Allocating a Reader to output buffer. TSIOBufferReaderAlloc returns NULL");
     }
   
     return 0;
   }
   
   static int
   transform_read(TSCont contp, TransformData *data)
   {
     data->state = STATE_READ;
   
     TSIOBufferDestroy(data->input_buf);
     data->input_buf    = NULL;
     data->input_reader = NULL;
     TSDebug("strans","Inside Transform Read...Reading contents from php server...");
     data->server_vio = TSVConnRead(data->server_vc, contp, data->output_buf, data->content_length);
     //TSDebug("strans","Inside Transform Read...Reading contents from php server...");
     data->output_vc  = TSTransformOutputVConnGet((TSVConn)contp);
     if (data->output_vc == NULL) {
       TSError("[server_transform] TSTransformOutputVConnGet returns NULL");
     } else {

       data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, data->content_length);
       if (data->output_vio == NULL) {
         TSError("[server_transform] TSVConnWrite returns NULL");
       }
     }
   
     return 0;
   }
   
   static int
   transform_bypass(TSCont contp, TransformData *data)
   {
     data->state = STATE_BYPASS;
   
     if (data->server_vc) {
       TSVConnAbort(data->server_vc, 1);
       data->server_vc  = NULL;
       data->server_vio = NULL;
     }
   
     if (data->output_buf) {
       TSIOBufferDestroy(data->output_buf);
       data->output_buf    = NULL;
       data->output_reader = NULL;
     }
   
     TSIOBufferReaderConsume(data->input_reader, sizeof(int));
     data->output_vc = TSTransformOutputVConnGet((TSVConn)contp);
     if (data->output_vc == NULL) {
       TSError("[server_transform] TSTransformOutputVConnGet returns NULL");
     } else {
       data->output_vio = TSVConnWrite(data->output_vc, contp, data->input_reader, TSIOBufferReaderAvail(data->input_reader));
       if (data->output_vio == NULL) {
         TSError("[server_transform] TSVConnWrite returns NULL");
       }
     }
     return 1;
   }
   
   static int
   transform_buffer_event(TSCont contp, TransformData *data, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
   {
     TSVIO write_vio;
     int towrite;
     int avail;
   
     if (!data->input_buf) {
       data->input_buf    = TSIOBufferCreate();
       data->input_reader = TSIOBufferReaderAlloc(data->input_buf);
     }
   
     /* Get the write VIO for the write operation that was performed on
        ourself. This VIO contains the buffer that we are to read from
        as well as the continuation we are to call when the buffer is
        empty. */
     write_vio = TSVConnWriteVIOGet(contp);
   
     /* We also check to see if the write VIO's buffer is non-NULL. A
        NULL buffer indicates that the write operation has been
        shutdown and that the continuation does not want us to send any
        more WRITE_READY or WRITE_COMPLETE events. For this buffered
        transformation that means we're done buffering data. */
     if (!TSVIOBufferGet(write_vio)) {
       return transform_connect(contp, data);
     }
   
     /* Determine how much data we have left to read. For this server
        transform plugin this is also the amount of data we have left
        to write to the output connection. */
     towrite = TSVIONTodoGet(write_vio);
     if (towrite > 0) {
       /* The amount of data left to read needs to be truncated by
          the amount of data actually in the read buffer. */
       avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
       if (towrite > avail) {
         towrite = avail;
       }
   
       if (towrite > 0) {
         /* Copy the data from the read buffer to the input buffer. */
         TSIOBufferCopy(data->input_buf, TSVIOReaderGet(write_vio), towrite, 0);
   
         /* Tell the read buffer that we have read the data and are no
            longer interested in it. */
         TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);
   
         /* Modify the write VIO to reflect how much data we've
            completed. */
         TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
       }
     }
   
     /* Now we check the write VIO to see if there is data left to
        read. */
     if (TSVIONTodoGet(write_vio) > 0) {
       /* Call back the write VIO continuation to let it know that we
          are ready for more data. */
       TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
     } else {
       /* Call back the write VIO continuation to let it know that we
          have completed the write operation. */
       TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
   
       /* start compression... */
       return transform_connect(contp, data);
     }
   
     return 0;
   }
   
   static int
   transform_connect_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
   { TSDebug("strans", "Inside transform Connect event [%d], data->state = [%d]", event, data->state);
     switch (event) {
     case TS_EVENT_NET_CONNECT:
       TSDebug("strans", "connected");
   
       data->pending_action = NULL;
       data->server_vc      = (TSVConn)edata;
       return transform_write(contp, data);
     case TS_EVENT_NET_CONNECT_FAILED:
       TSDebug("strans", "connect failed");
       data->pending_action = NULL;
       return transform_bypass(contp, data);
     default:
       break;
     }
   
     return 0;
   }
   
   static int
   transform_write_event(TSCont contp, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
   {
      TSDebug("strans", "Inside transform write event [%d], data->state = [%d]", event, data->state);
     switch (event) {
     case TS_EVENT_VCONN_WRITE_READY:
       TSVIOReenable(data->server_vio);
       break;
     case TS_EVENT_VCONN_WRITE_COMPLETE:
       return transform_read_status(contp, data);
     case TS_EVENT_ERROR:
       return transform_bypass(contp, data);
     case TS_EVENT_IMMEDIATE:
       TSVIOReenable(data->server_vio);
       break;
     default:
       /* An error occurred while writing to the server. Close down
          the connection to the server and bypass. */
       return transform_bypass(contp, data);
     }
   
     return 0;
   }
   
   static int
   transform_read_status_event(TSCont contp, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
   { TSDebug("strans", "Inside transform Read status event [%d], data->state = [%d]", event, data->state);
     switch (event) {
     case TS_EVENT_ERROR:
     case TS_EVENT_VCONN_EOS:
       return transform_bypass(contp, data);
     case TS_EVENT_VCONN_READ_COMPLETE:
     
       if (TSIOBufferReaderAvail(data->output_reader) == sizeof(int)) {
         TSIOBufferBlock blk;
         char *buf;
         void *buf_ptr;
         int64_t avail;
         int64_t read_nbytes = sizeof(int);
         int64_t read_ndone  = 0;
   
         buf_ptr = &data->content_length;
         while (read_nbytes > 0) {
           blk        = TSIOBufferReaderStart(data->output_reader);
           buf        = (char *)TSIOBufferBlockReadStart(blk, data->output_reader, &avail);
           char *a = "hello";
           TSDebug("strans","Buffer contents Read %s. %s",(char *)buf,a);
           //TSDebug("strans","TSRead Buff COntains: %s ",buf);
           read_ndone = (avail >= read_nbytes) ? read_nbytes : avail;
           memcpy(buf_ptr, buf, read_ndone);
           if (read_ndone > 0) {
             TSIOBufferReaderConsume(data->output_reader, read_ndone);
             read_nbytes -= read_ndone;
             /* move ptr frwd by read_ndone bytes */
             buf_ptr = (char *)buf_ptr + read_ndone;
           }
           TSDebug("strans","Reading %ld bytes of data from php server.",read_ndone);
         }
         
         // data->content_length = ntohl(data->content_length);
         return transform_read(contp, data);
       }
       return transform_bypass(contp, data);
     default:
       break;
     }
   
     return 0;
   }
   
   static int
   transform_read_event(TSCont contp ATS_UNUSED, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
   {
      TSDebug("strans", "Inside transform Read event [%d], data->state = [%d]", event, data->state);
     switch (event) {
     case TS_EVENT_ERROR:
       TSVConnAbort(data->server_vc, 1);
       data->server_vc  = NULL;
       data->server_vio = NULL;
   
       TSVConnAbort(data->output_vc, 1);
       data->output_vc  = NULL;
       data->output_vio = NULL;
       break;
     case TS_EVENT_VCONN_EOS:
       TSVConnAbort(data->server_vc, 1);
       data->server_vc  = NULL;
       data->server_vio = NULL;
   
       TSVConnAbort(data->output_vc, 1);
       data->output_vc  = NULL;
       data->output_vio = NULL;
      TSVIOReenable(data->server_vio);     
       break;
     case TS_EVENT_VCONN_READ_COMPLETE:
       TSVConnClose(data->server_vc);
       data->server_vc  = NULL;
       data->server_vio = NULL;
   
       TSVIOReenable(data->output_vio);
       break;
     case TS_EVENT_VCONN_READ_READY:
     //TSVIOReenable(data->output_vio);
     TSVIOReenable(data->server_vio);
       break;
     case TS_EVENT_VCONN_WRITE_COMPLETE:
       TSVConnShutdown(data->output_vc, 0, 1);
       break;
     case TS_EVENT_VCONN_WRITE_READY:
       TSVIOReenable(data->server_vio);
       break;
     default:
       break;
     }
   
     return 0;
   }
   
   static int
   transform_bypass_event(TSCont contp ATS_UNUSED, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
   {TSDebug("strans", "Inside transform bypass event [%d], data->state = [%d]", event, data->state);
     switch (event) {
     case TS_EVENT_VCONN_WRITE_COMPLETE:
       TSVConnShutdown(data->output_vc, 0, 1);
       break;
     case TS_EVENT_VCONN_WRITE_READY:
     default:
       TSVIOReenable(data->output_vio);
       break;
     }
   
     return 0;
   }
   
   static int
   transform_handler(TSCont contp, TSEvent event, void *edata)
   {
     /* Check to see if the transformation has been closed by a call to
        TSVConnClose. */
     // if (TSVConnClosedGet(contp)) {
     //   TSDebug("strans", "transformation closed");
     //   transform_destroy(contp);
     //   return 0;
     // } else {
       TransformData *data;
       int val = 0;
   
       data = (TransformData *)TSContDataGet(contp);
       if (data == NULL) {
         TSError("[server_transform] Didn't get Continuation's Data. Ignoring Event..");
         return 0;
       }
       TSDebug("strans", "transform handler event [%d], data->state = [%d]", event, data->state);
   
       do {
         switch (data->state) {
         case STATE_BUFFER:
           val = transform_buffer_event(contp, data, event, edata);
           break;
         case STATE_CONNECT:
           val = transform_connect_event(contp, data, event, edata);
           break;
         case STATE_WRITE:
           val = transform_write_event(contp, data, event, edata);
           break;
         case STATE_READ_STATUS:
           val = transform_read_status_event(contp, data, event, edata);
           break;
         case STATE_READ:
           val = transform_read_event(contp, data, event, edata);
           break;
         case STATE_BYPASS:
           val = transform_bypass_event(contp, data, event, edata);
           break;
         }
       } while (val);
    // }
   
     return 0;
   }
   
   static int
   request_ok(TSHttpTxn txnp ATS_UNUSED)
   {
     /* Is the initial client request OK for transformation. This is a
        good place to check accept headers to see if the client can
        accept a transformed document. */
     return 1;
   }
   
   static int
   cache_response_ok(TSHttpTxn txnp ATS_UNUSED)
   {
     /* Is the response we're reading from cache OK for
      * transformation. This is a good place to check the cached
      * response to see if it is transformable. The default
      * behavior is to cache transformed content; therefore
      * to avoid transforming twice we will not transform
      * content served from the cache.
      */
     return 0;
   }
   
   static int findSubstr(char *inpText, char *pattern) {
       int inplen = strlen(inpText);
       while (inpText != NULL) {
           char *remTxt = inpText;
           char *remPat = pattern;
           if (strlen(remTxt) < strlen(remPat)) {
               return -1;
           }
           while (*remTxt++ == *remPat++) {
               //printf("remTxt %s \nremPath %s \n", remTxt, remPat);
               if (*remPat == '\0') {
                   //printf ("match found \n");
                   return inplen - strlen(inpText+1);
               }
               if (remTxt == NULL) {
                   return -1;
               }
           }
           remPat = pattern;
           inpText++;
       }
   }
   
   
   static int
   server_intercept_ok(TSHttpTxn txnp)
   {
     /* Is the response the server sent OK for transformation. This is
      * a good place to check the server's response to see if it is
      * transformable. In this example, we will transform only "200 OK"
      * responses.
      */
     TSMBuffer bufp;
     TSMLoc offset_loc,url_loc;
     TSReturnCode req_status;
     int retv = 0,url_length;
     char *url_str, *php_str = ".php";
     TSDebug("strans","Inside Server Intercept call");
     if (TSHttpTxnClientReqGet(txnp, &bufp, &offset_loc) == TS_SUCCESS) {
      req_status = TSHttpHdrUrlGet(bufp, offset_loc,&url_loc);
       if(req_status == TS_SUCCESS){
         url_str = TSUrlStringGet(bufp, url_loc, &url_length);
         //check wheather to intercept the request
         retv = findSubstr(url_str, php_str);
         if (retv > -1) {
             TSDebug("strans","Found the substring at position %d Intercepting URL: %s\n", retv,url_str);
             //we can proceed with intercepting http request from this point and set up vConnection to php-fastcgi
         }
         else
         {
           retv = 0;
           TSDebug("strans","You are forbidden from intercepting url: \"%s\"\n", url_str);
         }
         TSfree(url_str);
       }
       TSHandleMLocRelease(bufp, TS_NULL_MLOC, offset_loc);
     }
    return retv; 
   }
   
   static int
   transform_plugin(TSCont contp, TSEvent event, void *edata)
   {
     TSHttpTxn txnp = (TSHttpTxn)edata;
   
     switch (event) {
     case TS_EVENT_HTTP_READ_REQUEST_HDR:
       if (server_intercept_ok(txnp)) {
         TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_TRANSFORM_HOOK, transform_create(txnp));
       }
       TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
       break;
     default:
       break;
     }
     return 0;
   }
   
   void
   TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
   {
     TSPluginRegistrationInfo info;
     TSCont cont;
   
     info.plugin_name   = "server-transform";
     info.vendor_name   = "MyCompany";
     info.support_email = "ts-api-support@MyCompany.com";
   
     if (TSPluginRegister(&info) != TS_SUCCESS) {
       TSError("[server_transform] Plugin registration failed.");
     }
   
     /* connect to the echo port on localhost */
     server_ip   = (127 << 24) | (0 << 16) | (0 << 8) | (1);
     server_ip   = htonl(server_ip);
     server_port = htons(6000);
   
     cont = TSContCreate(transform_plugin, NULL);
     TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
   }
   