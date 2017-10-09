#!/usr/bin/env python

import os
import sys
import subprocess


def main():
    if len(sys.argv) != 2:
        print 'usage: ./build_and_start path_to_ats_root'
        sys.exit(1)

    ATSPATH = os.path.abspath(sys.argv[1])
    if not os.path.exists(ATSPATH):
        print "%s path not found." % ATSPATH
        sys.exit(1)

    TSXSPATH = os.path.join(ATSPATH, "bin/tsxs")
    LIBPATH = os.path.join(ATSPATH, "lib")

    print TSXSPATH
    print LIBPATH

    # Build the so
    output = subprocess.check_output([TSXSPATH, "-o", "demoIntercept.so",
                                      "-c", "demoIntercept.cc", "-L", LIBPATH, "fcgi-client/fcgi_header.c"])

    if "error" in output:
        print "Error building .so"
        sys.exit(1)

    # Copy the so to relevant directory
    output = subprocess.check_output(
        ["sudo", TSXSPATH, "-o", "demoIntercept.so", "-i"])
    if "installing" not in output:
        print "Error linking .so", output
        sys.exit(1)

    # Start the php-fhm
    subprocess.call(["chmod", "+x", "fcgi-client/bin/php-fastcgi"])
    output = subprocess.check_output(["fcgi-client/bin/php-fastcgi", "start"])
    if "error" in output:
      print "Unable to start php server ", output

    #start Apache traffic server
    subprocess.call(["clear"])
    subprocess.call([os.path.join(ATSPATH,"bin/traffic_server"), '-T"demoIntercept"'])


if __name__ == '__main__':
    main()
