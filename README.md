# AtsFastcgi

Apache Traffic Server

Traffic Server is a high-performance building block for cloud services.
It's more than just a caching proxy server; it also has support for
plugins to build large scale web applications.

1. DIRECTORY STRUCTURE

  AtsFastcgi/ ............... top src dir
  |-- opt/ts-7.1/ ................ ATS Server Binaries
  |-- plugins/ ............... FastCGI Plugins Development Directory
      |-- demoFastCGI/          Initial Demo Version
  	    |-- bin/
		|-- php-fastcgi ---- php-fastCGI server protocol App Script	
      |-- fastCGI.so ........ fastCGI Plugins Shared object file 
  |-- trafficserver-7.1.1 ............ Actual Traffic server source code loaded from git 7.1.1 branch
  |-- doc/ ...............   fastCGI development work documentation 
  |-- README ............. intro, links, build info
  |-- LICENSE ............ full license text
  `-- NOTICE ............. copyright notices

2. REQUIREMENTS

  This section outlines build requirements for different OS
  distributions. This may be out of date compared to the on-line
  requirements at

  <https://cwiki.apache.org/confluence/display/TS/Building>.

  As of ATS v7.0.0 and later, gcc 4.8.1 or later is required, since we now use
  and require the C++11 standard.

  1. download libfcgi-dev package :i.e libfcgi-dev_2.4.0-8.4+b1_arm64.deb from https://packages.debian.org/sid/arm64/libfcgi-dev/download     
 
  2. download php7.0-cli php7.0-fpm with :  sudo apt-get install php7.0-cli php7.0-fpm




Plugin Shared Object generation and Installation:
	1.Plz from point to /AtsFastcgi/plugins/demoFastCGI/ directory from terminal
	2. generate .so object from below command:
	    /AtsFastcgi/opt/ts-7.1/tsxs -o fastCGI.so -c fastCGI.c -I../trafficserver-7.1.1/lib/ -I./include/ -L../opt/ts-7.1/lib/ ../trafficserver-7.1.1/lib/ts/ink_defs.cc fcgi_header.c

	3.Now Install the fastCGI.so file to /AtsFastcgi/opt/libexec directory... this is the place from where ATS will pick up the plugins . Use below command
	/AtsFastcgi/opt/ts-7.1/tsxs -o fastCGI.so -i
	


Start the PHP fastCGI with below command:
	Note: ensure u have php-cgi installed
 
        sudo /AtsFastcgi/plugins/demoFastCGI/bin/php-fast-cgi start    //this will start the server on port 6000


Now include this plugin to the ATS within plugin.config

and Start the ATS server from : sudo /AtsFastcgi/opt/ts-7.1/bin/trafficserver start
	 


   




