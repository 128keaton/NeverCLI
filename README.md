# Never CLI

## Dependencies

### Debian
Requires `libavcodec-dev libavutil-dev libavformat-dev libcurl4-openssl-dev libavdevice-dev libavfilter-dev libspdlog-dev` on Debian

### macOS
Requires `ffmpeg` and `spdlog` installable through Homebrew

## Building

```shell
cmake -S . -B cmake-build-debug   
```
then 

```shell
cmake --build cmake-build-debug   
```

Binary is available at `./cmake-build-debug/never_cli`

