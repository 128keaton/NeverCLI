#!/usr/bin/env bash

## Update
git pull

## Build
cmake --build cmake-build-debug

## Stop services
systemctl --user stop nvr-stream@*
systemctl --user stop nvr-record@*

## Copy binaries
sudo cp ./cmake-build-debug/nvr_stream /usr/local/bin/
sudo cp ./cmake-build-debug/nvr_record /usr/local/bin/

## Permit nvr_stream to access network stuff
sudo setcap cap_net_admin+ep  /usr/local/bin/nvr_stream

## Restart Janus
systemctl --user restart janus

echo "Done!"