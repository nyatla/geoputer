#pragma once

#include "SettingsStore.h"

namespace geoputer {

// Storage-only transition. Candidate validation never calls this function.
// The RAM selection is cleared even if persistence fails; the diagnostic records
// that the previous selection may still be present in NVS at the next boot.
inline bool releaseUnusableDataset(SettingsStore& store, StorageSnapshot& state, PlaceState reason) {
    if (!state.settings.dataset || !datasetUnavailable(reason)) return false;
    auto& failure = state.dataset_failure;
    if (++failure.revision == 0) ++failure.revision;
    failure.number = state.settings.dataset;
    failure.reason = reason;
    Settings cleared = state.settings;
    setSettingValue(cleared, static_cast<unsigned>(SettingId::Dataset), 0);
    failure.save_failed = !store.save(cleared);
    state.settings = cleared;
    state.settings_state = failure.save_failed ? SettingsState::Failed : SettingsState::Saved;
    state.nvs_error = failure.save_failed ? -21 : 0;
    if (failure.save_failed) state.error = state.nvs_error;
    return true;
}

} // namespace geoputer
