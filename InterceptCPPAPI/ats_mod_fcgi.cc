#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/PluginInit.h>
#include <iostream>
#include <string.h>
#include <netinet/in.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"
#include "utils_internal.h"
#include "fcgi_intercept.h"

using namespace atscppapi;

using std::cout;
using std::endl;
using std::string;
namespace
{
GlobalPlugin *plugin;
}

class FastCGIGlobalPlugin : public GlobalPlugin
{
public:
  FastCGIGlobalPlugin() : GlobalPlugin(true)
  {
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS);
  }
  void
  handleReadRequestHeaders(Transaction &transaction) override
  {
    if (transaction.getClientRequest().getUrl().getPath().find(".php") != string::npos)
    {
      transaction.addPlugin(new FastCGIIntercept(transaction));
    }
    transaction.resume();
  }
  
};



void TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("ats_mod_fcgi", "apache", "dev@trafficserver.apache.org");
  cout << "Registered and invoked the ats_mod_fcgi Plugin into ATS." << endl;
  plugin = new FastCGIGlobalPlugin();
 
}
