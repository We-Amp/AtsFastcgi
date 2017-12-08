#ifndef ATS_FCGI_CONFIG_H
#define ATS_FCGI_CONFIG_H

#include <list>
#include <map>
#include <stdint.h>
#include <string>
#include <ts/ts.h>
#include <utility>

#define PLUGIN_NAME "ats_mod_fcgi"
#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"
#pragma once
namespace atsfcgiconfig
{
typedef enum {
  fcgiEnabled,
  fcgiHostname,
  fcgiServerIp,
  fcgiServerPort,
  fcgiInclude,
  fcgiDocumentRoot,
  fcgiMinConnections,
  fcgiMaxConnections,
  fcgiRequestQueueSize
} FcgiConfigKey;
typedef enum {
  gatewayInterface,
  serverSoftware,
  queryString,
  requestMethod,
  contentType,
  contentLength,
  scriptFilename,
  scriptName,
  requestUri,
  documentUri,
  documentRoot,
  serverProtocol,
  remoteAddr,
  remotePort,
  serverAddr,
  serverPort,
  serverName
} FcgiParamKey;

typedef std::map<uint32_t, int8_t> UintMap;
typedef std::map<std::string, std::string> FCGIParams;

class FcgiPluginConfig
{
  bool enabled;
  TSMgmtString hostname;
  TSMgmtString server_ip;
  TSMgmtString server_port;
  TSMgmtString include;
  FCGIParams *params;
  TSMgmtString document_root;
  TSMgmtInt min_connections, max_connections, request_queue_size;

public:
  FcgiPluginConfig()
    : enabled(true),
      hostname(nullptr),
      server_ip(nullptr),
      server_port(nullptr),
      include(nullptr),
      params(nullptr),
      document_root(nullptr),
      min_connections(0),
      max_connections(0),
      request_queue_size(0)
  {
  }

  ~FcgiPluginConfig()
  {
    hostname           = nullptr;
    server_ip          = nullptr;
    server_port        = nullptr;
    include            = nullptr;
    document_root      = nullptr;
    min_connections    = 0;
    max_connections    = 0;
    request_queue_size = 0;
  }

  FcgiPluginConfig *initConfig(const char *fn);
  bool getFcgiEnabledStatus();
  void setFcgiEnabledStatus(bool val);

  TSMgmtString getHostname();
  void setHostname(char *str);
  TSMgmtString getServerIp();
  void setServerIp(char *str);
  TSMgmtString getServerPort();
  void setServerPort(char *str);
  TSMgmtString getIncludeFilePath();
  void setIncludeFilePath(char *str);
  FCGIParams *getFcgiParams();
  void setFcgiParams(FCGIParams *params);
  TSMgmtString getDocumentRootDir();
  void setDocumentRootDir(char *str);
  TSMgmtInt getMinConnLength();
  void setMinConnLength(int64_t minLen);
  TSMgmtInt getMaxConnLength();
  void setMaxConnLength(int64_t maxLen);
  TSMgmtInt getRequestQueueSize();
  void setRequestQueueSize(int64_t queueSize);
};

class FcgiPluginData
{
  UintMap *active_hash_map;
  TSMutex mutex;
  uint64_t seq_id;
  int txn_slot;
  FcgiPluginConfig *global_config;
  TSHRTime last_gc_time;
  bool read_while_writer;
  int tol_global_hook_reqs;
  int tol_remap_hook_reqs;
  int tol_non_cacheable_reqs;
  int tol_got_passed_reqs;

public:
  FcgiPluginData()
    : active_hash_map(nullptr),
      mutex(0),
      seq_id(0),
      txn_slot(0),
      global_config(nullptr),
      last_gc_time(0),
      read_while_writer(0),
      tol_global_hook_reqs(0),
      tol_remap_hook_reqs(0),
      tol_non_cacheable_reqs(0),
      tol_got_passed_reqs(0){
        // TSDebug(PLUGIN_NAME, "FCGIPluginData Initialised.");
      };
  ~FcgiPluginData();
  FcgiPluginConfig *getGlobalConfigObj();
  void setGlobalConfigObj(FcgiPluginConfig *config);
};
}

#endif /*ATS_FCGI_CONFIG_H*/
