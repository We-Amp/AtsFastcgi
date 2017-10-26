# AtsFastcgi

1. REQUIREMENTS

  This section outlines build requirements for different OS
  distributions. This may be out of date compared to the on-line
  requirements at

  <https://cwiki.apache.org/confluence/display/TS/Building>.

  As of ATS v7.0.0 and later, gcc 4.8.1 or later is required, since we now use
  and require the C++11 standard.

  a. download libfcgi-dev package :i.e libfcgi-dev_2.4.0-8.4+b1_arm64.deb from https://packages.debian.org/sid/arm64/libfcgi-dev/download
 
  b. download php7.0-cli php7.0-fpm and php-cgi with :  sudo apt-get install php7.0-cli php7.0-fpm php-cgi




2. Plugin Shared Object generation and Installation:

	a. generate and install .so object from below command:
 /path/to/ts-7.1/bin/tsxs -o demoIntercept.so -c demoIntercept.cc -L /path/to/ts-7.1/lib/ fcgi-client/fcgi_header.c && sudo /path/to/ts-7.1/bin/tsxs -o demoIntercept.so -i


3. Start the PHP fastCGI with below command:
	Note: ensure u have php-cgi installed
 
        sudo workingPOC/fcgi-client/bin/php-fast-cgi start    //this will start the server on port 60000


4. Keep some php script file named abc.php at /var/www/html/abc.php location.


Now specify this plugin to the ATS within plugin.config

and Start the ATS server from : sudo /path/to/ts-7.1/bin/traffic_server -T"demoIntercept"
	 


   




