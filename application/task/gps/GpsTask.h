#pragma once

#include <HardwareSerial.h>
#include "../../TaskMessages.h"
#include "../../utils/Task.h"
#include "GpsReceiver.h"

namespace geoputer {

class GpsTask final : public Task<GpsRequest, 1> {
public:
    GpsTask(Queue<AppNotification, 8>& notifications, Queue<ReturnMessage, 1>& results)
        : notifications_(notifications), results_(results) {}

protected:
    void run() override;

private:
    void handle(const GpsRequest& request);
    void sendReturn();
    void notify();

    Queue<AppNotification, 8>& notifications_;
    Queue<ReturnMessage, 1>& results_;
    HardwareSerial serial_{1};
    GpsReceiver receiver_;
    ReturnMessage pending_return_{};
    bool has_return_ = false;
    bool ready_ = false;
    uint32_t sequence_ = 0;
    uint32_t coalesced_ = 0;
    uint32_t failed_revision_ = 0;
};

}  // namespace geoputer
