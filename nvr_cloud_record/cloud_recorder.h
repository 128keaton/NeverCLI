//
// Created by Keaton Burleson on 1/24/24.
//

#ifndef NEVER_CLI_CLOUD_RECORDER_H
#define NEVER_CLI_CLOUD_RECORDER_H
#include "../common/common.h"

namespace nvr {
    class CloudRecorder {
    public:
        CloudRecorder(CloudRecorder const&) = delete;
        CloudRecorder& operator=(CloudRecorder const&) = delete;
        static std::shared_ptr<CloudRecorder> instance();
    };
}

#endif //NEVER_CLI_CLOUD_RECORDER_H
