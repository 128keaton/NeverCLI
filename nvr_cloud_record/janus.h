//
// Created by Keaton Burleson on 1/24/24.
//

#ifndef NEVER_CLI_JANUS_H
#define NEVER_CLI_JANUS_H
#include "../common/common.h"

class JanusGateway {
public:
    JanusGateway(const string &serverUrl);

private:
    const string &serverURL;

};


#endif //NEVER_CLI_JANUS_H
