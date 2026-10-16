#pragma once

#include "../../TaskMessages.h"
#include "../../utils/Task.h"
#include "UiModel.h"

namespace geoputer {

class UiTask final : public Task<TaskMessage, 1> {
public:
    UiTask(Queue<TaskMessage, 8>& notifications, Queue<TaskMessage, 1>& results)
        : notifications_(notifications), results_(results) {}

protected:
    void run() override;

private:
    void handle(const TaskMessage& request, bool ready);
    void sendReturn();
    void notifyContentChanged();

    Queue<TaskMessage, 8>& notifications_;
    Queue<TaskMessage, 1>& results_;
    UiModel model_;
    TaskMessage pending_return_{};
    bool has_return_ = false;
    uint32_t sequence_ = 0;
};

}  // namespace geoputer
