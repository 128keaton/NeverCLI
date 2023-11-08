//
// Created by Keaton Burleson on 11/8/23.
//

#include "janus.h"

using json = nlohmann::json;

namespace nvr {
    Janus::Janus() {
        if ((out_sock = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
            exit(1);
        }

        struct sockaddr_un serv_addr{};
        bzero(&serv_addr, sizeof(serv_addr));
        serv_addr.sun_family = AF_UNIX;

        strcpy(serv_addr.sun_path, "/tmp/nvr");

        if (connect(out_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1) {
            exit(1);
        }

    }

    void Janus::cleanup() {
        close(out_sock);
    }

    json Janus::getStreamList() {
        json request;

        request["janus"] = "list";
        request["transaction"] = generateRandom();
        json response = sendAndReceive(request);
        return response;
    }

    string Janus::generateRandom() {
        static const char alphanum[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";
        std::string tmp_s;
        tmp_s.reserve(15);

        for (int i = 0; i < 15; ++i) {
            tmp_s += alphanum[random() % (sizeof(alphanum) - 1)];
        }

        return tmp_s;
    }


    int64_t Janus::getSessionID() {
        json request;

        request["janus"] = "create";
        request["transaction"] = generateRandom();

        json response = sendAndReceive(request);
        json data = response["data"];

        return data["id"];
    }

    int64_t Janus::getPluginHandlerID(int64_t sessionID) {
        json request;

        request["janus"] = "attach";
        request["session_id"] = sessionID;
        request["plugin"] = "janus.plugin.streaming";
        request["transaction"] = generateRandom();

        json response = sendAndReceive(request);
        json data = response["data"];

        return data["id"];
    }

    json Janus::sendAndReceive(const json &request) const {
        string request_str = request.dump();


        if (send(out_sock, request_str.data(), request_str.size(), 0) == -1) {
            printf("Client: Error on send() call \n");
        }

        string raw_response;

        char buf[1024] = {0};

        read(out_sock, buf, 1024 - 1);
        raw_response.append(buf);


        json response = json::parse(raw_response);

        return response;
    }

    bool
    Janus::createStream(int64_t sessionID, int64_t handlerID, const string &streamName, int64_t streamID,
                        int64_t port) {
        json request;
        json body;
        json mediaItem;
        json media;

        string mid = string(streamName).append("-").append(std::to_string(streamID));

        media["mid"] = mid;
        media["type"] = "video";
        media["codec"] = "h264";
        media["is_private"] = false;
        media["port"] = port;
        media["rtpmap"] = "H264/90000";
        media["pt"] =  96;
        media["fmtp"] = "profile-level-id=42e01f;packetization-mode=1";


        body["request"] = "create";
        body["name"] = streamName;
        body["type"] = "rtp";
        body["permanent"] = true;
        body["id"] = streamID;

        media = json::array({media});

        body["media"] = media;

        request["janus"] = "message";
        request["session_id"] = sessionID;
        request["handle_id"] = handlerID;
        request["transaction"] = generateRandom();
        request["body"] = body;

        json response = sendAndReceive(request);
        spdlog::info("Stream create response\n {}", response.dump());

        return true;
    }
}