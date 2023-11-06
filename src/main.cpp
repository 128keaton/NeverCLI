#include "camera.h"
#include <thread>



void start_recording(never::Camera *camera, long clip_runtime) {
    camera->startRecording(clip_runtime);
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        printf("usage: %s RTSP_STREAM_URL SNAPSHOT_URL STREAM_NAME OUTPUT_PATH SPLIT_EVERY\n"
               "i.e. %s rtsp://0.0.0.0 http://0.0.0.0 camera1 ./ 60\n"
               "Write an RTSP stream to file.\n"
               "\n", argv[0], argv[0]);
        return 1;
    }


    const char *stream_url = argv[1],
            *snapshot_url = argv[2],
            *output_path = argv[3],
            *stream_name = argv[4];

    const long clip_runtime = strtol(argv[5], nullptr, 10);

    auto *camera = new never::Camera(stream_name, stream_url, snapshot_url, output_path);


    if (!camera->connect()) {
        printf("Could not connect\n");
        return EXIT_FAILURE;
    }

    std::thread t1(start_recording, camera, clip_runtime);
    t1.join();
}

