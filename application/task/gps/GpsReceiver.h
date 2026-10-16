#pragma once

#include "NmeaParser.h"

namespace geoputer {

// Hardware-free reception/epoch state, owned exclusively by GpsTask.
class GpsReceiver {
public:
    static constexpr uint32_t StaleMs = 3000;
    static constexpr uint32_t MergeMs = 80;
    void begin(uint32_t now);
    void feed(char byte, uint32_t now);
    void poll(uint32_t now);
    const GpsSnapshot& snapshot() const { return snapshot_; }
    bool sampleChanged() const { return sample_changed_; }
    bool changed() const { return changed_; }
    uint32_t receivedSentences() const { return received_sentences_; }
    uint32_t rejectedSentences() const { return rejected_sentences_; }
    void notificationSent() { changed_ = false; sample_changed_ = false; }

private:
    void flush(uint32_t now);
    void updateState(GpsReception state);
    void changed(bool sample);

    NmeaParser parser_;
    GpsSnapshot snapshot_{};
    GpsFix epoch_{};
    bool epoch_pending_ = false;
    bool epoch_invalid_ = false;
    bool have_sentence_ = false;
    bool have_fix_ = false;
    bool have_epoch_ = false;
    bool changed_ = false;
    bool sample_changed_ = false;
    uint32_t started_at_ = 0;
    uint32_t sentence_at_ = 0;
    uint32_t fix_at_ = 0;
    uint32_t epoch_at_ = 0;
    uint32_t last_epoch_ = 0;
    uint32_t received_sentences_ = 0;
    uint32_t rejected_sentences_ = 0;
};

}  // namespace geoputer
