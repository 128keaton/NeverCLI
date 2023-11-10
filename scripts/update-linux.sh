#!/usr/bin/env bash

## Update
git pull

## Build
cmake --build cmake-build-debug

## Stop services
systemctl --user stop nvr-stream@*

## Copy binary
sudo cp ./cmake-build-debug/nvr_stream /usr/local/bin/

systemctl --user daemon-reload

cp ./systemd/*.service /etc/systemd/user/

echo "Done!"