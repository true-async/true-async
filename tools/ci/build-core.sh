#!/bin/bash
# Build ior and the core for one tree and install the core into ~/ta-prefix/pocs-<tree>, as
# dev/WORKFLOW.md ("Building the core") does locally. CI caches the prefix by this file's hash, so
# a change here rebuilds the core.
#
#     build-core.sh <dbg|asan> <core sha> <ior sha>
set -euo pipefail

tree=$1
core_ref=$2
ior_ref=$3

ior_prefix=$HOME/ior-$tree
core_prefix=$HOME/ta-prefix/pocs-$tree
work=${RUNNER_TEMP:-/tmp}/core-$tree

ior_flags=()
core_flags=()

if [ "$tree" = asan ]; then
    ior_flags=(-DIOR_ENABLE_ASAN=ON)
    core_flags=(--enable-address-sanitizer --enable-undefined-sanitizer)
fi

fetch() {
    git init -q "$2"
    git -C "$2" fetch -q --depth 1 "$1" "$3"
    git -C "$2" checkout -q FETCH_HEAD
}

rm -rf "$work"
fetch https://github.com/libior/ior.git "$work/ior" "$ior_ref"
cmake -S "$work/ior" -B "$work/ior/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ior_prefix" \
    -DIOR_WITH_THREADS=ON -DIOR_BUILD_TESTS=OFF -DIOR_BUILD_BENCH=OFF "${ior_flags[@]}"
cmake --build "$work/ior/build" --parallel
cmake --install "$work/ior/build"

fetch https://github.com/true-async/php-src.git "$work/php-src" "$core_ref"
cd "$work/php-src"
./buildconf --force
./configure --enable-zts --enable-debug --with-ior="$ior_prefix" --enable-test-scheduler \
    --with-curl --with-openssl --enable-sockets --enable-pcntl --with-mysqli --with-pdo-mysql --with-zlib \
    --prefix="$core_prefix" "${core_flags[@]}"
make -j"$(nproc)" > /dev/null
make install
