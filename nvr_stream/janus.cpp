//
// Created by Keaton Burleson on 11/8/23.
//

#include "janus.h"

using json = nlohmann::json;

namespace nvr {

    const char *janus_socket = "/tmp/nvr";

    Janus::Janus() {
        this->logger = spdlog::get("janus");
    }

    Janus::Janus(std::shared_ptr<spdlog::logger> &logger) {
        this->logger = logger;
    }

    bool Janus::isConnected() const {
        return this->connected;
    }

    bool Janus::isStreaming() const {
        return this->streaming;
    }


    /**
     * Connect to the Janus UDS
     * @return
     */
    bool Janus::connect() {
        if (connected)
            return connected;

        logger->info("Connecting to Janus with socket '{}'", janus_socket);

        if ((out_sock = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
            logger->error("Could not initialize socket");
            return false;
        }

        struct sockaddr_un serv_addr{};
        bzero(&serv_addr, sizeof(serv_addr));
        serv_addr.sun_family = AF_UNIX;

        strcpy(serv_addr.sun_path, janus_socket);

        if (::connect(out_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1) {
            logger->error("Could not connect to Janus");
            return false;
        }

        connected = true;
        return connected;
    }


    /**
     * Get the list of streams
     * @return
     */
    json Janus::getStreamList() {
        json body;

        body["request"] = "list";

        json request = buildMessage(body);
        json response = performRequest(request);
        json plugin_data = response["plugindata"];
        json response_data = plugin_data["data"];


        return response_data["list"];
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
        if (_session_id > 0)
            return _session_id;

        json request;

        request["janus"] = "create";
        request["transaction"] = generateRandom();

        json response = performRequest(request);
        json data = response["data"];

        _session_id = data["id"];
        return _session_id;
    }

    int64_t Janus::getPluginHandlerID(int64_t sessionID) {
        if (_handler_id > 0)
            return _handler_id;

        json request;

        request["janus"] = "attach";
        request["session_id"] = sessionID;
        request["plugin"] = "janus.plugin.streaming";
        request["transaction"] = generateRandom();

        json response = performRequest(request);
        json data = response["data"];

        _handler_id = data["id"];
        return _handler_id;
    }

    json Janus::performRequest(const json &request) const {
        string request_str = request.dump();

        if (send(out_sock, request_str.data(), request_str.size(), 0) == -1) {
            logger->error("Could not send request: {}", request_str);
            logger->error("Could not send request: {}", request_str);
        }

        string raw_response;



        while (true) {
            char buffer[BUFSIZ];
            int bytes = (int) read(out_sock, &buffer, BUFSIZ);
            if (bytes <= 0)
                break;

            printf("bytes: %i\n", bytes);

            raw_response.append(buffer);

            spdlog::info(raw_response);

            auto o_tag_count = std::ranges::count(raw_response, '{');
            auto c_tag_count = std::ranges::count(raw_response, '}');

            if (abs(o_tag_count) == abs(c_tag_count))
                break;
        }


        json response = json::parse(raw_response);

        return response;
    }

    /**
     * Build a JSON plugin message
     * @param body Inner body
     * @return
     */
    json Janus::buildMessage(json &body) {
        json request;
        int64_t session_id;
        int64_t handler_id;

        session_id = this->getSessionID();
        handler_id = this->getPluginHandlerID(session_id);

        request["janus"] = "message";
        request["session_id"] = session_id;
        request["handle_id"] = handler_id;
        request["transaction"] = generateRandom();
        request["body"] = std::move(body);

        return request;
    }

    /**
     * Destroy a Janus stream
     * @param streamID ID of the stream to destroy
     * @return
     */
    bool Janus::destroyStream(int64_t streamID) {
        json body;

        body["request"] = "destroy";
        body["id"] = streamID;

        json request = buildMessage(body);
        json response = performRequest(request);

        json plugin_data = response["plugindata"];
        json response_data = plugin_data["data"];

        if (response_data.contains("destroyed") && response_data["destroyed"] == streamID)
            return response_data["streaming"] == string("destroyed");

        logger->error("Could not destroy stream with ID '{}'", streamID);
        logger->error("Destroy response: {}", response.dump());
        return false;
    }

    /**
     * Create a Media JSON array for the stream
     * @param streamName Readable stream name with hyphen
     * @param streamID Numeric stream ID
     * @param port RTP streaming port
     * @return
     */
    json Janus::buildMedia(const string &streamName, int64_t streamID, int64_t port) {
        json media;

        string mid = string(streamName)
                .append("_")
                .append(std::to_string(streamID))
                .append("_")
                .append(std::to_string(getpid()));

        media["mid"] = mid;
        media["type"] = "video";
        media["codec"] = "h264";
        media["is_private"] = false;
        media["port"] = port;
        media["rtpmap"] = "H264/90000";
        media["pt"] = 96;
        media["fmtp"] = "profile-level-id=42e01f;packetization-mode=1";

        media = json::array({media});
        return media;
    }

    /**
     * Create a stream on Janus
     * @param streamName Readable stream name with hyphen
     * @param streamID Numeric stream ID
     * @param port RTP streaming port
     * @return true if created
     */
    bool Janus::createStream(const string &streamName, int64_t streamID, int64_t port) {

        json list = getStreamList();

        for (auto &stream: getStreamList()) {
            if (stream.contains("id") && stream["id"] == streamID) {
                logger->warn("Destroying existing stream with ID '{}'", streamID);
                destroyStream(streamID);
                break;
            }
        }

        json body;

        logger->info("Creating Janus stream '{}'", streamName);

        body["request"] = "create";
        body["name"] = streamName;
        body["type"] = "rtp";
        body["id"] = streamID;
        body["media"] = buildMedia(streamName, streamID, port);

        json request = buildMessage(body);

        json response = performRequest(request);
        json plugin_data = response["plugindata"];
        json response_data = plugin_data["data"];

        if (response_data.contains("error")) {
            string error = response_data["error"];
            logger->error("Could not create stream: {}", error);
            logger->error(response_data.dump());
            streaming = false;
        } else if (response_data.contains("created")) {
            logger->info("Stream '{}' created", streamName);
            streaming = true;
        } else {
            logger->warn("Not sure, dumping response: {}", response.dump());
            streaming = false;
        }

        return streaming;
    }
}
