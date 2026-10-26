#pragma once
#include "../../TaskMessages.h"
#include "../../utils/Queue.h"
#include <cassert>
#include <new>

namespace geoputer {
// Only the Storage task writes this area. A queued value is a separate copy;
// keep the active member unchanged until that copy has been accepted.
class StorageReplies {
public:
    bool idle() const { return kind_ == Kind::Idle; }
    bool returnPending() const { return kind_ == Kind::Return; }
    bool completionPending() const { return kind_ == Kind::Completion; }
    ReturnMessage& beginReturn() {
        assert(idle());
        new (&data_.result) ReturnMessage{{}, {}}; kind_ = Kind::Return;
        return data_.result;
    }
    AppNotification& beginCompletion() {
        assert(idle());
        new (&data_.completion) AppNotification{{}, {}}; kind_ = Kind::Completion;
        return data_.completion;
    }
    void sendReturn(Queue<ReturnMessage, 1>& destination) {
        if (returnPending() && sendSpeculative(destination, data_.result) == SpeculativeResult::Queued)
            kind_ = Kind::Idle;
    }
    void notifyCompletion(Queue<AppNotification, 8>& destination) {
        if (completionPending() && sendSpeculative(destination, data_.completion) == SpeculativeResult::Queued)
            kind_ = Kind::Idle;
    }
private:
    enum class Kind : uint8_t { Idle, Return, Completion };
    union Data {
        ReturnMessage result;
        AppNotification completion;
        constexpr Data() : result{} {}
    } data_{};
    Kind kind_ = Kind::Idle;
};
} // namespace geoputer
