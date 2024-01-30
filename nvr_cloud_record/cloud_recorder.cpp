//
// Created by Keaton Burleson on 1/24/24.
//

#include "cloud_recorder.h"

namespace nvr {
    std::shared_ptr<CloudRecorder> CloudRecorder::instance(){
        static std::shared_ptr<CloudRecorder> s{new CloudRecorder};
        return s;
    }

}