#!/usr/bin/env bash

if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi

## Permit nvr_stream to access network stuff
setcap cap_net_admin+ep  /usr/local/bin/nvr_stream

## Install
cmake --install cmake-build-debug/