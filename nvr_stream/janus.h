//
// Created by Keaton Burleson on 11/8/23.
//

#include "../common.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#ifndef NEVER_CLI_JANUS_H
#define NEVER_CLI_JANUS_H

using json = nlohmann::json;

namespace nvr {
    class Janus {
    public:
        Janus();

        bool createStream(int64_t sessionID, int64_t handlerID, const string& streamName, int64_t streamID, int64_t port);
        int64_t getPluginHandlerID(int64_t sessionID);
        int64_t getSessionID();
        json getStreamList();
        void cleanup();

    private:
        static string generateRandom();
        int out_sock;

        [[nodiscard]] json sendAndReceive(const json& request) const;
    };
}

#endif //NEVER_CLI_JANUS_H
