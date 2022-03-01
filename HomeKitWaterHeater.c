#define HOMEKIT 0
#if HOMEKIT

#include <Arduino.h>
#include <homekit/types.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <stdio.h>
#include <port.h>

#define HOMEKIT_NAME          ("ESP8266")
#define HOMEKIT_SERIAL_NUMBER ("123-45-678")
#define HOMEKIT_MANUFACTURER  ("Arduino")
#define HOMEKIT_MODEL         ("ESP8266")
#define HOMEKIT_REVISION      ("1.00")

void homekitIdentify_set(homekit_value_t _value) {}
homekit_characteristic_t homekitManufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER, HOMEKIT_MANUFACTURER);
homekit_characteristic_t homekitModel = HOMEKIT_CHARACTERISTIC_(MODEL, HOMEKIT_MODEL);
homekit_characteristic_t homekitName = HOMEKIT_CHARACTERISTIC_(NAME, HOMEKIT_NAME);
homekit_characteristic_t homekitSerialNumber = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, HOMEKIT_SERIAL_NUMBER);
homekit_characteristic_t homekitFirmwareRevision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, HOMEKIT_REVISION);

homekit_value_t homekitTargetTemperature_get();
homekit_value_t homekitInputTemperature_get();
homekit_value_t homekitOutputTemperature_get();
homekit_characteristic_t homekitTargetTemperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0, .getter = homekitTargetTemperature_get);
homekit_characteristic_t homekitInputTemperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0, .getter = homekitInputTemperature_get);
homekit_characteristic_t homekitOutputTemperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0, .getter = homekitOutputTemperature_get);

homekit_accessory_t *accessories[] = {
  HOMEKIT_ACCESSORY(
    .id = 1,
    .category = homekit_accessory_category_sensor,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(IDENTIFY, homekitIdentify_set),
          &homekitManufacturer,
          &homekitModel,
          &homekitName,
          &homekitSerialNumber,
          &homekitFirmwareRevision,
          NULL
        }
      ),
      HOMEKIT_SERVICE(TEMPERATURE_SENSOR,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Target"),
          &homekitTargetTemperature,
          NULL
        }
      ),
      HOMEKIT_SERVICE(TEMPERATURE_SENSOR,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Input"),
          &homekitInputTemperature,
          NULL
        }
      ),
      HOMEKIT_SERVICE(TEMPERATURE_SENSOR,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Output"),
          &homekitOutputTemperature,
          NULL
        }
      ),
      NULL
    }),
  NULL
};
homekit_server_config_t homekitConfig = { .accessories = accessories, .password = "123-45-678", .setupId = "XYZW" };

#endif
