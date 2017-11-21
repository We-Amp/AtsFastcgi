#Please update below required files/directories inorder to work this make file
# 1. TSXS  						--path to tsxs script file present under path/to/ts/bin/tsxs
# 2. MOD_FCGI_DIR   			--specify the directory where ats_mod_fcgi plugin src is present.
# 3. TRAFFIC_SERVER_SRC_DIR		--path to traffic server src directory 
# 4. TRAFFIC_SERVER_EXEC_DIR    --path to traffic server exec directory

MOD_FCGI_DIR=/home/rahul/GDBTech/newClone/AtsFastcgi/InterceptCPPAPI/
TRAFFIC_SERVER_SRC_DIR=/home/rahul/GDBTech/ATS/trafficserver-7.1.1/
TRAFFIC_SERVER_EXEC_DIR=/home/rahul/GDBTech/ATS/opt/ts-7.1/

TSXS?=$(TRAFFIC_SERVER_EXEC_DIR)bin/tsxs
OBJ_FILES=fcgi_intercept.cc ats_fcgi_config.cc ats_fcgi_client.cc

TRAFFIC_SERVER_EXEC_CONF_DIR=$(TRAFFIC_SERVER_EXEC_DIR)etc/trafficserver/
PLUGIN_CONF_FILES=$(MOD_FCGI_DIR)config/ats_mod_fcgi.config

LDLIB_DIR=-L $(TRAFFIC_SERVER_EXEC_DIR)lib/
LDLIB=-latscppapi

INC=-I $(MOD_FCGI_DIR)\
	-I $(TRAFFIC_SERVER_SRC_DIR)lib\
	-I $(TRAFFIC_SERVER_SRC_DIR)lib/cppapi/include/
COPY_CONFIG = $(shell sudo cp -R '$(PLUGIN_CONF_FILES)' '$(TRAFFIC_SERVER_EXEC_CONF_DIR)')

all:
	$(TSXS) -o ats_mod_fcgi.so -c ats_mod_fcgi.cc $(LDLIB_DIR) $(LDLIB) $(INC) $(OBJ_FILES)
install: 
	$(TSXS) -v -i -o ats_mod_fcgi.so
	echo copying config files to ats_mod_fcgi config dir..... $(COPY_CONFIG)
clean:
	rm -f *.lo *.so