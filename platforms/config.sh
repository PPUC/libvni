#!/bin/bash

set -e

LIBFRAMEUTIL_SHA=03d2483d5cded0bdef84bec24c9ddfdede324b5c

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""
