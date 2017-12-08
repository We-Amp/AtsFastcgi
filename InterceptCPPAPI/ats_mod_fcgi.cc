#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>

#include "ats_fcgi_config.h"
#include "ats_mod_fcgi.h"
#include "fcgi_intercept.h"
#include "ts/ink_defs.h"
#include "ts/ts.h"
#include <cinttypes>
#include <ctime>

#include "utils_internal.h"

#include "fcgi_server.h"

using namespace atscppapi;

using std::cout;
using std::endl;
using std::string;

/*
 * You should always take advantage of the LOG_LEVEL_NO_LOG,LOG_DEBUG, LOG_INFO,
 * and LOG_ERROR
 * macros available in Logger.h, they are easy to use as you can see below
 * and will provide detailed information about the logging site such as
 * filename, function name, and line number of the message
 */
namespace fcgiGlobal
{
GlobalPlugin *plugin;
atsfcgiconfig::FcgiPluginData *plugin_data;
FCGIServer *fcgi_server;
int reqId, respId;
}
using namespace fcgiGlobal;

const char reqName[]  = "plugin." PLUGIN_NAME ".reqCount";
const char respName[] = "plugin." PLUGIN_NAME ".respCount";

class FastCGIGlobalPlugin : public GlobalPlugin
{
public:
  FastCGIGlobalPlugin() : GlobalPlugin(true) { GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS); }
  void
  handleReadRequestHeaders(Transaction &transaction) override
  {
    int count = reqId - respId + 1;
    if (transaction.getClientRequest().getUrl().getPath().find(".php") != string::npos) {
      auto intercept = new FastCGIIntercept(transaction);
      if (fcgi_server->max_connections > count) {
        transaction.addPlugin(intercept);
        fcgi_server->connect(intercept);
      } else {
        TSDebug(PLUGIN_NAME, "Adding to request queue : Count %d", count);
        fcgi_server->addToRequestQueue(intercept);
      }
    }

    if (!fcgi_server->request_queue.empty()) {
      TSDebug(PLUGIN_NAME, "Popping from requst Queue.");
      auto intercept = fcgi_server->popFromRequestQueue();
      transaction.addPlugin(intercept);
      fcgi_server->connect(intercept);
    }
    transaction.resume();
  }
};

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin(ATS_MODULE_FCGI_NAME, "apache", "dev@trafficserver.apache.org");
  plugin_data                             = new atsfcgiconfig::FcgiPluginData();
  atsfcgiconfig::FcgiPluginConfig *config = new atsfcgiconfig::FcgiPluginConfig();
  if (argc > 1) {
    plugin_data->setGlobalConfigObj(config->initConfig(argv[1]));
  } else {
    plugin_data->setGlobalConfigObj(config->initConfig(nullptr));
  }
  atsfcgiconfig::FcgiPluginConfig *gConfig = plugin_data->getGlobalConfigObj();
  if (gConfig->getFcgiEnabledStatus()) {
    plugin      = new FastCGIGlobalPlugin();
    fcgi_server = new FCGIServer();
    fcgi_server->setDefaultLimits(gConfig->getMinConnLength(), gConfig->getMaxConnLength(), gConfig->getRequestQueueSize());

    // TSDebug(PLUGIN_NAME, " plugin loaded into ATS. Transaction_slot = %d", plugin_data->txn_slot);
    if (TSStatFindName(reqName, &reqId) == TS_ERROR) {
      reqId = TSStatCreate(reqName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (reqId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, reqName);
        return;
      }
    }
    TSError("[%s] %s registered with id %d", PLUGIN_NAME, reqName, reqId);

    if (TSStatFindName(respName, &respId) == TS_ERROR) {
      respId = TSStatCreate(respName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (respId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, respName);
        return;
      }
    }
    TSError("[%s] %s registered with id %d", PLUGIN_NAME, respName, respId);

#if DEBUG
    TSReleaseAssert(reqId != TS_ERROR);
    TSReleaseAssert(respId != TS_ERROR);
#endif
    // Set an initial value for our statistic.
    TSStatIntSet(reqId, 0);
    TSStatIntSet(respId, 0);

  } else {
    TSDebug(PLUGIN_NAME, " plugin is disabled.");
  }
}
