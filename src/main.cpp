#include "camera.h"
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <fstream>
#include <csignal>

using std::ifstream;
using json = nlohmann::json;

int start_recording(never::Camera *camera, long clip_runtime) {
   return camera->startRecording(clip_runtime);
}

static void write_pid(pid_t pid, const string &pid_file_name) {
    FILE *pid_file;

    pid_file = fopen(pid_file_name.c_str(), "w");
    fprintf(pid_file, "%d", pid);
    fclose(pid_file);
}

static void spawn_camera_recording(never::Camera *camera, long clip_runtime, const string &pid_file_name) {
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    write_pid(getpid(), pid_file_name);
    start_recording(camera, clip_runtime);
}


static pid_t read_pid(const string &pid_file_name) {
    ifstream pid_file(pid_file_name.c_str());
    if (!pid_file.is_open()) {
        remove(pid_file_name.c_str());
        return 0;
    }

    int number;
    while (pid_file >> number) {
    }

    pid_file.close();
    return number;
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
    const string config_file_path = string(config_file);

    size_t last_path_index = config_file_path.find_last_of('/');
    string config_file_name = config_file_path.substr(last_path_index + 1);
    size_t last_ext_index = config_file_name.find_last_of('.');
    string stream_name = config_file_name.substr(0, last_ext_index);

    if (access(config_file, F_OK) != 0) {
        spdlog::error("Cannot read config file: {}", config_file);
        exit(-1);
    }

    std::ifstream config_stream(config_file);
    json config = json::parse(config_stream);

    const long clip_runtime = config["splitEvery"];

    const string stream_url = config["streamURL"];
    const string snapshot_url = config["snapshotURL"];
    const string output_path = config["outputPath"];



    if (getenv("INVOCATION_ID")) {
        spdlog::info("INVOCATION_ID is set, meaning we are running from systemd");
        string pid_file_name = string("/run/");
        pid_file_name.append(stream_name);
        pid_file_name.append(".pid");

        if (access(pid_file_name.c_str(), F_OK) == 0) {
            pid_t pid = read_pid(pid_file_name);

            if (pid > 0) {
                kill(pid, 1);
            } else {
                spdlog::error("Could not find PID to kill");
                remove(pid_file_name.c_str());
            }
        }

        auto *camera = new never::Camera(stream_name, stream_url, snapshot_url, output_path);
        spawn_camera_recording(camera, clip_runtime, pid_file_name);
    } else {
        auto *camera = new never::Camera(stream_name, stream_url, snapshot_url, output_path);
        if (!camera->connect()) {
            spdlog::error("Could not connect\n");
            return EXIT_FAILURE;
        }


        std::thread rec_thread(start_recording, camera, clip_runtime);
        rec_thread.join();
    }
}

