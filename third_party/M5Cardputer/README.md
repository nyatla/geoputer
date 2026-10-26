# M5Cardputer 1.1.1 — ReverseGeoputer local copy

Source: M5Stack's M5Cardputer 1.1.1, copied from the pinned PlatformIO package.
Upstream: https://github.com/m5stack/M5Cardputer/tree/1.1.1

Only `src` and the library manifest are vendored. Original copyright/SPDX notices
are retained; source line endings are normalized to LF. `LICENSE` is the existing
M5Cardputer MIT notice; the embedded Adafruit TCA8418 driver uses
`LICENSE.Adafruit`. PlatformIO selects this copy through the `symlink://`
dependency in the root `platformio.ini`; edits in `.pio/libdeps` are unnecessary.
M5Unified, M5GFX and IRremote remain separately pinned dependencies. The vendored
manifest pins the same versions as the application, preventing wildcard
dependencies from installing additional versions.

## Fixed keyboard capacity

`Keyboard_Class(max_keys)` and `KeyboardReader(max_keys)` fix their limits at
construction and reserve all key vectors immediately. The default and physical
maximum are 56; larger requests are clamped to 56, and zero disables key input.
No setter is provided. Fn, Shift and other modifiers count toward the same limit.
The standard global `M5Cardputer.Keyboard` uses the default of 56.

`maxKeys()` exposes the immutable limit. The built-in matrix and TCA8418 readers
inherit it; an injected reader may have the same or a smaller limit. An injected
reader with a larger limit is rejected before initialization and leaves the
existing reader unchanged. The app's ADV adapter is constructed with
`M5Cardputer.Keyboard.maxKeys()`.

`KeyboardReader::setKey()` is the common checked insertion/removal path. Invalid
coordinates and duplicate presses are ignored. At capacity, additional presses
are discarded; releases continue to work. Event-driven readers do not queue
ignored presses for later admission. A new press or a physical recovery snapshot
is required. Matrix readers rebuild the bounded list on each scan; recovery
snapshots use scan order when more keys are physically held than the limit.

`Keyboard_Class::updateKeysState()` clears and reuses the reserved containers;
it no longer reserves based on the current key count. The keyboard list/state
paths make no C++ allocations or frees after initialization. This is not a claim
that all underlying M5Unified/I2C/display internals are heap-free.

The changed upstream files are `Keyboard.h/.cpp`,
`KeyboardReader/KeyboardReader.h`, `KeyboardReader/IOMatrix.h/.cpp` and
`KeyboardReader/TCA8418.h/.cpp`. Hardware setup and key mapping are retained.
The app's ADV adapter uses the common checked insertion/removal helper.

Run `python3 tests/run_keyboard.py` from the repository root. The tests compile
this copy's actual keyboard/readers against fake GPIO/I2C and check allocation
counts, bounds, modifiers, releases, snapshots and warm-reset detection.

The unused private key-position vectors are removed. Public key/state lists and
the two-pass modifier handling are unchanged; only observed lists are reserved.
