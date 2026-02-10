#include <Arduino.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "my_accessory.h"

// ========== 对外暴露的目标状态（.ino 会读） ==========
volatile bool  hk_targetOn = false;
volatile float hk_targetBrightness = 100.0f;
volatile float hk_targetHue = 0.0f;
volatile float hk_targetSat = 0.0f;

// ========== Identify ==========
static void on_identify(homekit_value_t _value) {
  (void)_value;
}

// ========== setters（Home app 写入时调用） ==========
static void set_on(const homekit_value_t v) {
  hk_targetOn = v.bool_value;
  cha_on.value.bool_value = v.bool_value;
}

static void set_brightness(const homekit_value_t v) {
  int b = v.int_value;
  if (b < 0) b = 0;
  if (b > 100) b = 100;
  hk_targetBrightness = (float)b;
  cha_brightness.value.int_value = b;
}

static void set_hue(const homekit_value_t v) {
  float h = v.float_value;
  if (h < 0) h = 0;
  if (h > 360) h = 360;
  hk_targetHue = h;
  cha_hue.value.float_value = h;
}

static void set_saturation(const homekit_value_t v) {
  float s = v.float_value;
  if (s < 0) s = 0;
  if (s > 100) s = 100;
  hk_targetSat = s;
  cha_saturation.value.float_value = s;
}

// ========== 手写 characteristic（不用宏） ==========
// 注意：字符串必须是字面量，不能用变量指针
homekit_characteristic_t cha_on = {
  .type = HOMEKIT_CHARACTERISTIC_ON,
  .description = "On",
  .format = homekit_format_bool,
  .permissions = homekit_permissions_paired_read | homekit_permissions_paired_write | homekit_permissions_notify,
  .value = HOMEKIT_BOOL(false),
  .setter = set_on,
};

homekit_characteristic_t cha_brightness = {
  .type = HOMEKIT_CHARACTERISTIC_BRIGHTNESS,
  .description = "Brightness",
  .format = homekit_format_int,
  .permissions = homekit_permissions_paired_read | homekit_permissions_paired_write | homekit_permissions_notify,
  .value = HOMEKIT_INT(100),
  .min_value = (float[]) {0},
  .max_value = (float[]) {100},
  .min_step  = (float[]) {1},
  .setter = set_brightness,
};

homekit_characteristic_t cha_hue = {
  .type = HOMEKIT_CHARACTERISTIC_HUE,
  .description = "Hue",
  .format = homekit_format_float,
  .permissions = homekit_permissions_paired_read | homekit_permissions_paired_write | homekit_permissions_notify,
  .value = HOMEKIT_FLOAT(0),
  .min_value = (float[]) {0},
  .max_value = (float[]) {360},
  .min_step  = (float[]) {1},
  .setter = set_hue,
};

homekit_characteristic_t cha_saturation = {
  .type = HOMEKIT_CHARACTERISTIC_SATURATION,
  .description = "Saturation",
  .format = homekit_format_float,
  .permissions = homekit_permissions_paired_read | homekit_permissions_paired_write | homekit_permissions_notify,
  .value = HOMEKIT_FLOAT(0),
  .min_value = (float[]) {0},
  .max_value = (float[]) {100},
  .min_step  = (float[]) {1},
  .setter = set_saturation,
};

static homekit_characteristic_t cha_name = {
  .type = HOMEKIT_CHARACTERISTIC_NAME,
  .description = "Name",
  .format = homekit_format_string,
  .permissions = homekit_permissions_paired_read,
  .value = HOMEKIT_STRING("HomeKit 灯带"),
};

// Accessory info characteristics
static homekit_characteristic_t cha_manu = {
  .type = HOMEKIT_CHARACTERISTIC_MANUFACTURER,
  .description = "Manufacturer",
  .format = homekit_format_string,
  .permissions = homekit_permissions_paired_read,
  .value = HOMEKIT_STRING("RGB"),
};
static homekit_characteristic_t cha_sn = {
  .type = HOMEKIT_CHARACTERISTIC_SERIAL_NUMBER,
  .description = "Serial Number",
  .format = homekit_format_string,
  .permissions = homekit_permissions_paired_read,
  .value = HOMEKIT_STRING("HKLED-001"),
};
static homekit_characteristic_t cha_model = {
  .type = HOMEKIT_CHARACTERISTIC_MODEL,
  .description = "Model",
  .format = homekit_format_string,
  .permissions = homekit_permissions_paired_read,
  .value = HOMEKIT_STRING("WS2812"),
};
static homekit_characteristic_t cha_fw = {
  .type = HOMEKIT_CHARACTERISTIC_FIRMWARE_REVISION,
  .description = "Firmware Revision",
  .format = homekit_format_string,
  .permissions = homekit_permissions_paired_read,
  .value = HOMEKIT_STRING("1.0.0"),
};
static homekit_characteristic_t cha_acc_name = {
  .type = HOMEKIT_CHARACTERISTIC_NAME,
  .description = "Name",
  .format = homekit_format_string,
  .permissions = homekit_permissions_paired_read,
  .value = HOMEKIT_STRING("RGB"),
};
static homekit_characteristic_t cha_ident = {
  .type = HOMEKIT_CHARACTERISTIC_IDENTIFY,
  .description = "Identify",
  .format = homekit_format_bool,
  .permissions = homekit_permissions_paired_write,
  .value = HOMEKIT_BOOL(false),
  .setter = 0,
  .callback = on_identify,
};

// ========== services（静态数组，不用临时数组） ==========
static homekit_characteristic_t* info_chars[] = {
  &cha_acc_name,
  &cha_manu,
  &cha_sn,
  &cha_model,
  &cha_fw,
  &cha_ident,
  NULL
};

static homekit_service_t service_info = {
  .type = HOMEKIT_SERVICE_ACCESSORY_INFORMATION,
  .characteristics = info_chars
};

static homekit_characteristic_t* light_chars[] = {
  &cha_on,
  &cha_brightness,
  &cha_hue,
  &cha_saturation,
  &cha_name,
  NULL
};

static homekit_service_t service_light = {
  .type = HOMEKIT_SERVICE_LIGHTBULB,
  .primary = true,
  .characteristics = light_chars
};

static homekit_service_t* services[] = {
  &service_info,
  &service_light,
  NULL
};

// ========== accessory（静态对象） ==========
static homekit_accessory_t accessory = {
  .id = 1,
  .category = homekit_accessory_category_lightbulb,
  .services = services
};

static homekit_accessory_t* accessories[] = {
  &accessory,
  NULL
};

// ========== server config ==========
homekit_server_config_t hk_config = {
  .accessories = accessories,
  .password = "111-22-333",   // 11122333
};