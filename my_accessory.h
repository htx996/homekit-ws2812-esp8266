#pragma once
#include <stdbool.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

#ifdef __cplusplus
extern "C" {
#endif

extern homekit_server_config_t hk_config;

extern homekit_characteristic_t cha_on;
extern homekit_characteristic_t cha_brightness;
extern homekit_characteristic_t cha_hue;
extern homekit_characteristic_t cha_saturation;

extern volatile bool  hk_targetOn;
extern volatile float hk_targetBrightness; // 0..100
extern volatile float hk_targetHue;        // 0..360
extern volatile float hk_targetSat;        // 0..100

#ifdef __cplusplus
}
#endif