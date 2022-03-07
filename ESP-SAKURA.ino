// ESP-SAKURA 1.01
// Copyright 2022 taiga

#define MANUFACTURER    "SAKURA"
#define MODEL           "DH1637E"
#define VERSION         "1.01"

#define UART_RX         3
#define UART_TX         1

#define SERIAL_RX       2
#define SERIAL_TX       0
#define SERIAL_HARDWARE 0

#define WIFI_MANAGER    0
#define WIFI_SSID       "wifi"
#define WIFI_PASSWORD   "00000000"
#define WIFI_HOSTNAME   "SAKURA_Unknown"

#define MQTT_SERVER     "127.0.0.1"
#define MQTT_PORT       1883

#define NTP_SERVER      "time.google.com"
#define NTP_TIMEZONE    0

#define HOMEKIT         0

#define BUCKET_COUNT    16

bool forceRestart = false;
bool forceReset = false;
byte state = 0;
char number[128];
char buffer[128];
char status[16][BUCKET_COUNT];
char buckets[16];

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include <WiFiUdp.h>

#if SERIAL_HARDWARE
#define messageSerial Serial1
#define sakuraSerial Serial
#else
#define messageSerial Serial
SoftwareSerial sakuraSerial(SERIAL_RX, SERIAL_TX);
#endif

#include "BasicOTA.h"
#include "MQTT_ESP8266.h"

#if HOMEKIT
#include <arduino_homekit_server.h> // see https://github.com/Mixiaoxiao/Arduino-HomeKit-ESP8266
extern "C" homekit_characteristic_t homekitManufacturer;
extern "C" homekit_characteristic_t homekitModel;
extern "C" homekit_characteristic_t homekitName;
extern "C" homekit_characteristic_t homekitSerialNumber;
extern "C" homekit_characteristic_t homekitFirmwareRevision;
extern "C" homekit_server_config_t homekitConfig;
extern "C" homekit_client_step_t arduino_homekit_get_running_server_step() {
  homekit_server_t *server = arduino_homekit_get_running_server();
  if (server == NULL)
    return HOMEKIT_CLIENT_STEP_NONE;
  client_context_t *context = server->clients;
  if (context == NULL)
    return HOMEKIT_CLIENT_STEP_NONE;
  return context->step;
}
extern "C" int XPGM_PRINTF_P(PGM_P formatP, ...) {
  va_list va;
  va_start(va, formatP);
  vsnprintf_P(number, 128, formatP, va);
  va_end(va);
  MQTTclient.publish(MQTTprefix("HomeKit", "Message", 0), number);
  return 0;
}
extern "C" homekit_characteristic_t homekitTargetTemperature;
extern "C" homekit_characteristic_t homekitInputTemperature;
extern "C" homekit_characteristic_t homekitOutputTemperature;
extern "C" homekit_value_t homekitTargetTemperature_get() {
  return HOMEKIT_FLOAT_CPP(status[4][0] + 34.0f);
}
extern "C" homekit_value_t homekitInputTemperature_get() {
  int total = 0;
  for (int i = 0; i < BUCKET_COUNT; ++i) {
    total += status[7][i];
  }
  return HOMEKIT_FLOAT_CPP(total / float(BUCKET_COUNT));
}
extern "C" homekit_value_t homekitOutputTemperature_get() {
  int total = 0;
  for (int i = 0; i < BUCKET_COUNT; ++i) {
    total += status[8][i];
  }
  return HOMEKIT_FLOAT_CPP(total / float(BUCKET_COUNT));
}
#endif

#if WIFI_MANAGER
#include <WiFiManager.h> // see https://github.com/tzapu/WiFiManager
#undef MQTT_SERVER
#undef MQTT_PORT
char MQTT_SERVER[32] = "127.0.0.1";
word MQTT_PORT = 1883;
#endif

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_TIMEZONE * 3600);

int payload_integer(char* payload, unsigned int length, bool hex = false) {
  memcpy(number, payload, length);
  number[length] = '\0';
  return hex ? strtol(number, 0, 16) : atoi(number);
}

bool payload_compare(const char* payload, const char* compare, unsigned int length) {
  if (length != strlen(compare))
    return false;
  return strncmp(payload, compare, length) == 0;
}

struct PayloadCallback
{
  const char* payload;
  void (*callback)();
};

