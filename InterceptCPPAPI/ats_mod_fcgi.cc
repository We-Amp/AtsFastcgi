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
