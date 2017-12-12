#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ats_fcgi_config.h"
#include <ts/ts.h>

static char DEFAULT_HOSTNAME[]     = "localhost";
static char DEFAULT_SERVER_IP[]    = "127.0.0.1";
static char DEFAULT_SERVER_PORT[]  = "60000";
static char DEFAULT_INCLUDE_FILE[] = "fastcgi.config";

/**
 * Helper function for the config parser
 *  copied from plugins/conf_remap/conf_remap.cc
 *
 * @param const char *str config data type
 *
 * @return TSRecordDataType config data type
 */
inline TSRecordDataType
str_to_datatype(const char *str)
{
  TSRecordDataType type = TS_RECORDDATATYPE_NULL;

  if (!str || !*str) {
    return TS_RECORDDATATYPE_NULL;
  }

  if (!strcmp(str, "INT")) {
    type = TS_RECORDDATATYPE_INT;
  } else if (!strcmp(str, "STRING")) {
    type = TS_RECORDDATATYPE_STRING;
  }

  return type;
}

/**
 * Find ats_mod_fcgi transaction config option
 *  modified from InkAPI.cc:TSHttpTxnConfigFind
 *
 * @param const char       *name  name of config options
 * @param int              length length of *name
 * @param fcgiConfigKey      *conf  config key
 * @param TSRecordDataType *type  config option data type
 *
 * @return fcgiTxnState hashEntry added, should be ignore and pass, or fail
 */