bool payload_callback(const struct PayloadCallback* callbacks, const char* payload, unsigned int length) {
  while (callbacks && callbacks->payload) {
    if (payload_compare(payload, callbacks->payload, length)) {
      callbacks->callback();
      return true;
    }
    callbacks++;
  }
  return false;
}

void MQTTcallback(char* topic, char* payload, unsigned int length) {
  const char* result = "succeed";
  const PayloadCallback* callbacks = NULL;

  if (strcmp(topic, MQTTprefix("set", "Restart", 0)) == 0) {
    forceRestart = true;
  } else if (strcmp(topic, MQTTprefix("set", "Reset", 0)) == 0) {
    forceReset = true;
#if HOMEKIT
  } else if (strcmp(topic, MQTTprefix("set", "ResetHomeKit", 0)) == 0) {
    homekit_server_reset();
    ESP.reset();
#endif
  } else {
    result = "unknown";
  }

  if (callbacks) {
    if (payload_callback(callbacks, payload, length) == false) {
      result = "failed";
    }
  }

  if (strcmp(topic, MQTTprefix("set", "result", 0)) != 0) {
    MQTTclient.publish(MQTTprefix("set", "result", 0), result);
  }
}

void setup() {

  // MAC
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  char hostname[32] = WIFI_HOSTNAME;
  WiFi.macAddress(mac);
  sprintf(strchr(hostname, '_'), "_%02X%02X%02X", mac[3], mac[4], mac[5]);

  // Serial
  messageSerial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  messageSerial.println();
  messageSerial.printf("%lu:%s starting", millis(), hostname);
  messageSerial.println();

  // WIFI
  WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);
#if WIFI_MANAGER
  // SPIFF
  messageSerial.println("mounting FS...");
  SPIFFS.begin();

  // MQTT
  if (SPIFFS.exists("/mqtt.txt")) {
    File mqttFile = SPIFFS.open("/mqtt.txt", "r");
    if (mqttFile) {
      mqttFile.readBytes(MQTT_SERVER, sizeof(MQTT_SERVER));
      for (int i = 0; i < sizeof(MQTT_SERVER); ++i) {
        if (MQTT_SERVER[i] == ':') {
          MQTT_SERVER[i] = 0;
          MQTT_PORT = atoi(&MQTT_SERVER[i + 1]);
          break;
        }
      }
    }
  }
  char mqtt_port[6];
  sprintf(mqtt_port, "%d", MQTT_PORT);
  WiFiManagerParameter wifi_mqtt_server("server", "mqtt server", MQTT_SERVER, sizeof(MQTT_SERVER));
  WiFiManagerParameter wifi_mqtt_port("port", "mqtt port", mqtt_port, sizeof(mqtt_port));

  static bool shouldSaveConfig = false;
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setSaveConfigCallback([]{ shouldSaveConfig = true; });
  wifiManager.addParameter(&wifi_mqtt_server);
  wifiManager.addParameter(&wifi_mqtt_port);
  if (!wifiManager.autoConnect(hostname, "12345678")) {
    messageSerial.println("failed to connect and hit timeout");
    ESP.reset();
  }
  if (shouldSaveConfig) {
    strcpy(MQTT_SERVER, wifi_mqtt_server.getValue());
    MQTT_PORT = atoi(wifi_mqtt_port.getValue());
    if (MQTT_SERVER[0]) {
      File mqttFile = SPIFFS.open("/mqtt.txt", "w");
      if (mqttFile) {
        mqttFile.printf("%s:%d", MQTT_SERVER, MQTT_PORT);
      }
    }
  }
#else
  WiFi.persistent(false);
  WiFi.disconnect(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  messageSerial.println();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    messageSerial.print(".");
  }
#endif
  WiFi.hostname(hostname);
  messageSerial.println();
  messageSerial.printf(" connected to %s, IP address: %s", WIFI_SSID, WiFi.localIP().toString().c_str());
  messageSerial.println();

  // OTA
  OTAsetup();

  // MQTT
  MQTTclient.setServer(MQTT_SERVER, MQTT_PORT);
  MQTTclient.setCallback((void(*)(char*, byte*, unsigned int))MQTTcallback);
  MQTTreconnect(false);

  // NTP
  timeClient.begin();

  // Serial
#if SERIAL_HARDWARE
  sakuraSerial.begin(9600, SERIAL_8N1, SERIAL_FULL);
