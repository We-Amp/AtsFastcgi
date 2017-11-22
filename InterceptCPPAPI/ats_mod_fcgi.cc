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
#include "utils_internal.h"

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
Logger log;
GlobalPlugin *plugin;
fcgiPluginData *plugin_data;
}
using namespace fcgiGlobal;

class FastCGIGlobalPlugin : public GlobalPlugin
{
public:
  FastCGIGlobalPlugin() : GlobalPlugin(true) { GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS); }
  void
  handleReadRequestHeaders(Transaction &transaction) override
  {
    if (transaction.getClientRequest().getUrl().getPath().find(".php") != string::npos) {
      transaction.addPlugin(new FastCGIIntercept(transaction));
    }
    transaction.resume();
  }
};

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin(ATS_MODULE_FCGI_NAME, "apache", "dev@trafficserver.apache.org");

  plugin_data = getFCGIPlugin();

  if (argc > 1) {
    plugin_data->global_config = initConfig(argv[1]);
  } else {
    plugin_data->global_config = initConfig(nullptr);
  }

  if (plugin_data->global_config->enabled) {
    plugin = new FastCGIGlobalPlugin();
    TSDebug(PLUGIN_NAME, " plugin loaded into ATS. Transaction_slot = %d", plugin_data->txn_slot);
  } else {
    TSDebug(PLUGIN_NAME, " plugin is disabled.");
  }
}
