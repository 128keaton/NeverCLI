//
// Created by Keaton Burleson on 11/8/23.
//

#include "streamer.h"

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 2) {
        spdlog::error("usage: {} camera-config.json\n"
                      "i.e. {} ./cameras/camera-1.json\n"
                      "Stream an RTSP camera to an RTP port.\n", argv[0], argv[0]);
        return 1;
    }

    const char *config_file = argv[1];
    const auto config = nvr::getConfig(config_file);

    auto streamer = nvr::Streamer(config);

    return streamer.start();
}
