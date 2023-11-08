//
// Created by Keaton Burleson on 11/8/23.
//

#include "janus.h"

using json = nlohmann::json;

namespace nvr {
    Janus::Janus() {
        if ((out_sock = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
            printf("Client: Error on socket() call \n");
            exit(1);
        }


        struct sockaddr_un serv_addr{};
        bzero(&serv_addr, sizeof(serv_addr));
        serv_addr.sun_family = AF_UNIX;

        strcpy(serv_addr.sun_path, "/tmp/nvr");

        printf("Client: Trying to connect... \n");
        if (connect(out_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1) {
            printf("Client: Error on connect call \n");
            exit(1);
        }



        printf("Client: Connected \n");
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


    string Janus::getSessionID() {

        json request;
        request["janus"] = "create";
        request["transaction"] = generateRandom();

        string request_str = request.dump(4);

        printf("sent\n");
        if (send(out_sock, request_str.data(), request_str.size(), 0) == -1) {
            printf("Client: Error on send() call \n");
        }

        std::string raw_response;

        char buf[1024] = { 0 };

        printf("Waiting for response\n");

        read(out_sock, buf, 1024 - 1);
        raw_response.append(buf);

        json response = json::parse(raw_response);

        return response["data"]["id"];
    }
}