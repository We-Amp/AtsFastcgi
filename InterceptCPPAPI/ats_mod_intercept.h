#ifndef ATS_MOD_INTERCEPT_H
#define ATS_MOD_INTERCEPT_H

#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"

#define ATS_MODULE_FCGI_NAME "ats_mod_fcgi"
#define ATS_MOD_FCGI_VERSION "ats_mod_fcgi"
#define ATS_FCGI_PROFILER true

#include "fcgi_config.h"
#include <atscppapi/GlobalPlugin.h>
#include "server.h"
#include "Profiler.h"

using namespace atscppapi;

class FCGIServer;

namespace InterceptGlobal
{
extern GlobalPlugin *plugin;
extern ats_plugin::InterceptPluginData *plugin_data;
// TODO (Rakesh): Move to FCGI connect file
extern ats_plugin::Server *gServer;
extern int reqId, respId;
#ifdef ATS_FCGI_PROFILER
extern ats_plugin::Profiler profiler;
#endif
}

#endif /* ATS_MOD_INTERCEPT_H */
