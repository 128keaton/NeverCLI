//
// Created by Keaton Burleson on 11/8/23.
//

#include "streamer.h"
#include "janus.h"
#include <thread>

nvr::Streamer streamer;


void quit(int sig)
{
    if (streamer.valid())
        streamer.quit();

    exit(sig);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 2) {
        spdlog::error("usage: {} camera-config.json\n"
                      "i.e. {} ./cameras/camera-1.json\n"
                      "Stream an RTSP camera to an RTP port.\n", argv[0], argv[0]);
        return 1;
    }

    auto config = nvr::getConfig(argv[1]);
    streamer = nvr::Streamer(config);

    signal(SIGINT, quit);
    return streamer.start();
}
