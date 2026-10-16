"""Capture real firmware diagnostics using pyserial; optionally reset with esptool."""
import argparse
from pathlib import Path
import sys
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("port")
parser.add_argument("--seconds", type=float, default=30)
parser.add_argument("--reset", action="store_true")
parser.add_argument("--output", type=Path)
parser.add_argument("--until", action="append", default=[], help="Stop after any matching text is received (repeatable)")
args = parser.parse_args()
if args.seconds <= 0:
    parser.error("--seconds must be positive")

device = serial.Serial()
device.port = args.port
device.baudrate = 115200
device.timeout = 0.2
device.dtr = False
device.rts = False
device.open()
captured = bytearray()
disconnected = False
try:
    if args.reset:
        from esptool.reset import HardReset
        HardReset(device, uses_usb=True)()
    device.dtr = True  # ESP32-S3 USB CDC output requires the host to assert DTR.
    device.rts = False
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        try:
            chunk = device.read(8192)
        except serial.SerialException as error:
            print(f"[CAPTURE] serial connection closed: {error}", file=sys.stderr)
            disconnected = True
            break
        if chunk:
            captured.extend(chunk)
            print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
            if any(marker.encode("utf-8") in captured for marker in args.until):
                break
finally:
    device.close()
    if args.output:
        args.output.write_bytes(captured)
if disconnected:
    raise SystemExit(1)
