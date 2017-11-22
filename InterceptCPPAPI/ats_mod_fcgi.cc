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
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin(ATS_MODULE_FCGI_NAME, "apache", "dev@trafficserver.apache.org");
  // cout << "Registered and invoked the ats_mod_fcgi Plugin into ATS." << endl;

  // Create a new logger
  // This will create a log file with the name logger_example.log (since we left
  // off
  //    the extension it will automatically add .log)
  //
  // The second argument is timestamp, which will force a timestamp on every log
  // message
  //  this is enabled by default.
  // The third argument is renaming enabled, which means if a log already exists
  // with that
  //  name it will try logger_example.1 and so on, this is enabled by default.
  // The fourth argument is the initial logging level this can always be changed
  // with log.setLogLevel().
  //  the default log level is LOG_LEVEL_INFO.
  // The fifth argument is to enable log rolling, this is enabled by default.
  // The sixth argument is the freuqency in which we will roll the logs, 300
  // seconds is very low,
  //  the default for this argument is 3600.
  // log.init(ATS_MOD_LOG_FILENAME, true,true, Logger::LOG_LEVEL_DEBUG, true,
  // 300);

  // Now that we've initialized a logger we can do all kinds of fun things on
  // it:
  // log.setRollingEnabled(true);        // already done via log.init, just an
  // example.
  // log.setRollingIntervalSeconds(300); // already done via log.init

  // You have two ways to log to a logger, you can log directly on the object
  // itself:
  // log.logInfo("Hello World from: %s", argv[0]);

  // Alternatively you can take advantage of the super helper macros for logging
  // that will include the file, function, and line number automatically as part
  // of the log message:
  // LOG_INFO(log, "Hello World with more info from: %s", argv[0]);

  // This will hurt performance, but it's an option that's always available to
  // you
  // to force flush the logs. Otherwise TrafficServer will flush the logs around
  // once every second. You should really avoid flushing the log unless it's
  // really necessary.
  // log.flush();
  // changing log level with below call
  // log.setLogLevel(Logger::LOG_LEVEL_INFO);

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

  //  plugin = new FastCGIGlobalPlugin();
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the TSRemapAPI plugin.
//

// TSReturnCode
// TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
// {
//   if (!api_info) {
//     strncpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument",
//     errbuf_size - 1);
//     return TS_ERROR;
//   }

//   if (api_info->size < sizeof(TSRemapInterface)) {
//     strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface
//     structure", errbuf_size - 1);
//     return TS_ERROR;
//   }

//   if (api_info->tsremap_version < TSREMAP_VERSION) {
//     snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version
//     %ld.%ld", api_info->tsremap_version >> 16,
//              (api_info->tsremap_version & 0xffff));
//     return TS_ERROR;
//   }

//   fcgiPluginData *plugin_data = getFCGIPlugin();

//   TSDebug(PLUGIN_NAME, "Remap plugin is succesfully initialized, txn_slot =
//   %d", plugin_data->txn_slot);
//   return TS_SUCCESS;
// }

// TSReturnCode
// TSRemapNewInstance(int argc, char *argv[], void **ih, char *, int)
// {
//   if (argc > 2) {
//     *ih = static_cast<fcgiPluginConfig *>(initConfig(argv[2]));
//   } else {
//     *ih = static_cast<fcgiPluginConfig *>(initConfig(nullptr));
//   }

//   return TS_SUCCESS;
// }

// void
// TSRemapDeleteInstance(void *ih)
// {
//   fcgiPluginConfig *config = static_cast<fcgiPluginConfig *>(ih);

//   if (nullptr != config->required_header) {
//     TSfree(config->required_header);
//   }

//   TSfree(config);
// }

// ///////////////////////////////////////////////////////////////////////////////
// // This is the main "entry" point for the plugin, called for every request.
// //
// TSRemapStatus
// TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo * /* rri ATS_UNUSED
// */)
// {
//   TSHttpTxn txnp            = static_cast<TSHttpTxn>(rh);
//   fcgiPluginData *plugin_data = getFCGIPlugin();
//   fcgiTxnData *txn_data       = getFCGITxnData(txnp, true, true);

//   txn_data->config = reinterpret_cast<fcgiPluginConfig *>(ih);

//   if (!plugin_data->global_config || !plugin_data->global_config->enabled) {
//     if (txn_data->config->enabled) {
//       TSCont contp = TSContCreate(collapsedConnectionMainHandler, nullptr);
//       TSHttpTxnHookAdd(txnp, TS_HTTP_POST_REMAP_HOOK, contp);

//       txn_data->contp = contp;
//       TSHttpTxnArgSet(txnp, plugin_data->txn_slot, txn_data);
//     } else {
//       // global & remap were both disabled
//       txn_data->txnp = nullptr;
//       freeCcTxnData(txn_data);
//     }
//   } else {
//     // if globally enabled, set txn_data for remap config
//     TSHttpTxnArgSet(txnp, plugin_data->txn_slot, txn_data);
//   }

//   return TSREMAP_NO_REMAP;
// }
