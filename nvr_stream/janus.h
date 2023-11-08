//
// Created by Keaton Burleson on 11/8/23.
//

#include "../common.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#ifndef NEVER_CLI_JANUS_H
#define NEVER_CLI_JANUS_H

namespace nvr {
    class Janus {
    public:
        Janus();
        string getSessionID();

    private:
      string generateRandom();

        int sock;
        int data_len;
        struct sockaddr_un remote{};
        char recv_msg[255]{};
        char send_msg[255]{};
    };
}

#endif //NEVER_CLI_JANUS_H
