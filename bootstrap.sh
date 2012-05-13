#!/bin/bash

autoreconf -vfi

if type -p colorgcc > /dev/null ; then
   export CC=colorgcc
fi

if test "x$NOCONFIGURE" = "x"; then
    CFLAGS="-g -O0" ./configure --enable-maintainer-mode --disable-processing "$@"
    make clean
fi
