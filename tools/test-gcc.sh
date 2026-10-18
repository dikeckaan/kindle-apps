#!/bin/sh
# Runs the host unit tests with gcc -Werror in the toolchain image, like CI does (clang on macOS misses some warnings).
set -eu
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/src:ro kindle-apps-tc sh -c 'mkdir /tmp/w && cd /src &&
  tar cf - --exclude=./build --exclude=./third_party --exclude=./.claude --exclude=./dist . | (cd /tmp/w && tar xf -) &&
  cd /tmp/w && make -s test HOST_CC=gcc HOST_CFLAGS="-std=c11 -Wall -Wextra -Werror -g -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Icore/kui -Iapps -Icore"'
