#pragma once

#include "../../GpsMessages.h"
#include "../../utils/Task.h"
#include "GpsUiModel.h"
#include "UiFrameBuffer.h"
#include "../../hardware/AudioOutput.h"

namespace geoputer {

class GpsUiTask final : public Task<UiRequest, 1> {
public:
    GpsUiTask(Queue<AppNotification, 8>& notifications, Queue<ReturnMessage, 1>& results)
        : notifications_(notifications), results_(results) {}

    void setPowerMode(BootMode mode){power_manager_.configure(power::Level::None,mode);}

protected:
    void run() override;

private:
    void handle(const UiRequest& request, bool ready);
    void sendReturn();
    void notifyContentChanged();

    Queue<AppNotification, 8>& notifications_;
    Queue<ReturnMessage, 1>& results_;
    // GpsUiTask has application lifetime; this framebuffer never occupies task stack.
    UiFrameBuffer frame_buffer_;
    GpsUiModel model_;
    power::PowerManager power_manager_;
    AudioOutput audio_;
    ReturnMessage pending_return_{};
    bool has_return_ = false;
    uint32_t sequence_ = 0;
};

}  // namespace geoputer
