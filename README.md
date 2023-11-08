# Never CLI

## Dependencies

### Debian
Requires `libavcodec-dev libavutil-dev libavformat-dev libcurl4-openssl-dev libavdevice-dev libavfilter-dev libspdlog-dev gstreamer-1.0` on Debian

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

### systemd

An example systemd file is included in the `examples/` of the repository (`camera-example-record.service`).

This assumes you have a directory at root called `nvr`, this also assumes you have copied your compiled binary into `/usr/local/bin`
