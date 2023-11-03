# Never CLI

## Dependencies

Requires `libavcodec-dev libavutil-dev libavformat-dev libcurl4-openssl-dev libavdevice-dev libavfilter-dev` on Debian


## Building

```shell
cmake -S . -B cmake-build-debug   
```
then 

```shell
cmake --build cmake-build-debug   
```

Binary is available at `./cmake-build-debug/never_cli`

