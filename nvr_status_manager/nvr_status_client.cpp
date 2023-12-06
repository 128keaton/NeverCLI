extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
}
#include <iostream>
#include <filesystem>
#include <string>
/*server socket to connect with*/
#define SERVER_SOCK_PATH "server.socket"

int main(int argc, char **argv){
    int peer_socket_fd;
    int sockaddr_un_length;
    struct sockaddr_un peer_socket = {
            .sun_family = AF_UNIX,
    };
    struct request{
        const char * command;
        const char * camera_id;
    };
    char stream_buffer[100];//stream buffer

    /*check if args are correct*/
    if (argc != 3) {
        printf("usage: %s SYSTEMD_COMMAND PATH_TO_CAMERA_JSON\n"
               "i.e. %s start /nvr/cameras/camera-1.json\n", argv[0], argv[0]);
        exit(1);
    }
    const char *command = argv[1];
    std::filesystem::path json_filepath = argv[2];
    if (!exists(json_filepath)) {
        fprintf(stderr, "File %s not found.\n", json_filepath.c_str());
        exit(1);
    }

    /*Extract camera-x substring*/
    std::string camera_id;
    try{
        std::string full_path(json_filepath);
        camera_id = full_path.substr(full_path.find("camera-"),
                                                 (full_path.find(".json") - full_path.find("camera-")));
    }catch(...){
        fprintf(stderr, "%s is not camera.json file\n", argv[2]);
        exit(1);
    }

    /*copy request to stream_buf*/
    request req = {command, camera_id.c_str()};
    char * ptr;
    ptr = stream_buffer;
    size_t string_length = strlen(req.command);
    memcpy(ptr, &string_length, sizeof(string_length));
    ptr += sizeof(string_length);
    memcpy(ptr, req.command, string_length);
    ptr += string_length;
    string_length = strlen(req.camera_id);
    memcpy(ptr, &string_length, sizeof(string_length));
    ptr += sizeof(string_length);
    memcpy(ptr, req.camera_id, string_length);

    /*create an active peer socket of type stream(TCP) in the unix domain */
    if ((peer_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    strcpy(peer_socket.sun_path, SERVER_SOCK_PATH);
    sockaddr_un_length = (int)(strlen(peer_socket.sun_path) + sizeof(peer_socket.sun_family));
    /*connect to server side*/
    if (connect(peer_socket_fd, (struct sockaddr *)&peer_socket, sockaddr_un_length) == -1) {
        perror("connect");
        exit(1);
    }

    /*send request to server*/
    if (send(peer_socket_fd, stream_buffer, sizeof(stream_buffer), 0) < 0) {
        fprintf(stderr,"Could not send req to nvr_status_server\n");
        exit(1);
    }else{
        /*print server response to terminal*/
        if ((string_length = recv(peer_socket_fd, stream_buffer, sizeof(stream_buffer)-1, 0)) > 0) {
            stream_buffer[string_length] = '\0';
            printf("%s", stream_buffer);
            printf("\n");
        } else {
            perror("recv");
            printf("\n");
            exit(1);
        }
    }
    close(peer_socket_fd);
    return 0;
}
