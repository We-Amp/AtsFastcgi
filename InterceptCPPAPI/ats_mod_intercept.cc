
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
#include "Profiler.h"

#include <regex>
#define CONFIGURU_IMPLEMENTATION 1
#include "configuru.hpp"
using namespace configuru;

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
int reqBegId, reqEndId, respBegId, respEndId;
#if ATS_FCGI_PROFILER
ats_plugin::Profiler profiler;
#endif
}

// For experimental purpose to keep stats of plugin request/response
const char reqBegName[]  = "plugin." PLUGIN_NAME ".reqCountBeg";
const char reqEndName[]  = "plugin." PLUGIN_NAME ".reqCountEnd";
const char respBegName[] = "plugin." PLUGIN_NAME ".respCountBeg";
const char respEndName[] = "plugin." PLUGIN_NAME ".respCountEnd";

using namespace InterceptGlobal;

class InterceptGlobalPlugin : public GlobalPlugin
{
public:
  InterceptGlobalPlugin() : GlobalPlugin(true) { GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS); }
  void
  handleReadRequestHeaders(Transaction &transaction) override
  {
    string path = transaction.getClientRequest().getUrl().getPath();

    std::cout << "URL Path:" << path << std::endl;
    // TODO: Regex based url selection
    std::smatch urlMatch;
    std::regex e(".*.[wp*|php|js|css|scss|png|gif](p{2})?");
    while (std::regex_search(path, urlMatch, e)) {
      for (auto x : urlMatch)
        std::cout << x << " ";
      std::cout << std::endl;
      // path = urlMatch.suffix().str();
      // std::cout << "Path:" << path << std::endl;
      break;
    }
    std::cout << "UrlMatch" << urlMatch.str() << std::endl;
    // #if ATS_FCGI_PROFILER
    //     // Intentionally written this piece of code to record profiler stats in a file.
    //     if (path.find("_profilerStats") != string::npos) {
    //       std::cout << "Generating Dump file." << std::endl;
    //       auto p = profiler.profiles();
    //       profiler.profileLength();
    //       std::vector<ats_plugin::Profile>::iterator it;
    //       // Config resultObj = Config::object();
    //       Config resultArr = Config::array();

    //       for (it = p.begin(); it != p.end(); it++) {
    //         // std::cout << "start time: " << it->start_time() << std::endl;
    //         // std::cout << "end time: " << it->end_time() << std::endl;
    //         // std::cout << "thread id: " << it->thread_id() << std::endl;
    //         // std::cout << "Task Name: " << it->task_name() << std::endl;
    //         Config cfg = Config::object();
    //         cfg["ts"]  = it->start_time();
    //         // cfg["end_time"] = it->end_time();
    //         // cfg["dur"]  = it->end_time() - it->start_time();
    //         cfg["tid"]  = std::to_string(it->thread_id());
    //         cfg["pid"]  = it->process_id();
    //         cfg["name"] = it->task_name();
    //         // cfg["id"]   = std::to_string(it->object_id());
    //         cfg["ph"] = it->obj_stage();

    //         resultArr.push_back(cfg);
    //       }
    //       // resultObj["traceEvents"] = resultArr;
    //       // std::string json = dump_string(resultArr, JSON);
    //       dump_file("/tmp/output.json", resultArr, JSON);
    //       // std::ofstream example_file;
    //       // example_file.open("./example.json");
    //       // example_file << json;
    //       // example_file.close();
    //     }
    // #endif

    // if (path.find("php") != string::npos) {
    if (path.compare(urlMatch.str()) == 0) {
#if ATS_FCGI_PROFILER
      profiler.set_record_enabled(true);
      ats_plugin::ProfileTaker profile_taker(&profiler, "handleReadRequestHeaders", (std::size_t)&plugin, "B");
#endif

      TSStatIntIncrement(reqBegId, 1);
      auto intercept = new ats_plugin::ServerIntercept(transaction);
      gServer->connect(intercept);
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

    if (TSStatFindName(reqBegName, &reqBegId) == TS_ERROR) {
      reqBegId = TSStatCreate(reqBegName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (reqBegId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, reqBegName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, reqBegName, reqBegId);

    if (TSStatFindName(reqEndName, &reqEndId) == TS_ERROR) {
      reqEndId = TSStatCreate(reqEndName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (reqEndId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, reqEndName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, reqEndName, reqEndId);

    if (TSStatFindName(respBegName, &respBegId) == TS_ERROR) {
      respBegId = TSStatCreate(respBegName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (respBegId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, respBegName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, respBegName, respBegId);

    if (TSStatFindName(respEndName, &respEndId) == TS_ERROR) {
      respEndId = TSStatCreate(respEndName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (respEndId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, respEndName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, respEndName, respEndId);

#if DEBUG
    TSReleaseAssert(reqBegId != TS_ERROR);
    TSReleaseAssert(reqEndId != TS_ERROR);
    TSReleaseAssert(respBegId != TS_ERROR);
    TSReleaseAssert(respEndId != TS_ERROR);
#endif
    // Set an initial value for our statistic.
    TSStatIntSet(reqBegId, 0);
    TSStatIntSet(reqEndId, 0);
    TSStatIntSet(respBegId, 0);
    TSStatIntSet(respEndId, 0);

  } else {
    TSDebug(PLUGIN_NAME, " plugin is disabled.");
  }
}
