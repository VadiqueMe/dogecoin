#!/bin/sh

SOURCE_DIR="dogecoin-git"
BUILD_DIR="boo-dogecoin"

rm -f -r ${BUILD_DIR}
cp -r dogecoin-git ${BUILD_DIR}
cd ${BUILD_DIR}

autoreconf -f -i

./configure CFLAGS='-O3 -Wno-implicit-fallthrough' CXXFLAGS='-O3 -Wno-implicit-fallthrough -Wno-stringop-overflow' --prefix=/opt/dogecoin --enable-sse2 --with-bignum=gmp | tee _configure_log

## --disable-tests --disable-bench

make -j2 2>&1 | tee _make_log
##make V=1 2>&1 | tee _make_log

cd src && make translate && cd ..
cp src/qt/translationstrings.cpp ../${SOURCE_DIR}/src/qt/
cp src/qt/locale/dogecoin_en.ts ../${SOURCE_DIR}/src/qt/locale/

./contrib/devtools/gen-manpages.sh
cp doc/man/*.1 ../${SOURCE_DIR}/doc/man/