#else
  sakuraSerial.begin(9600, SWSERIAL_8N1, SERIAL_RX, SERIAL_TX, false, 128, 1280);
  pinMode(SERIAL_RX, INPUT);
  pinMode(SERIAL_TX, OUTPUT);
#endif

  // Watchdog
  ESP.wdtEnable(2000);
}

void loop() {
  switch (state) {
  case 0: { // Initialize

    forceRestart = false;
    forceReset = false;
    state = 0;
    memset(number, 0, sizeof(number));
    memset(buffer, 0, sizeof(buffer));
    memset(status, 0, sizeof(status));
    memset(buckets, 0, sizeof(buckets));

    MQTTclient.publish(MQTTprefix("ESP", "SketchSize", 0), itoa(ESP.getSketchSize(), number, 10)); 
    MQTTclient.publish(MQTTprefix("ESP", "FreeSketchSpace", 0), itoa(ESP.getFreeSketchSpace(), number, 10));
    MQTTclient.publish(MQTTprefix("ESP", "FlashChipSize", 0), itoa(ESP.getFlashChipSize(), number, 10));
    MQTTclient.publish(MQTTprefix("ESP", "FlashChipRealSize", 0), itoa(ESP.getFlashChipRealSize(), number, 10));
    MQTTclient.publish(MQTTprefix("ESP", "FlashChipSpeed", 0), itoa(ESP.getFlashChipSpeed(), number, 10));
    MQTTclient.publish(MQTTprefix("ESP", "SdkVersion", 0), ESP.getSdkVersion());
    MQTTclient.publish(MQTTprefix("ESP", "FullVersion", 0), ESP.getFullVersion().c_str());
    MQTTclient.publish(MQTTprefix("ESP", "CpuFreq", 0), itoa(ESP.getCpuFreqMHz(), number, 10));
    MQTTclient.publish(MQTTprefix("ESP", "FreeHeap", 0), itoa(ESP.getFreeHeap(), number, 10));
    MQTTclient.publish(MQTTprefix("ESP", "ResetInfo", 0), ESP.getResetInfo().c_str());
    MQTTclient.publish(MQTTprefix("ESP", "ResetReason", 0), ESP.getResetReason().c_str());
    MQTTclient.publish(MQTTprefix("ESP", "Build", 0), __DATE__ " " __TIME__, true);
    MQTTclient.publish(MQTTprefix("ESP", "Version", 0), VERSION, true);

    state = 1;
    break;
  }
  case 1: { // Homekit
#if HOMEKIT
    // HomeKit setup
    char hostname[32];
    strcpy(hostname, WiFi.hostname().c_str());
    homekitName.value = HOMEKIT_STRING_CPP(hostname);
    homekitManufacturer.value = HOMEKIT_STRING_CPP(MANUFACTURER);
    homekitModel.value = HOMEKIT_STRING_CPP(MODEL);
    homekitFirmwareRevision.value = HOMEKIT_STRING_CPP(VERSION);
    arduino_homekit_setup(&homekitConfig);
#endif

    state = 10;
    break;
  }
  case 10: { // Loop

    // Protocol
    if (sakuraSerial.available() > 0) {
      for (int i = 0; i < 128; ++i) {

        // Wait when not responding
        for (int i = 0; i < 100; ++i) {
          if (sakuraSerial.available() > 0)
            break;
          delay(16);
          ESP.wdtFeed();
          ArduinoOTA.handle();
#if HOMEKIT
          arduino_homekit_loop();
#endif
        }

        // Reset when not responding
        if (sakuraSerial.available() <= 0)
          break;
        char c = sakuraSerial.read();
        if (c == '\r' || c == '\n')
          break;
        buffer[i] = c;
        buffer[i + 1] = 0;
        continue;
      }

      if (buffer[0] == ':' && buffer[1] == 'H') {
#if 0
        MQTTclient.publish(MQTTprefix("SAKURA", "Protocol", 0), buffer);
#endif
        int length = strlen(buffer);
        if (length > 2) {
          buffer[length - 2] = 0;

          char* protocol = buffer + 2; 
          char* first = strtok(protocol, ",");
          char* second = strtok(NULL, ",");
          char* third = strtok(NULL, ",");
          char* fourth = strtok(NULL, ",");
          int index = first ? atoi(first) : 0;
          int value = fourth ? atoi(fourth) : 0;
          if (index >= 0 && index < 16) {
            char bucket = buckets[index]++;
            char current = status[index][bucket];

            // Temperature
            if (index == 7 || index == 8) {
              if (current == 0) {
                for (int i = 0; i < BUCKET_COUNT; ++i) {
                  status[index][i] = value;
                }                
              }
              if (buckets[index] >= BUCKET_COUNT) {
                buckets[index] = 0;
              }
            } else {
              buckets[index] = 0; 
            }

            if (current != value) {
              status[index][bucket] = value;

              // Temperature
              if (index == 7 || index == 8) {
                int total = 0;
                for (int i = 0; i < BUCKET_COUNT; ++i) {
                  total += status[index][i];
                }
                sprintf(number, "%.1f", total / float(BUCKET_COUNT));
                fourth = number;
              }

              if (first && fourth) {
                MQTTclient.publish(MQTTprefix("SAKURA", first, 0), fourth);
              }
#if HOMEKIT
              switch (index) {
              case 4:
                homekitTargetTemperature.value = homekitTargetTemperature_get();
                homekit_characteristic_notify(&homekitTargetTemperature, homekitTargetTemperature.value);
                break;
              case 7:
                homekitInputTemperature.value = homekitInputTemperature_get();
                homekit_characteristic_notify(&homekitInputTemperature, homekitInputTemperature.value);
                break;
              case 8:
                homekitOutputTemperature.value = homekitOutputTemperature_get();
                homekit_characteristic_notify(&homekitOutputTemperature, homekitOutputTemperature.value);
                break;
              }
#endif
            }
          }
        }
      }
    }

    // Heap
    static int now_FreeHeap = 0;
    int freeHeap = ESP.getFreeHeap();
    if (abs(now_FreeHeap - freeHeap) >= 1024) {
      now_FreeHeap = freeHeap;
      MQTTclient.publish(MQTTprefix("ESP", "FreeHeap", 0), itoa(now_FreeHeap, number, 10));
    }

#if HOMEKIT
    arduino_homekit_loop();

    // HomeKit mDNS
    static unsigned long mdnsAnnounceMillis = 0;
    if (mdnsAnnounceMillis < millis()) {
      mdnsAnnounceMillis = millis() + 1000 * 60;
      MDNS.announce();
    }

    // HomeKit client
    static int now_HomeKitClient = -1;
    int homekitClient = arduino_homekit_connected_clients_count();
    if (now_HomeKitClient != homekitClient) {
      now_HomeKitClient = homekitClient;
      sprintf(number, "%d", now_HomeKitClient);
      MQTTclient.publish(MQTTprefix("HomeKit", "Client", 0), number);
    }

    // HomeKit step
    static homekit_client_step_t now_HomeKitStep = (homekit_client_step_t)-1;
    homekit_client_step_t homekitStep = arduino_homekit_get_running_server_step();
    if (now_HomeKitStep != homekitStep) {
      now_HomeKitStep = homekitStep;
#if 0
      switch (now_HomeKitStep) {
      case HOMEKIT_CLIENT_STEP_NONE:              MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "None");         break;
      case HOMEKIT_CLIENT_STEP_PAIR_SETUP_1OF3:   MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "Setup (1/3)");  break;
      case HOMEKIT_CLIENT_STEP_PAIR_SETUP_2OF3:   MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "Setup (2/3)");  break;
      case HOMEKIT_CLIENT_STEP_PAIR_SETUP_3OF3:   MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "Setup (3/3)");  break;
      case HOMEKIT_CLIENT_STEP_PAIR_VERIFY_1OF2:  MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "Verify (1/2)"); break;
      case HOMEKIT_CLIENT_STEP_PAIR_VERIFY_2OF2:  MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "Verify (2/2)"); break;
      case HOMEKIT_CLIENT_STEP_END:               MQTTclient.publish(MQTTprefix("HomeKit", "Step", 0), "End");          break;
      }
#endif
      ESP.wdtFeed();
      return;
    }
#endif

    break;
  }
  default:
    break;
  }

  // ForceRestart
  if (forceRestart) {
    state = 0;
  }

  // ForceReset
  if (forceReset) {
    ESP.reset();
  }

  if (!MQTTclient.connected())
    MQTTreconnect(false);
  MQTTclient.loop();
  ArduinoOTA.handle();
  timeClient.update();

  delay(16);
  ESP.wdtFeed();
}
