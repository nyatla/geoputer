#pragma once
#include "../Config.h"
#include "LoraSafety.h"
#include "../utils/Console.h"
#include <Arduino.h>
#include <SPI.h>
namespace geoputer {
inline void initializeCardSpi(SPIClass& spi){digitalWrite(config::LoraSelectPin,HIGH);pinMode(config::LoraSelectPin,OUTPUT);pinMode(12,OUTPUT);digitalWrite(12,HIGH);spi.begin(40,39,14,12);uint8_t status=0xff;const bool asleep=sleepUnusedLora(spi,&status);Console.printf("[LORA] %s status=0x%02X (TX/RX disabled)\n",asleep?"SLEEP":"RESET HELD",status);}
}
