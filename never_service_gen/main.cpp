//
// Created by Keaton Burleson on 11/6/23.
//
#define SYSTEM_D_UNIT_PATH "/etc/systemd/system/"
#define LOAD_DAEMON_INTO_SYSD "systemctl start test.timer && "\
    "systemctl enable test.service"

#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 2) {
        printf("usage: %s camera-name camera-config.json\n"
                      "i.e. %s  camera-name ./cameras/camera-1.json\n"
                      "\n", argv[0], argv[0]);
        return 1;
    }


    // create the timer daemon
    std::ofstream ofs_t { SYSTEM_D_UNIT_PATH "" };
    ofs_t << "[Unit]\n" << "Description=test\n\n"
          << "[Timer]\n" << "OnBootSec=15s\n" << "OnUnitActiveSec=15min\n";

    // now create the service itself
    std::ofstream ofs_s { SYSTEM_D_UNIT_PATH "test.service" };
    ofs_s << "[Unit]\n" << "Description=test\n\n"
          << "[Service]\n" << "ExecStart=/home/gcoh/testscript\n\n"
          << "[Install]\n" << "WantedBy=multi-user.target\n";

    // finally, enable the service and start the timer
    std::system(LOAD_DAEMON_INTO_SYSD);
}