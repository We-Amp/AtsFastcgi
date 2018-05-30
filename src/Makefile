#Please update below required files/directories inorder to work this make file
# 1. ATS_SRC		--either set or export env variable path to traffic server src directory
# 2. ATS_EXEC       --either set or export env variable path to traffic server exec directory


ifndef ATS_SRC
$(error ATS_SRC is not set)
endif

ifndef ATS_EXEC
$(error ATS_EXEC is not set)
endif

MOD_FCGI_DIR=$(CURDIR)
TRAFFIC_SERVER_SRC_DIR=$(ATS_SRC)
TRAFFIC_SERVER_EXEC_DIR=$(ATS_EXEC)

TSXS?=$(TRAFFIC_SERVER_EXEC_DIR)/bin/tsxs
OBJ_FILES=server_intercept.cc fcgi_config.cc ats_fcgi_client.cc server.cc connection_pool.cc request_queue.cc server_connection.cc

TRAFFIC_SERVER_EXEC_CONF_DIR=$(TRAFFIC_SERVER_EXEC_DIR)/etc/trafficserver/
PLUGIN_CONF_FILES=$(MOD_FCGI_DIR)/config/ats_fastcgi.config

LDLIB_DIR=-L $(TRAFFIC_SERVER_EXEC_DIR)/lib/
LDLIB=-latscppapi

INC=-I $(MOD_FCGI_DIR)\
	-I $(TRAFFIC_SERVER_SRC_DIR)/lib\
	-I $(TRAFFIC_SERVER_SRC_DIR)/lib/cppapi/include/
COPY_CONFIG = $(shell sudo cp -R '$(PLUGIN_CONF_FILES)' '$(TRAFFIC_SERVER_EXEC_CONF_DIR)')

all:
	$(TSXS) -o ats_fastcgi.so -c ats_fastcgi.cc $(LDLIB_DIR) $(LDLIB) $(INC) $(OBJ_FILES)
install:
	$(TSXS) -v -i -o ats_fastcgi.so
	echo copying config files to ats_fascgi config dir..... $(COPY_CONFIG)
clean:
	rm -f *.lo *.so
