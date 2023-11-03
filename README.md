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

## Test

```shell
./dist/never_cli rtsp://admin:VipVipq34@wildwood.copcart.com:554/Streaming/Channels/101/ http://admin:VipVipq34@192.168.1.162/ISAPI/Streaming/Channels/101/picture ./ camera1 15
```
