#!/usr/bin/env bash

## Update
git pull

## Build
cmake --build cmake-build-debug

## Stop services
sudo systemctl stop nvr-stream@*

## Copy binary
sudo cp ./cmake-build-debug/nvr_stream /usr/local/bin/

echo "Done!"