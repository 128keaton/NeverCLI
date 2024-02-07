//
// Created by Keaton Burleson on 11/8/23.
//

#include "janus.h"

#include <utility>

using json = nlohmann::json;
using nvr_logger = std::shared_ptr<spdlog::logger>;

namespace nvr {

    const char *janus_socket = "/tmp/nvr";

    Janus::Janus() {
        this->logger = spdlog::get("janus");
    }

    Janus::Janus(nvr_logger &logger) {
        this->logger = logger;
    }

    bool Janus::isConnected() const {
        return this->connected;
    }

    bool Janus::isStreaming() const {
        return this->streaming;
    }

    int64_t Janus::getStreamID() {
        return this->_stream_id;
    }

    void Janus::keepAlive() {
        std::thread{
                [this]() {
                    auto result = std::async(std::launch::async, [&] {
                        while (connected) {
                            if (!sendKeepAlive())
                                std::this_thread::sleep_for(std::chrono::seconds(1));
                            else
                                std::this_thread::sleep_for(std::chrono::seconds(15));
                        }

                        return;
                    });
                    result.get();
                }
        }.detach();
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
            logger->error("Could not initialize Janus socket");
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

    bool Janus::disconnect() {
        if (streaming && _stream_id != -1)
            if (!destroyStream(_stream_id))
                logger->warn("Could not destroy Janus stream with ID '{}'", _stream_id);


        close(out_sock);

        connected = false;
        return true;
    }

    /**
     * Find the stream ID by a description
     * @param description
     * @return
     */
    int64_t Janus::findStreamID(const string &description) {
        json stream_list = getStreamList();

        int64_t stream_id = -1;

        for (auto &stream: stream_list)
            if (stream.contains("description") && stream["description"] == description)
                stream_id = stream["id"];

        return stream_id;
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

    int64_t Janus::getPluginHandlerID(int64_t session_id) {
        if (_handler_id > 0)
            return _handler_id;

        json request;

        request["janus"] = "attach";
        request["session_id"] = session_id;
        request["plugin"] = "janus.plugin.streaming";
        request["transaction"] = generateRandom();

        json response = performRequest(request);
        json data = response["data"];

        _handler_id = data["id"];
        return _handler_id;
    }

    bool Janus::sendKeepAlive() {
        json request;
        int64_t session_id;

        logger->debug("Sending keep-alive");
        session_id = this->getSessionID();

        request["janus"] = "keepalive";
        request["session_id"] = session_id;
        request["transaction"] = generateRandom();

        json response = performRequest(request);

        if (response.contains("janus")) {
            logger->flush();
            return response["janus"] == "ack";
        } else
            logger->error("Keep-alive response: {}", response.dump());


        return false;
    }

    json Janus::performRequest(const json &request) const {
        string request_str = request.dump();

        if (send(out_sock, request_str.data(), request_str.size(), 0) == -1) {
            logger->error("Could not send request: {}", request_str);
        }

        char *buffer;
        clock_t begin = clock();
        string raw_response = string();
        int total_bytes = 0;

        while (true) {
            buffer = (char *) malloc((BUFSIZ + 1) * sizeof(char));
            int bytes = (int) read(out_sock, buffer, BUFSIZ);
            total_bytes += bytes;

            if (bytes <= 0 && total_bytes > 0) {
                free(buffer);
                break;
            }

            raw_response.append(buffer);
            free(buffer);

            // Using the fact that we need to have the same number of opening/closing tags with JSON for it to be valid
            auto o_tag_count = std::ranges::count(raw_response, '{');
            auto c_tag_count = std::ranges::count(raw_response, '}');

            if (abs(o_tag_count) == abs(c_tag_count))
                break;

            clock_t end = clock();
            double time_spent = (double) (end - begin) / CLOCKS_PER_SEC;

            if (time_spent >= 5) {
                logger->error("Timed out waiting for reply from Janus");
                break;
            }
        }


        if (!raw_response.ends_with('}')) {
            auto last_pos = raw_response.find_last_of('}');
            auto initial_raw_response = string(raw_response);

            raw_response = raw_response.substr(0, last_pos + 1);

            if (initial_raw_response.size() > raw_response.size()) {
                logger->warn("Removed {} bytes from end of response",
                             (initial_raw_response.size() - raw_response.size()));
            }
        }

        if (raw_response.empty() || !raw_response.starts_with('{')) {
            logger->error("Raw response is not valid JSON: '{}'", raw_response);
        } else {
            try {
                json response = json::parse(raw_response);
                return response;
            } catch (json::exception &exception) {
                logger->error("Could not parse JSON: {}", exception.what());
                logger->error("Full response: {}", raw_response);
            }
        }

        json response;
        response["message"] = "error";

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
     * @param camera_id ID of the stream to destroy
     * @return
     */
    bool Janus::destroyStream(int64_t stream_id) {
        json body;

        body["request"] = "destroy";
        body["id"] = stream_id;

        json request = buildMessage(body);
        json response = performRequest(request);

        json plugin_data = response["plugindata"];
        json response_data = plugin_data["data"];

        if (response_data.contains("destroyed") && response_data["destroyed"] == stream_id) {
            streaming = !(response_data["streaming"] == string("destroyed"));
            return !streaming;
        }

        logger->error("Could not destroy stream with ID '{}'", stream_id);
        logger->error("Destroy response: {}", response.dump());
        return false;
    }

    /**
     * Create a Media JSON array for the stream
     * @param port RTP streaming port
     * @param media_id Media ID
     * @param codec Media codec, either vp8 or h264
     *
     * @return
     */
    json Janus::buildMedia(int64_t port, int64_t media_id, const string& codec) {
        json media;

        media["mid"] = std::to_string(media_id);
        media["type"] = "video";
        media["codec"] = codec;
        media["is_private"] = false;
        media["port"] = port;
        media["pt"] = 96;

        media = json::array({media});
        return media;
    }

    /**
     * Create a stream on Janus
     * @param camera_id Readable camera ID with hyphen
     * @param port RTP streaming port
     * @param codec Media codec, either vp8 or h264
     * @return true if created
     */
    bool Janus::createStream(const string &camera_id, int64_t port, string codec) {
        json body;
        json metadata;

        logger->info("Creating Janus stream '{}'", camera_id);

        int64_t media_id = generateMediaID();
        metadata["cameraID"] = camera_id;

        body["request"] = "create";
        body["name"] = camera_id;
        body["type"] = "rtp";
        body["media"] = buildMedia(port, media_id, std::move(codec));
        body["metadata"] = to_string(metadata);
        body["threads"] = 2;

        logger->debug("Body: {}", body.dump());

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
            json stream_data = response_data["stream"];

            this->_stream_id = stream_data["id"];

            logger->info("Stream '{}' created, has ID '{}' and media ID '{}'", camera_id, this->getStreamID(),
                         media_id);
            streaming = true;
        } else {
            logger->warn("Not sure, dumping response: {}", response.dump());
            streaming = false;
        }

        return streaming;
    }

    int64_t Janus::generateMediaID() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<unsigned long long> dis;

        int64_t mid = dis(gen) % 9 + 1;
        int index;

        for (index = 0; index < 15; index++) {
            mid *= 10;
            mid += dis(gen) % 10;
        }

        return mid;
    }
}
