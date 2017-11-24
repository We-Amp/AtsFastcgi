#ifndef ATS_MOD_FCGI_H
#define ATS_MOD_FCGI_H

#define PLUGIN_NAME "ats_mod_fcgi"
#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"

#define ATS_MODULE_FCGI_NAME "ats_mod_fcgi"
#define ATS_MOD_FCGI_VERSION "ats_mod_fcgi"
#define ATS_MOD_LOG_FILENAME "atsModFCGI.log"

#include "ats_fcgi_config.h"
#include <atscppapi/GlobalPlugin.h>
#include "fcgi_server.h"

using namespace atscppapi;

class FCGIServer;

namespace fcgiGlobal
{
extern GlobalPlugin *plugin;
extern fcgiPluginData *plugin_data;
extern FCGIServer *fcgi_server;
}

/*
 * # of idle seconds allowed to pass while connected to a FastCGI before
 * aborting
 */
#define ATS_FCGI_DEFAULT_IDLE_TIMEOUT 30

#endif /* ATS_MOD_FCGI_H */
