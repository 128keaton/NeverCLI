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

## Usage

The binary expects to be passed the path to a JSON file containing the following:

```json
{
	"streamURL": "rtsp://camera/Streaming/Channels/101/",
	"snapshotURL": "http://camera/ISAPI/Streaming/Channels/101/picture",
	"outputPath": "./",
	"splitEvery": 30
}
```
(replacing the appropriate details)

### systemd

An example systemd file is included in the root of the repository (`camera-1.service`).

This assumes you have a directory at root called `nvr`, this also assumes you have copied your compiled binary into `/usr/local/bin`
