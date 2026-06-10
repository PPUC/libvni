#!/bin/bash

set -e

LIBFRAMEUTIL_SHA=28f2bae0dabcbd5c599e6f62211f009e078c1f96

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
