//
// Created by Keaton Burleson on 11/8/23.
//

#include "janus.h"

using json = nlohmann::json;

namespace nvr {
    Janus::Janus() {
        sock = 0;
        data_len = 0;

        memset(recv_msg, 0, 255 * sizeof(char));
        memset(send_msg, 0, 255 * sizeof(char));

        if ((sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
            printf("Client: Error on socket() call \n");
            exit(1);
        }

        remote.sun_family = AF_UNIX;
        strcpy(remote.sun_path, "/tmp/nvr");
        data_len = strlen(remote.sun_path) + sizeof(remote.sun_family);


        printf("Client: Trying to connect... \n");
        if (connect(sock, (struct sockaddr *) &remote, data_len) == -1) {
            printf("Client: Error on connect call \n");
            exit(1);
        }

        printf("Client: Connected \n");
    }

    string Janus::generateRandom(const int len) {
        static const char alphanum[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";
        std::string tmp_s;
        tmp_s.reserve(len);

        for (int i = 0; i < len; ++i) {
            tmp_s += alphanum[random() % (sizeof(alphanum) - 1)];
        }

        return tmp_s;
    }


    string Janus::getSessionID() {

        json request;
        request["janus"] = "create";
        request["transaction"] = generateRandom(15);

        string request_str = request.dump();

        if (send(sock, request_str.data(), request_str.size(), 0) == -1) {
            printf("Client: Error on send() call \n");
        }

        memset(send_msg, 0, 255 * sizeof(char));
        memset(recv_msg, 0, 255 * sizeof(char));

        std::string response;

        ssize_t n;
        char buf[256];

        while((n = recv(sock, buf, sizeof(buf), 0)) > 0)
            response.append(buf, buf + n);

        return response;
    }
}