static TSReturnCode
fcgiHttpTxnConfigFind(const char *name, int length, fcgiConfigKey *conf, TSRecordDataType *type)
{
  *type = TS_RECORDDATATYPE_NULL;
  if (length == -1) {
    length = strlen(name);
  }

  if (!strncmp(name, "proxy.config.http.fcgi.enabled", length)) {
    *conf = fcgiEnabled;
    *type = TS_RECORDDATATYPE_INT;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.hostname", length)) {
    *conf = fcgiHostname;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.server_ip", length)) {
    *conf = fcgiServerIp;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.server_port", length)) {
    *conf = fcgiServerPort;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.include", length)) {
    *conf = fcgiInclude;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}
/**
 * Initial ats_mod_fcgi plugin with config file
 *  modified from plugins/conf_remap/conf_remap.cc, RemapConfigs::parse_file
 *
 * @param const char *fn filename of config file
 *
 * @return FCGIPluginConfig *config object
 */
FCGIPluginConfig *
initConfig(const char *fn)
{
  InterceptPluginData *plugin_data = getFCGIPlugin();
  FCGIPluginConfig *config         = static_cast<FCGIPluginConfig *>(TSmalloc(sizeof(FCGIPluginConfig)));

  // Default config
  if (nullptr == plugin_data || nullptr == plugin_data->global_config) {
    config->enabled     = true;
    config->hostname    = DEFAULT_HOSTNAME;
    config->server_ip   = DEFAULT_SERVER_IP;
    config->server_port = DEFAULT_SERVER_PORT;
    config->include     = DEFAULT_INCLUDE_FILE;
  } else {
    // Inherit from global config
    FCGIPluginConfig *global_config = plugin_data->global_config;
    config->enabled                 = global_config->enabled;
    config->hostname                = TSstrdup(global_config->hostname);
    config->server_ip               = TSstrdup(global_config->server_ip);
    config->server_port             = TSstrdup(global_config->server_port);
    config->include                 = TSstrdup(global_config->include);
  }

  if (fn) {
    if (1 == strlen(fn)) {
      if (0 == strcmp("0", fn)) {
        config->enabled = false;
      } else if (0 == strcmp("1", fn)) {
        config->enabled = true;
      } else {
        TSError("[ats_mod_fcgi] Parameter '%s' ignored", fn);
      }
    } else {
      int line_num = 0;
      TSFile file;
      char buf[8192];
      fcgiConfigKey name;
      TSRecordDataType type, expected_type;

      if (nullptr == (file = TSfopen(fn, "r"))) {
        TSError("[ats_mod_fcgi] Could not open config file %s", fn);
      } else {
        while (nullptr != TSfgets(file, buf, sizeof(buf))) {
          char *ln, *tok;
          char *s = buf;

          ++line_num; // First line is #1 ...
          while (isspace(*s)) {
            ++s;
          }
          tok = strtok_r(s, " \t", &ln);

          // check for blank lines and comments
          if ((!tok) || (tok && ('#' == *tok))) {
            continue;
          }

          if (strncmp(tok, "CONFIG", 6)) {
            TSError("[ats_mod_fcgi] File %s, line %d: non-CONFIG line encountered", fn, line_num);
            continue;
          }

          // Find the configuration name
          tok = strtok_r(nullptr, " \t", &ln);
          if (fcgiHttpTxnConfigFind(tok, -1, &name, &expected_type) != TS_SUCCESS) {
            TSError("[ats_mod_fcgi] File %s, line %d: no records.config name given", fn, line_num);
            continue;
          }

          // Find the type (INT or STRING only)
          tok = strtok_r(nullptr, " \t", &ln);
          if (TS_RECORDDATATYPE_NULL == (type = str_to_datatype(tok))) {
            TSError("[ats_mod_fcgi] File %s, line %d: only INT and STRING "
                    "types supported",
                    fn, line_num);
            continue;
          }

          if (type != expected_type) {
            TSError("[ats_mod_fcgi] File %s, line %d: mismatch between provide "
                    "data type, and expected type",
                    fn, line_num);
            continue;
          }

          // Find the value (which depends on the type above)
          if (ln) {
            while (isspace(*ln)) {
              ++ln;
            }

            if ('\0' == *ln) {
              tok = nullptr;
            } else {
              tok = ln;

              while (*ln != '\0') {
                ++ln;
              }

              --ln;

              while (isspace(*ln) && (ln > tok)) {
                --ln;
              }

              ++ln;
              *ln = '\0';
            }
          } else {
            tok = nullptr;
          }

          if (!tok) {
            TSError("[ats_mod_fcgi] File %s, line %d: the configuration must "
                    "provide a value",
                    fn, line_num);
            continue;
          }

          // Now store the new config
          switch (name) {
          case fcgiEnabled:
            config->enabled = tok;
            break;

          case fcgiHostname:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->hostname = nullptr;
            } else {
              config->hostname = TSstrdup(tok);
            }
            break;

          case fcgiServerIp:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->server_ip = nullptr;
            } else {
              config->server_ip = TSstrdup(tok);
            }
            break;

          case fcgiServerPort:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->server_port = nullptr;
            } else {
              config->server_port = TSstrdup(tok);
            }
            break;

          case fcgiInclude:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->include = nullptr;
            } else {
              config->include = TSstrdup(tok);
            }
            break;

          default:
            break;
          }
        }

        TSfclose(file);
      }
    }
  }

  TSDebug(PLUGIN_NAME, "enabled = %d", static_cast<int>(config->enabled));
  TSDebug(PLUGIN_NAME, "hostname = %s", config->hostname);
  TSDebug(PLUGIN_NAME, "server_ip = %s", config->server_ip);
  TSDebug(PLUGIN_NAME, "server_port = %s", config->server_port);
  return config;
}

InterceptPluginData *
getFCGIPlugin()
{
  static InterceptPluginData *data = nullptr;

  // TODO Check where this data is released/freed

  if (!data) {
    data                  = static_cast<InterceptPluginData *>(TSmalloc(sizeof(InterceptPluginData)));
    data->mutex           = TSMutexCreate();
    data->active_hash_map = new UintMap();
    data->keep_pass_list  = new UsecList();
    data->seq_id          = 0;
    data->global_config   = nullptr;
    TSHttpArgIndexReserve(PLUGIN_NAME, "reserve txn_data slot", &(data->txn_slot));
  }

  return data;
}
