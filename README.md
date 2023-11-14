# Never CLI

## Dependencies

### Debian

Requires a new-ish version of [cmake](https://apt.kitware.com/)

Requires `pkg-config cmake build-essential libavcodec-dev libavutil-dev libavformat-dev libcurl4-openssl-dev libavdevice-dev libavfilter-dev libspdlog-dev gstreamer-1.0 libgstreamer1.0-dev` on Debian

### macOS
Requires `pkg-config`, `ffmpeg`, `gstreamer`, and `spdlog` installable through Homebrew

```shell
brew install pkg-config ffmpeg spdlog gstreamer
```
## Building

```shell
cmake -S . -B cmake-build-debug   
```
then 

```shell
cmake --build cmake-build-debug   
```


## Usage

The binary expects to be passed the path to a JSON file similar to the one under `examples/`
(replacing the appropriate details)

Also please use `setcap` to allow `nvr_stream` access to the socket:

```shell
sudo setcap cap_net_admin+ep  /usr/local/bin/nvr_stream
```

### systemd

There are two systemd unit templates included, one for streaming and one for recording. 

To start recording:
```shell
systemd enable --now nvr-record@camera-1
```
This assumes you have a directory at root called `nvr`, 
this also assumes you have copied your compiled binary into `/usr/local/bin`, 
and this assumes `/nvr/cameras/` has a file named `camera-1.json`

