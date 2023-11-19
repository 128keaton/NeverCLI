#!/usr/bin/env bash

# Check for sudo
if [ $EUID != 0 ]; then
    sudo "$0" "$@"
    exit $?
fi


## Install
cmake --install cmake-build-debug/

## Permit nvr_stream to access network stuff
setcap cap_net_admin+ep  /usr/local/bin/nvr_stream