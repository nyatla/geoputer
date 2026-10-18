#pragma once
namespace geoputer {
struct UsbStorageView {
    bool ready = false, ejected = false, io_error = false;
    bool connected = false, local = false;
};
inline bool operator==(const UsbStorageView& a, const UsbStorageView& b) {
    return a.ready == b.ready && a.ejected == b.ejected && a.io_error == b.io_error &&
           a.connected == b.connected && a.local == b.local;
}
inline bool usbHostNeedsEject(const UsbStorageView& view) { return view.ready && view.connected && !view.ejected && !view.local; }
}
