#!/bin/bash

set -e

source ./platforms/config.sh

echo "Building libraries..."
echo "  LIBFRAMEUTIL_SHA: ${LIBFRAMEUTIL_SHA}"
echo ""

rm -rf external
mkdir external
cd external

#
# copy libframeutil
#

curl -sL https://github.com/ppuc/libframeutil/archive/${LIBFRAMEUTIL_SHA}.tar.gz -o libframeutil-${LIBFRAMEUTIL_SHA}.tar.gz
tar xzf libframeutil-${LIBFRAMEUTIL_SHA}.tar.gz
mv libframeutil-${LIBFRAMEUTIL_SHA} libframeutil
cd libframeutil
mkdir -p ../../third-party/include
cp include/* ../../third-party/include
cd ..
