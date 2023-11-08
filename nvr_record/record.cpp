#include "recorder.h"

nvr::Recorder *recorder;

void quit(int sig)
{
    if (recorder->valid())
        recorder->quit();

    exit(sig);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 2) {
        spdlog::error("usage: %s camera-config.json\n"
                      "i.e. %s ./cameras/camera-1.json\n"
                      "Write an RTSP stream to file.\n"
                      "\n", argv[0], argv[0]);
        return 1;
    }

    const auto config = nvr::getConfig(argv[1]);

    recorder = new nvr::Recorder(config);

    signal(SIGINT, quit);

    if (!recorder->connect()) {
        spdlog::error("Could not connect\n");
        return EXIT_FAILURE;
    }


    return recorder->startRecording(config.clip_runtime);
}

