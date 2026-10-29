// Compile/link smoke test only. No SD peripheral is accessed.
#include <Arduino.h>
#ifdef SD_FIRST
#include <SD_FatFs.h>
#include <ff.h>
#else
#include <ff.h>
#include <SD_FatFs.h>
#endif
static_assert(FF_MAX_SS == 4096, "Expected the bundled SDK FatFs configuration");
static_assert(SD_FATFS_FF_MAX_SS == 512, "SD-only sector size");
static_assert(sizeof(FIL) == 4136, "SDK FIL ABI");
static_assert(sizeof(SD_FATFS_FIL) == 552, "Dedicated FIL ABI");
static_assert(sizeof(((FATFS*)nullptr)->win) == 4096, "SDK window");
static_assert(sizeof(((SD_FATFS_FATFS*)nullptr)->win) == 512, "SD window");
void setup() {
    // Force both independent cores, Unicode modules and disk glue to link.
    FIL sdk_file{};
    SD_FATFS_FIL sd_file{};
    UINT sdk_read = 0;
    SD_FATFS_UINT sd_read = 0;
    char value = 0;
    f_open(&sdk_file, "0:/link-test", FA_READ);
    f_read(&sdk_file, &value, 1, &sdk_read);
    sd_fatfs_f_open(&sd_file, "0:/link-test", SD_FATFS_FA_READ);
    sd_fatfs_f_read(&sd_file, &value, 1, &sd_read);
    sd_fatfs_f_mount(nullptr, "0:", 0);
}
void loop() { delay(1000); }
