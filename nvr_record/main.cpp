#include "camera.h"

using std::thread;
using std::ifstream;
using json = nlohmann::json;

int startRecording(nvr::Camera *camera, long clip_runtime) {
    return camera->startRecording(clip_runtime);
}

static void spawnRecording(nvr::Camera *camera, long clip_runtime, const string &pid_file_name) {
    nvr::spawnTask(pid_file_name);
    startRecording(camera, clip_runtime);
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

    if (getenv("INVOCATION_ID")) {
        string pid_file_name = string("/run/");
        pid_file_name.append(config.stream_name);
        pid_file_name.append(".pid");

        spdlog::info("INVOCATION_ID is set, meaning we are running from systemd, using pid {}", pid_file_name);

        if (access(pid_file_name.c_str(), F_OK) == 0) {
            pid_t pid = nvr::readPID(pid_file_name);

            if (pid > 0) {
                kill(pid, 1);
            } else {
                spdlog::error("Could not find PID to kill");
                remove(pid_file_name.c_str());
            }
        }


        auto *camera = new nvr::Camera(config);
        spawnRecording(camera, config.clip_runtime, pid_file_name);
    } else {
        auto *camera = new nvr::Camera(config);
        if (!camera->connect()) {
            spdlog::error("Could not connect\n");
            return EXIT_FAILURE;
        }

        thread rec_thread(startRecording, camera, config.clip_runtime);
        rec_thread.join();
    }
}

