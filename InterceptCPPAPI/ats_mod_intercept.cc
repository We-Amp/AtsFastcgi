
#include "ats_mod_intercept.h"

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>

#include "ts/ink_defs.h"
#include "ts/ts.h"
#include "utils_internal.h"

#include "fcgi_config.h"
#include "server_intercept.h"
#include "server.h"

#include <regex>

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
namespace InterceptGlobal
{
GlobalPlugin *plugin;
ats_plugin::InterceptPluginData *plugin_data;
ats_plugin::Server *gServer;
int reqId, respId;
}

// For experimental purpose to keep stats of plugin request/response
const char reqName[]  = "plugin." PLUGIN_NAME ".reqCount";
const char respName[] = "plugin." PLUGIN_NAME ".respCount";

using namespace InterceptGlobal;

class InterceptGlobalPlugin : public GlobalPlugin
{
public:
  InterceptGlobalPlugin() : GlobalPlugin(true) { GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS); }
  void
  handleReadRequestHeaders(Transaction &transaction) override
  {
    string path = transaction.getClientRequest().getUrl().getPath();
    // std::smatch urlMatch;
    // std::regex e(".*.[php|js|css](p{2})?"); // matches words beginning by "sub"
    // while (std::regex_search(path, urlMatch, e)) {
    //   for (auto x : urlMatch)
    //     std::cout << x << " ";
    //   std::cout << std::endl;
    //   path = urlMatch.suffix().str();
    // }
    if (path.find("php") != string::npos) {
      TSStatIntIncrement(reqId, 1);
      auto intercept = new ats_plugin::ServerIntercept(transaction);

      int size = gServer->checkAvailability();
      TSDebug(PLUGIN_NAME, "[%s] availability : %d", __FUNCTION__, size);
      if (size) {
        transaction.addPlugin(intercept);
        gServer->connect(intercept);
        transaction.resume();
      } else {
        TSDebug(PLUGIN_NAME, "[%s] Adding to pending list. QueueLength: %lu", __FUNCTION__, gServer->pending_list.size());
        gServer->pending_list.push(intercept);
      }
    } else {
      transaction.resume();
    }
  }
};

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin(ATS_MODULE_FCGI_NAME, "apache", "dev@trafficserver.apache.org");
  plugin_data                          = new ats_plugin::InterceptPluginData();
  ats_plugin::FcgiPluginConfig *config = new ats_plugin::FcgiPluginConfig();
  if (argc > 1) {
    plugin_data->setGlobalConfigObj(config->initConfig(argv[1]));
  } else {
    plugin_data->setGlobalConfigObj(config->initConfig(nullptr));
  }
  ats_plugin::FcgiPluginConfig *gConfig = plugin_data->getGlobalConfigObj();
  if (gConfig->getFcgiEnabledStatus()) {
    plugin  = new InterceptGlobalPlugin();
    gServer = new ats_plugin::Server();
    // gServer->setDefaultLimits(gConfig->getMinConnLength(), gConfig->getMaxConnLength(), gConfig->getRequestQueueSize());

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
