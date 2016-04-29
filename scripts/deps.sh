#!/bin/sh

set -x
set -e

PREFIX="${HOME}/local"
export PKG_CONFIG_PATH="${PKG_CONFIG_PATH}:${PREFIX}/lib/pkgconfig"

if test -d "${PREFIX}"
then
    echo "Using cached dependencies"
    exit
fi

# install protobuf
(
    cd /tmp

    if test -d protobuf-2.5.0
    then
        cd protobuf-2.5.0
        make install
    else
        wget https://github.com/google/protobuf/releases/download/v2.5.0/protobuf-2.5.0.tar.bz2
        tar -xvf protobuf-2.5.0.tar.bz2
        cd protobuf-2.5.0
        autoreconf -fi
        ./configure --prefix="${PREFIX}"
        make
        make install
    fi
)

# install protobuf-c
(
    cd /tmp

    if test -d protobuf-c-1.0.2
    then
        cd protobuf-c-1.0.2
        make install
    else
        wget https://github.com/protobuf-c/protobuf-c/releases/download/v1.0.2/protobuf-c-1.0.2.tar.gz
        tar -xvf protobuf-c-1.0.2.tar.gz
        cd protobuf-c-1.0.2
        autoreconf -fi
        ./configure --prefix="${PREFIX}"
        make
        make install
    fi
)

# install cmocka
(
    cd /tmp

    if test -d cmocka-1.0.1
    then
        cd cmocka-1.0.1/build
        make install
    else
        wget https://cmocka.org/files/1.0/cmocka-1.0.1.tar.xz
        tar -xvf cmocka-1.0.1.tar.xz
        cd cmocka-1.0.1
        mkdir build
        cd build
        # Fix building without RPATH
        cmake -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_PREFIX:PATH="${PREFIX}" ..
        make
        make install
    fi
)

# install libsodium-1.0.8
(
    cd /tmp

    if test -d libsodium-1.0.8
    then
        cd libsodium-1.0.8
        make install
    else
        wget https://github.com/jedisct1/libsodium/releases/download/1.0.8/libsodium-1.0.8.tar.gz
        tar -xvf libsodium-1.0.8.tar.gz
        cd libsodium-1.0.8
        autoreconf -fi
        ./configure --prefix="${PREFIX}"
        make
        make install
    fi
)
