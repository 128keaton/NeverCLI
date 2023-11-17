/*camera status manager starts, stops, restarts and show the status of camera daemons*/

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <systemd/sd-bus.h>
}

#include <iostream>
#include <string>
#include <filesystem>


/*free error object and destroy message and bus references*/
void free_and_destroy(sd_bus_error *error, sd_bus_message *message, sd_bus *bus) {
    sd_bus_error_free(error);
    sd_bus_message_unref(message);
    sd_bus_unref(bus);
    return;
}

/*function to start the requested service
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
int main(int argc, char **argv) {

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
    std::string full_path(json_filepath);
    std::string camera_id = full_path.substr(full_path.find("camera-"),
                                             (full_path.find(".json") - full_path.find("camera-")));

    /*send request to systemd*/
    if (strcmp(command, "start") == 0) {
        request_service("StartUnit", "nvr-record@", camera_id, "replace");
        request_service("StartUnit", "nvr-stream@", camera_id, "replace");
        std::cout << "{\"status\": " << "\"" << request_status("nvr-record@", camera_id) << "\"}" << std::endl;
    } else if (strcmp(command, "stop") == 0) {
        request_service("StopUnit", "nvr-record@", camera_id, "fail");
        request_service("StopUnit", "nvr-stream@", camera_id, "fail");
        std::cout << "{\"status\": " << "\"" << request_status("nvr-record@", camera_id) << "\"}" << std::endl;
    } else if (strcmp(command, "restart") == 0) {
        request_service("RestartUnit", "nvr-record@", camera_id, "replace");
        request_service("RestartUnit", "nvr-stream@", camera_id, "replace");
        std::cout << "{\"status\": " << "\"" << request_status("nvr-record@", camera_id) << "\"}" << std::endl;
    } else if (strcmp(command, "status") == 0) {
        std::cout << "{\"status\": " << "\"" << request_status("nvr-record@", camera_id) << "\"}" << std::endl;
        //std::cout<<"{\"status\": }"<<"\""<<request_status("nvr-stream@", camera_id)<<"\"}"<<std::endl;
    } else {
        fprintf(stderr, "Unknown command %s\n", argv[1]);
        exit(1);
    }
    return 0;
}
