#!/bin/sh

VERSION=`head -1 ../VERSION`
DATE=`date +%Y-%m-%d`

echo Making WAkannel-${VERSION}-${DATE}-Solaris7-sparc-local.tar.Z
cd ..
CFLAGS="-Wall -O2 -Xlinker -rpath -Xlinker /usr/local/lib" ./configure --prefix=./solaris/kannel --with-malloc=native
make 
make install
cd solaris
sed "s/VERSION_NUM/${VERSION}/" < prototype.tmpl > prototype
pkgmk -r `pwd` -d .
tar cvf WAkannel-${VERSION}-${DATE}-Solaris7-sparc-local.tar WAkannel
compress WAkannel-${VERSION}-${DATE}-Solaris7-sparc-local.tar
