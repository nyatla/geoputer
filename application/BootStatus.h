#pragma once

#include <cstdint>
#include <cstdio>
#include "task/storage/StorageTypes.h"

namespace geoputer {

enum class BootItem : uint8_t { Display, Tasks, Settings, SD, GPS, Dataset, Count };
enum class BootCheckState : uint8_t { Pending, Checking, Passed, Warning, Failed };
enum class BootSetupPrompt : uint8_t { None, Format, Directories };

struct BootCheck {
    BootCheckState state;
    char detail[12];
};

// App owns this progress; UI receives a fixed-size value copy.
struct BootStatus {
    static constexpr unsigned Count = static_cast<unsigned>(BootItem::Count);
    static constexpr uint32_t NormalHoldMs = 2000;
    static constexpr uint32_t IssueHoldMs = 5000;
    BootCheck checks[Count]{};
    bool complete = false;
    bool maintenance = false; // Selected before GIS startup, or from a stopped error screen.
    BootSetupPrompt setup_prompt = BootSetupPrompt::None;

    void set(BootItem item, BootCheckState state, const char* detail) {
        auto& check = checks[static_cast<unsigned>(item)];
        check.state = state;
        std::snprintf(check.detail, sizeof(check.detail), "%.9s", detail);
    }
    bool hasIssues() const {
        for (const auto& check : checks)
            if (check.state == BootCheckState::Warning || check.state == BootCheckState::Failed) return true;
        return false;
    }
    bool finish() {
        for (const auto& check : checks)
            if (check.state == BootCheckState::Pending || check.state == BootCheckState::Checking) return false;
        complete = true;
        return true;
    }
    uint32_t holdMs() const { return hasIssues() ? IssueHoldMs : NormalHoldMs; }

    void settings(SettingsState state) {
        if (state == SettingsState::Loaded || state == SettingsState::Saved)
            set(BootItem::Settings, BootCheckState::Passed, "LOADED");
        else if (state == SettingsState::Defaults)
            set(BootItem::Settings, BootCheckState::Passed, "DEFAULT");
        else set(BootItem::Settings, BootCheckState::Failed, state == SettingsState::Invalid ? "INVALID" : "NVS ERROR");
    }
    void card(CardState state) {
        switch (state) {
            case CardState::Mounted: set(BootItem::SD, BootCheckState::Passed, "FAT32"); break;
            case CardState::NoCard: set(BootItem::SD, BootCheckState::Warning, "NO CARD"); break;
            case CardState::Unsupported: set(BootItem::SD, BootCheckState::Warning, "NOT FAT32"); break;
            case CardState::NeedsSetup: set(BootItem::SD, BootCheckState::Warning, "NO SETUP"); break;
            default: set(BootItem::SD, BootCheckState::Failed, "SD ERROR"); break;
        }
    }
    void dataset(PlaceState state) {
        switch (state) {
            case PlaceState::Unselected: set(BootItem::Dataset, BootCheckState::Passed, "NONE"); break;
            case PlaceState::Ready: set(BootItem::Dataset, BootCheckState::Passed, "OPEN OK"); break;
            case PlaceState::NoCard: set(BootItem::Dataset, BootCheckState::Warning, "NO SD"); break;
            case PlaceState::DatasetMissing: set(BootItem::Dataset, BootCheckState::Warning, "MISSING"); break;
            case PlaceState::MetadataError: set(BootItem::Dataset, BootCheckState::Failed, "CONFIG"); break;
            case PlaceState::VersionError: set(BootItem::Dataset, BootCheckState::Failed, "VERSION"); break;
            case PlaceState::EncodingError: set(BootItem::Dataset, BootCheckState::Failed, "ENCODING"); break;
            case PlaceState::TooManyChunks: set(BootItem::Dataset, BootCheckState::Warning, "TOO LARGE"); break;
            case PlaceState::OutOfMemory: set(BootItem::Dataset, BootCheckState::Warning, "NO MEMORY"); break;
            default: set(BootItem::Dataset, BootCheckState::Failed, "DATA ERR"); break;
        }
    }
};

inline const char* bootItemName(unsigned item) {
    static const char* const names[] = {"DISPLAY", "TASKS/QUEUES", "NVS SETTINGS", "SD CARD", "GPS UART", "GALUCHAT DATA"};
    return item < BootStatus::Count ? names[item] : "";
}

}  // namespace geoputer
