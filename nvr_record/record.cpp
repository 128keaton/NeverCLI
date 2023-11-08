#include "recorder.h"

using std::thread;

int startRecording(nvr::Recorder *recorder, long clip_runtime) {
    return recorder->startRecording(clip_runtime);
}


int main(int argc, char **argv) {
    if (argc < 2 || argc > 2) {
        spdlog::error("usage: %s camera-config.json\n"
                      "i.e. %s ./cameras/camera-1.json\n"
                      "Write an RTSP stream to file.\n"
                      "\n", argv[0], argv[0]);
        return 1;
    }

    const char *config_file = argv[1];
    const auto config = nvr::getConfig(config_file);


    auto *recorder = new nvr::Recorder(config);
    if (!recorder->connect()) {
        spdlog::error("Could not connect\n");
        return EXIT_FAILURE;
    }

    thread rec_thread(startRecording, recorder, config.clip_runtime);
    rec_thread.join();
}

