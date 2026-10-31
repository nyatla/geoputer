#include "license_scenarios.h"

void testLicenses() {
    geoputer::GpsUiModel ui;
    geoputer::test::exerciseLicenses(ui, [](const auto&) {});
}
