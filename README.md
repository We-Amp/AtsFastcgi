# AtsFastcgi

1. REQUIREMENTS

  This section outlines build requirements for different OS
  distributions. This may be out of date compared to the on-line
  requirements at

  <https://cwiki.apache.org/confluence/display/TS/Building>.

  As of ATS v7.0.0 and later, gcc 4.8.1 or later is required, since we now use
  and require the C++11 standard.

  a. download libfcgi-dev package :i.e libfcgi-dev_2.4.0-8.4+b1_arm64.deb from https://packages.debian.org/sid/arm64/libfcgi-dev/download
 
  b. download php7.0-cli php7.0-fpm with :  sudo apt-get install php7.0-cli php7.0-fpm




2. Plugin Shared Object generation and Installation:

	a. generate .so object from below command:
	    /path/to/ts-7.1/bin/tsxs -o fastCGI.so -c fastCGI.c -I/path/to/trafficServer7.1.1/lib/ -I./include/ -L../opt/ts-7.1/lib/ /path/to/trafficServer-7.1.10/lib/ts/ink_defs.cc fcgi_header.c

	b.Now Install the fastCGI.so file to /path/to/ts-7.1/libexec directory... this is the place from where ATS will pick up the plugins . Use below command
	/path/to/ts-7.1/bin/tsxs -o fastCGI.so -i
	


3. Start the PHP fastCGI with below command:
	Note: ensure u have php-cgi installed
 
        sudo /AtsFastcgi/plugins/demoFastCGI/bin/php-fast-cgi start    //this will start the server on port 6000


Now specify this plugin to the ATS within plugin.config

and Start the ATS server from : sudo /path/to/ts-7.1/bin/trafficserver start
	 


   




