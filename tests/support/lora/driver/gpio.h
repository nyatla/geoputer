#pragma once
using gpio_num_t = int;
constexpr int ESP_OK = 0;
int gpio_hold_en(gpio_num_t pin);
int gpio_hold_dis(gpio_num_t pin);
