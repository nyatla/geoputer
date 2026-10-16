#include "license_scenarios.h"

void testLicenses() {
    geoputer::UiModel ui;
    geoputer::test::exerciseLicenses(ui, [](const auto&) {});
}
