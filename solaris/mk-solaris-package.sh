#!/bin/sh

cd ..
CFLAGS="-Wall -O3" ./configure --prefix=./solaris/kannel --with-malloc=native
make 
make install
cd solaris
pkgmk -r `pwd` -d .
tar cvf WAkannel-cvs-Solaris7-sparc-local.tar WAkannel
compress WAkannel-cvs-Solaris7-sparc-local.tar
