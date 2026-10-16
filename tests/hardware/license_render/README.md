# Cardputer license rendering check

This standalone image compiles the production UiModel, UiRenderer, PixelFont,
Shinonome and license document unchanged. The shared scenario in
`tests/license_scenarios.h` also runs in `python3 tests/run_native.py`.

It scrolls the entire document forward and backward, renders every line on the
real LCD, closes/reopens the view 20 times, checks the sleep/wake state
transitions, and redraws the final page 128 times. Simulated GPS/storage content
updates must preserve the reading position. It reports frame times, heap and
stack headroom with `[LICENSE-TEST]` messages.

Build from the repository root with:

```sh
pio run -d tests/hardware/license_render -e cardputer-adv
```

The image is written to `.pio/license-render-build/cardputer-adv/firmware.bin`.
Back up the normal firmware before temporarily writing this image to the app
partition at `0x10000`. Restore that normal image when the check finishes.
The test does not mount SD, write NVS, or start App/GPS/Storage tasks. Its timings
measure LCD drawing; it does not replace full-system concurrency testing.

Capture with the existing serial helper, using the Windows Python environment
when the COM port belongs to Windows:

```sh
python tests/capture_serial.py COM6 --seconds 300 --reset \
  --until '[LICENSE-TEST] PASS' --until '[LICENSE-TEST] FAIL' \
  --output license-render.log
```

A successful run finishes with `[LICENSE-TEST] PASS`. A capture timeout without
that message is incomplete, even when the capture process exits successfully.
