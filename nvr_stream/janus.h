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
#include <random>

#ifndef NEVER_CLI_JANUS_H
#define NEVER_CLI_JANUS_H

using json = nlohmann::json;
using nvr_logger = std::shared_ptr<spdlog::logger>;

namespace nvr {
    class Janus {
    public:
        Janus();
        explicit Janus(nvr_logger &logger);
        void keepAlive();

        int64_t getPluginHandlerID(int64_t sessionID);
        int64_t getSessionID();
        json getStreamList();

        bool destroyStream(const string& streamID);
        bool createStream(const string& streamName, const string& streamID, int64_t port);
        bool connect();
        bool disconnect();

        [[nodiscard]] bool isConnected() const;
        [[nodiscard]] bool isStreaming() const;

    private:
        bool sendKeepAlive();
        bool streaming = false;
        bool connected = false;

        nvr_logger logger;
        int out_sock{};

        int64_t _session_id = -1;
        int64_t _handler_id = -1;
        string _stream_id = "";

        [[nodiscard]] json performRequest(const json& request) const;
        json buildMessage(json &body);

        static string generateRandom();
        static int64_t generateMediaID();
        static json buildMedia(const string &streamName, const string& streamID, int64_t port);
    };
}

#endif //NEVER_CLI_JANUS_H
