#ifndef ATS_FCGI_CONFIG_H
#define ATS_FCGI_CONFIG_H

#include <stdint.h>
#include <map>
#include <list>
#include <utility>

#define PLUGIN_NAME "ats_mod_fcgi"
#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"

#define DEFAULT_HOSTNAME "localhost"
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT "60000"

typedef enum {
  fcgiEnabled,
  fcgiHostname,
  fcgiServerIp,
  fcgiServerPort,
} fcgiConfigKey;

struct PassRecord {
    int64_t timeout;
    uint32_t hash_key;
 };
  
  typedef std::map<uint32_t, int8_t> UintMap;
  typedef std::list<PassRecord> UsecList;
  

typedef struct {
  bool enabled;
  TSMgmtString hostname;
  TSMgmtString server_ip;
  TSMgmtString server_port;

  //TSMgmtString root_directory;
  //int required_hostname_len;
} fcgiPluginConfig;


typedef struct {
    UintMap *active_hash_map;
    TSMutex mutex;
    uint64_t seq_id;
    int txn_slot;
    fcgiPluginConfig *global_config;
    UsecList *keep_pass_list;
    TSHRTime last_gc_time;
    bool read_while_writer;
    int tol_global_hook_reqs;
    int tol_remap_hook_reqs;
    int tol_collapsed_reqs;
    int tol_non_cacheable_reqs;
    int tol_got_passed_reqs;
    int cur_hash_entries;
    int cur_keep_pass_entries;
    int max_hash_entries;
    int max_keep_pass_entries;
  } fcgiPluginData;

  
  fcgiPluginConfig *initConfig(const char *fn);
  fcgiPluginData *getFCGIPlugin();


#endif /*ATS_FCGI_CONFIG_H*/