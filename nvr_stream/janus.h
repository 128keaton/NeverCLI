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

        int64_t getSessionID();

    private:
        string generateRandom();
        int out_sock;
    };
}

#endif //NEVER_CLI_JANUS_H
