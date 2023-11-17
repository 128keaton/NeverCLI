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

## Prompt user to restart
read -rp "Do you want to (re)start stream/recording tasks? (y/n) " yn

case $yn in
	[yY] ) systemctl --user start --all nvr-stream@*;
         systemctl --user start --all nvr-record@*;;
	* ) exit;;
esac
