#include "GpsReceiver.h"

namespace geoputer {

void GpsReceiver::begin(uint32_t now) {
    const auto revision = snapshot_.revision, sample = snapshot_.sample_seq;
    *this = GpsReceiver{};
    snapshot_.revision = revision; snapshot_.sample_seq = sample;
    started_at_ = now;
    snapshot_.reception = GpsReception::Waiting;
    changed(false);
}

void GpsReceiver::changed(bool sample) {
    if (++snapshot_.revision == 0) ++snapshot_.revision;
    changed_ = true;
    sample_changed_ = sample;
}

void GpsReceiver::updateState(GpsReception state) {
    if (snapshot_.reception == state) return;
    snapshot_.reception = state;
    if (state != GpsReception::Fix) snapshot_.fix = {};
    changed(false);
}

void GpsReceiver::feed(char byte, uint32_t now) {
    GpsFix position{};
    const auto result = parser_.feed(byte, position);
    if (result == NmeaResult::Rejected) ++rejected_sentences_;
    if (result != NmeaResult::Position && result != NmeaResult::OtherSentence) return;
    ++received_sentences_;
    snapshot_.received_at_ms = now; snapshot_.received = true;
    have_sentence_ = true;
    sentence_at_ = now;
    if (result != NmeaResult::Position) return;
    if (position.utc_ms == UINT32_MAX) {
        // An invalid startup sentence can lack UTC. Invalidate the displayed fix immediately.
        epoch_pending_ = false;
        have_fix_ = false;
        updateState(GpsReception::NoFix);
        return;
    }
    if (epoch_pending_ && epoch_.utc_ms != position.utc_ms) flush(now);
    if (!epoch_pending_) {
        if (have_epoch_) {
            const uint32_t delta = (position.utc_ms + 86400000u - last_epoch_) % 86400000u;
            if (delta == 0) {
                // A late void sentence for the same epoch must still revoke its displayed fix.
                if (!position.valid) {
                    have_fix_ = false;
                    updateState(GpsReception::NoFix);
                }
                return;
            }
            if (delta > 43200000u) return;  // Older epoch, including midnight rollover.
        }
        epoch_ = position;
        epoch_invalid_ = !position.valid;
        epoch_pending_ = true;
        epoch_at_ = now;
    } else {
        epoch_invalid_ |= !position.valid;
        mergeGpsFix(epoch_, position);
    }
}

void GpsReceiver::flush(uint32_t now) {
    if (!epoch_pending_) return;
    last_epoch_ = epoch_.utc_ms;
    have_epoch_ = true;
    epoch_pending_ = false;
    epoch_.valid = epoch_.valid && !epoch_invalid_;
    snapshot_.fix = epoch_;
    if (!epoch_.valid) {
        snapshot_.fix.latitude = snapshot_.fix.longitude = 0;
        snapshot_.fix.fields &= GpsTime | GpsHdop | GpsSatellites;
    }
    snapshot_.reception = epoch_.valid ? GpsReception::Fix : GpsReception::NoFix;
    have_fix_ = epoch_.valid;
    if (have_fix_) fix_at_ = now;
    ++snapshot_.sample_seq;
    changed(true);
}

void GpsReceiver::poll(uint32_t now) {
    if (epoch_pending_ && now - epoch_at_ >= MergeMs) flush(now);
    if (now - (have_sentence_ ? sentence_at_ : started_at_) >= StaleMs) {
        have_fix_ = false;
        have_epoch_ = false;  // Permit the first epoch again on receiver reconnection.
        updateState(GpsReception::NoData);
    } else if (have_sentence_ && (!have_fix_ || now - fix_at_ >= StaleMs)) {
        have_fix_ = false;
        updateState(GpsReception::NoFix);
    }
}

}  // namespace geoputer
