extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <systemd/sd-bus.h>
}
#include <iostream>
#include <string>
#define SERVER_SOCKET_PATH "server.socket"

/*free error object and destroy message and bus references*/
void free_and_destroy(sd_bus_error *error, sd_bus_message *message, sd_bus *bus) {
    sd_bus_error_free(error);
    sd_bus_message_unref(message);
    sd_bus_unref(bus);
    return;
}

/*function to start,stop or restart the requested service
Accepts systemd method, unit, camera_id and mode as args*/
std::string request_service(std::string method, std::string unit_name, std::string camera_id, std::string mode) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus *bus = NULL;
    const char *path;
    unit_name = unit_name + camera_id + std::string(".service");

    /* create a new independent connection to the system bus */
    int reference = sd_bus_open_system(&bus);
    if (reference < 0) {
        free_and_destroy(&error, reply, bus);
        return (std::string("sd_bus_open_system: ") + std::string(strerror(-reference)));
    }
    /*Call systemd method on the org.freedesktop.systemd1.Manager interface
     and store the response in reply, otherwise store error in error*/
    try {
        reference = sd_bus_call_method(bus, "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
                                       method.c_str(), &error, &reply, "ss", unit_name.c_str(), mode.c_str());
    } catch (std::logic_error *string_caught) {
        std::cout << string_caught->what() << std::endl;
    }
    if (reference < 0) {
        free_and_destroy(&error, reply, bus);
        return (std::string("sd_bus_call_method: ") + std::string(error.message));
    }
    /*parse the reply message into path */
    reference = sd_bus_message_read(reply, "o", &path);
    if (reference < 0) {
        free_and_destroy(&error, reply, bus);
        return (std::string("sd_bus_message_read: ") + std::string(strerror(-reference)));
    }

    free_and_destroy(&error, reply, bus);
    return (std::string(path));
}

/*function to check status of unit*/
std::string request_status(std::string unit_name, std::string camera_id) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus *bus = NULL;
    const char *primary_unit_name;
    const char *unit_description;
    const char *load_state;
    const char *active_state;
    const char *sub_state;
    const char *followed_unit;
    const char *unit_object_path;
    uint32_t job_id;
    const char *job_type_string;
    const char *job_object_path;
    unit_name = unit_name + camera_id + std::string(".service");

    /* create a new independent connection to the system bus */
    int reference = sd_bus_open_system(&bus);
    if (reference < 0) {
        free_and_destroy(&error, reply, bus);
        return (std::string("sd_bus_open_system: ") + std::string(strerror(-reference)));
    }

    /*Call ListUnitsByNames method on the org.freedesktop.systemd1.Manager interface
    and store the response in reply, otherwise store error in error*/
    try {
        reference = sd_bus_call_method(bus, "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
                                       "ListUnitsByNames", &error, &reply, "as", 1, (char *) unit_name.c_str());
    } catch (std::logic_error *string_caught) {
        std::cout << string_caught->what() << std::endl;
    }
    if (reference < 0) {
        free_and_destroy(&error, reply, bus);
        return (std::string("sd_bus_call_method: ") + std::string(error.message));
    }

    /*parse the reply message into variables */
    reference = sd_bus_message_read(reply, "a(ssssssouso)", 1, &primary_unit_name,
                                    &unit_description, &load_state, &active_state, &sub_state, &followed_unit,
                                    &unit_object_path, &job_id, &job_type_string, &job_object_path);
    if (reference < 0) {
        free_and_destroy(&error, reply, bus);
        return (std::string("sd_bus_message_read: ") + std::string(strerror(-reference)));
    }
    free_and_destroy(&error, reply, bus);
    return (std::string(active_state));
}

