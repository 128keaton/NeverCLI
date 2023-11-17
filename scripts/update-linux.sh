#!/usr/bin/env bash

## Update
git pull

## Build
cmake --build cmake-build-debug

## Stop services
systemctl --user stop nvr-stream@*
systemctl --user stop nvr-record@*

## Install
/bin/bash ./scripts/super-tasks.sh

## Restart Janus
systemctl --user restart janus

echo "Done!"