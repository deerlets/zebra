#!/bin/bash

# usage
usage()
{
    echo -e "USAGE: $0 [TOOLCHAIN]"
    echo -e "  TOOLCHAIN    arm-linux-gnueabi, [default: ]"
    echo -e ".e.g: $0"
    echo -e ".e.g: $0 arm-linux-gnueabi"
}
[[ "$*" =~ "help" ]] || [[ "$*" =~ "-h" ]] && usage && exit 2

# change directory to the location of this script
ORIGIN_DIR=$(pwd)
SCRIPT_DIR=$(cd `dirname $0`; pwd)
PROJECT_DIR=$SCRIPT_DIR/../

# parse opts & envs
[ -n "$1" ] && TOOLCHAIN=${1}-
[ -z "$JOBS" ] &&  JOBS=1
[[ "$OSTYPE" == "linux-gnu" ]] && ((JOBS=$(grep -c ^processor /proc/cpuinfo)-1))

# setup cross toolchain
export PKG_CONFIG_PATH=$PROJECT_DIR/lib/pkgconfig
export CC=${TOOLCHAIN}gcc
export CXX=${TOOLCHAIN}g++
export AR=${TOOLCHAIN}ar
export LD=${TOOLCHAIN}ld
export STRIP=${TOOLCHAIN}strip

# logging aspect
do_build()
{
    echo -e "\033[32m($(date '+%Y-%m-%d %H:%M:%S')): Building $1\033[0m"
    $*
    echo -e "\033[32m($(date '+%Y-%m-%d %H:%M:%S')): Finished $1\033[0m"
}

# initialization
do_init()
{
    git submodule init
    git submodule update

    [ ! -e $PROJECT_DIR/lib ] && mkdir -p $PROJECT_DIR/lib
}

libgtest()
{
    googletest_path=$PROJECT_DIR/deps/googletest

    if [ ! "$(find $PROJECT_DIR/lib* -maxdepth 1 -name *${FUNCNAME[0]}*)" ]; then
        mkdir -p $googletest_path/build && cd $googletest_path/build
        cmake .. -DCMAKE_INSTALL_PREFIX:PATH=$PROJECT_DIR
        make -j$JOBS && make install
        [ ! $? -eq 0 ] && exit 1
    fi
}

libzmq()
{
    libzmq_path=$PROJECT_DIR/deps/libzmq

    if [ ! "$(find $PROJECT_DIR/lib* -maxdepth 1 -name *${FUNCNAME[0]}*)" ]; then
        mkdir -p $libzmq_path/build && cd $libzmq_path/build
        cmake .. -DCMAKE_INSTALL_PREFIX:PATH=$PROJECT_DIR -DBUILD_TESTS=off
        make -j$JOBS && make install
        [ ! $? -eq 0 ] && exit 1
    fi
}

libzero()
{
    mkdir -p $PROJECT_DIR/build && cd $PROJECT_DIR/build
    cmake .. -DWITH_TESTS=on && make -j$JOBS && make test
    [ ! $? -eq 0 ] && exit 1
}

do_init
do_build libgtest
do_build libzmq
do_build libzero