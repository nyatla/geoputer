#pragma once
#include <cstddef>
#include <cstdint>
namespace geoputer::wifi {
inline constexpr size_t PasswordBytes=17;
using RandomWord=uint32_t(*)();
// Keeps the existing WAP1 NVS record. Output changes only after a successful
// read or atomic NVS write. The caller starts the radio before requesting RNG.
bool loadOrCreatePassword(char (&out)[PasswordBytes],RandomWord random);
bool regeneratePassword(char (&out)[PasswordBytes],RandomWord random);
}
