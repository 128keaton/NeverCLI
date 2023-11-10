//
// Created by Keaton Burleson on 11/8/23.
//

#include "../common.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <chrono>
#include <thread>
#include <future>

#ifndef NEVER_CLI_JANUS_H
#define NEVER_CLI_JANUS_H

using json = nlohmann::json;

namespace nvr {
    class Janus {
    public:
        Janus();
        explicit Janus(std::shared_ptr<spdlog::logger> &logger);
        bool destroyStream(int64_t streamID);
        bool createStream(const string& streamName, int64_t streamID, int64_t port);
        int64_t getPluginHandlerID(int64_t sessionID);
        int64_t getSessionID();
        json getStreamList();
        bool connect();
        bool disconnect();
        void keepAlive();
        [[nodiscard]] bool isConnected() const;
        [[nodiscard]] bool isStreaming() const;

    private:
        bool sendKeepAlive();
        bool streaming = false;
        bool connected = false;
        int64_t _session_id = -1;
        int64_t _handler_id = -1;
        int64_t _stream_id = -1;
        std::shared_ptr<spdlog::logger> logger;
        static string generateRandom();
        int out_sock{};
        json buildMessage(json &body);
        static json buildMedia(const string &streamName, int64_t streamID, int64_t port);
        [[nodiscard]] json performRequest(const json& request) const;
    };
}

#endif //NEVER_CLI_JANUS_H
