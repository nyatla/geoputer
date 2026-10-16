#include <Arduino.h>
#include "Application.h"

namespace {
geoputer::Application application;
bool ready = false;
}

void setup() {
    Serial.begin(115200);
    ready = application.begin();
    Serial.println(ready ? "[APP] UI, GPS and Storage started" : "[APP] initialization failed");
}

void loop() {
    if (ready) application.runOnce();
    else vTaskDelay(pdMS_TO_TICKS(1000));
}
