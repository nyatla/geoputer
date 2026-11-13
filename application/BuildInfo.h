#pragma once
#include "Version.h"

#if __has_include(<BuildMetadata.h>)
#include <BuildMetadata.h>
#else
// Standalone/host builds may omit the hook; never label unknown metadata clean.
#define GEOPUTER_BUILD_DATE "UNKNOWN"
#define GEOPUTER_BUILD_TIME "UNKNOWN"
#define GEOPUTER_GIT_REVISION "unknown"
#define GEOPUTER_GIT_DIRTY -1
#endif

namespace geoputer {
inline constexpr char BuildDate[] = GEOPUTER_BUILD_DATE;
inline constexpr char BuildTime[] = GEOPUTER_BUILD_TIME;
inline constexpr char BuildGitRevision[] = GEOPUTER_GIT_REVISION;
inline constexpr int BuildGitDirty = GEOPUTER_GIT_DIRTY;
inline constexpr const char* buildGitSuffix() {
    // A real revision is hexadecimal; an unavailable revision is already labelled unknown.
    return BuildGitRevision[0] == 'u' ? "" :
           BuildGitDirty > 0 ? "+dirty" : BuildGitDirty < 0 ? "+unknown" : "";
}
}  // namespace geoputer
