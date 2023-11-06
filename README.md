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

An example systemd file is shown below:
```systemd
[Unit]
Description=Never Camera 1
After=syslog.target network.target

[Service]
User=root
Type=forking
PIDFile=camera-1.pid
ExecStart=/usr/local/bin/never_cli /nvr/cameras/camera-1.json
ExecStop=/usr/local/bin/never_cli /nvr/cameras/camera-1.json
TimeoutSec=100
TimeoutStopSec=300
RemainAfterExit=yes
WorkingDirectory=/nvr
```

This assumes you have a directory at root called `nvr`, this also assumes you have copied your compiled binary into `/usr/local/bin`
