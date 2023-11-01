#include "record/record_to_file.h"


int did_finish = 0;

void force_finish([[maybe_unused]] int code) {
    printf("yes\n");
    did_finish = 1;
}

int main(int argc, char **argv) {
    if (argc < 4 || argc > 5) {
        printf("usage: %s RTSP_STREAM_URL OUTPUT_PATH STREAM_NAME SPLIT_EVERY\n"
               "i.e. %s rtsp://0.0.0.0 ./ 60\n"
               "Write an RTSP stream to file.\n"
               "\n", argv[0], argv[0]);
        return 1;
    }


    const char *stream_url = argv[1],
            *output_path = argv[2],
            *stream_name = argv[3];

    const long clip_runtime = strtol(argv[4], nullptr, 10);

    auto *recordToFile = new RecordToFile(stream_url, output_path, stream_name, clip_runtime);

    int return_state = EXIT_SUCCESS;
    std::signal(SIGINT, force_finish);
    return_state = recordToFile->start(did_finish);

    avformat_network_deinit();
    return return_state;
}

