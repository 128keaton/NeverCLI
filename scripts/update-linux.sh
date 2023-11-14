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

## Restart Janus
systemctl --user restart janus

echo "Done!"