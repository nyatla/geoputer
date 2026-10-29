#pragma once
#include "task/storage/log/LogReader.h"
namespace test_storage {
struct LogSlot {geoputer::ReadFileSlot slot;};
class LogReader:private LogSlot,public geoputer::LogReader {
public:
    explicit LogReader(std::string_view root=geoputer::config::LogDirectory,geoputer::StorageObserver* observer=nullptr,
        geoputer::ReadFileSlot* supplied=nullptr):geoputer::LogReader(root,observer,supplied?supplied:&slot) {}
};
}