/*main function*/
int main(){
    int server_socket_fd, peer_socket_fd;   //file descriptors
    int sockaddr_un_length;
    /*set socket domain to unix domain i.e AF_UNIX */
    struct sockaddr_un peer_socket, server_socket = {
            .sun_family = AF_UNIX,
    };
    char stream_buffer[100];

    /*create a stream(TCP) socket in the unix domain*/
    if ((server_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    strcpy(server_socket.sun_path, SERVER_SOCKET_PATH);
    /*remove socket if it already exists to avoid EINVAL error*/
    unlink(server_socket.sun_path);
    sockaddr_un_length = (int)(strlen(server_socket.sun_path) + sizeof(server_socket.sun_family));
    /*bind server_socket_fd to a known address in the unix domain*/
    if (bind(server_socket_fd, (struct sockaddr *)&server_socket, sockaddr_un_length) == -1) {
        perror("bind");
        exit(1);
    }

    /*make server_socket passive and listen to max 32 incoming connections*/
    if (listen(server_socket_fd, 32) == -1) {
        perror("listen");
        exit(1);
    }

    /*endlessly wait for incoming connections and handle requests*/
    for(;;) {
        socklen_t socket_length = sizeof(peer_socket);
        //printf("Waiting for incoming connection\n");
        /*block until there is an incoming connection.
        Create an active peer_socket for recv() and send() operations.*/
        if ((peer_socket_fd = accept(server_socket_fd, (struct sockaddr *)&peer_socket, &socket_length)) == -1) {
            perror("accept");
            exit(1);
        }

        //printf("Connected to %s.\n", peer_socket.sun_path);

        while(recv(peer_socket_fd, stream_buffer, sizeof(stream_buffer), 0) >= 0) {
            /*extract request from stream_buffer*/
            struct request{
                char * command;
                char * camera_id;
            }req;
            char * response;
            char * ptr;
            ptr = stream_buffer;
            size_t string_length;
            memcpy(&string_length, ptr, sizeof(string_length));
            ptr += sizeof(string_length);
            req.command = (char *)malloc(string_length);
            memcpy(req.command, ptr, string_length);
            ptr += string_length;
            *(req.command + string_length) = '\0';
            memcpy(&string_length, ptr, sizeof(string_length));
            ptr += sizeof(string_length);
            req.camera_id = (char *)malloc(string_length);
            memcpy(req.camera_id, ptr, string_length);
            *(req.camera_id + string_length) = '\0';

            /*send request to systemd and copy response to stream_buffer*/
            if (strcmp(req.command, "start") == 0) {
                request_service("StartUnit", "nvr-record@", req.camera_id, "replace");
                request_service("StartUnit", "nvr-stream@", req.camera_id, "replace");
                sprintf(response, "{\"status\": \"%s\"}", (request_status("nvr-record@", req.camera_id)).c_str());
                memcpy(stream_buffer, response, strlen(response));
                send(peer_socket_fd, stream_buffer, strlen(response), 0);
                break;
            } else if (strcmp(req.command, "stop") == 0) {
                request_service("StopUnit", "nvr-record@", req.camera_id, "fail");
                request_service("StopUnit", "nvr-stream@", req.camera_id, "fail");
                sprintf(response, "{\"status\": \"%s\"}", (request_status("nvr-record@", req.camera_id)).c_str());
                memcpy(stream_buffer, response, strlen(response));
                send(peer_socket_fd, stream_buffer, strlen(response), 0);
                break;
            } else if (strcmp(req.command, "restart") == 0) {
                request_service("RestartUnit", "nvr-record@", req.camera_id, "replace");
                request_service("RestartUnit", "nvr-stream@", req.camera_id, "replace");
                sprintf(response, "{\"status\": \"%s\"}", (request_status("nvr-record@", req.camera_id)).c_str());
                memcpy(stream_buffer, response, strlen(response));
                send(peer_socket_fd, stream_buffer, strlen(response), 0);
                break;
            } else if (strcmp(req.command, "status") == 0) {
                sprintf(response, "{\"status\": \"%s\"}", (request_status("nvr-record@", req.camera_id)).c_str());
                memcpy(stream_buffer, response, strlen(response));
                send(peer_socket_fd, stream_buffer, strlen(response), 0);
                break;
            } else {
                sprintf(response, "Unknown command %s\n", req.command);
                memcpy(stream_buffer, response, strlen(response));
                send(peer_socket_fd, stream_buffer, strlen(response), 0);
                break;
            }
        }
        /*clear stream_buffer and teardown peer_socket*/
        memset(stream_buffer, '\0', sizeof(stream_buffer));
        close(peer_socket_fd);
    }
    return 0;
}
