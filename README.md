# Never CLI

## Dependencies

### Debian/Ubuntu

Please run the following to install all required dependencies:

```shell
sudo apt-get install pkg-config cmake ffmpeg build-essential libsystemd-dev \
                                            libavcodec-dev libavutil-dev libavformat-dev \
                      libcurl4-openssl-dev libavdevice-dev libavfilter-dev \
                      libspdlog-dev gstreamer1.0 libgstreamer1.0-dev \
                      gstreamer1.0-vaapi gstreamer1.0-tools gstreamer1.0-rtsp
```

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

Running the update script at `scripts/update-linux.sh` will update the repo, build the new version,
copy the binaries to `/usr/local/bin` and set the permissions.




### Recording

The recording/snapshot daemon is expected to live on the _remote_ NVR (meaning the box that is deployed with the site/tower)

You can run the daemon by calling `nvr_record /path/to/camera/json`


### systemd

There are two systemd unit templates included, one for streaming and one for recording.

To start recording:
```shell
systemd --user enable --now nvr-record@05e1486e-0fe9-4363-b925-f3f0c3815d84
```
This assumes you have a directory at root called `nvr`,
this also assumes you have copied your compiled binary into `/usr/local/bin`,
and this assumes `/nvr/cameras/` has a file named `05e1486e-0fe9-4363-b925-f3f0c3815d84.json`