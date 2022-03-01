#pragma once

#include <ESP8266WiFi.h>
#include <PubSubClient.h> // see https://github.com/knolleary/pubsubclient

WiFiClient espClient;
PubSubClient MQTTclient(espClient);

#if MQTT_MAX_TRANSFER_SIZE < 512
#error "Need to define MQTT_MAX_TRANSFER_SIZE 512 in PubSubClient.h"
#endif

const char* MQTTprefix(const char* prefix, ...) {
  static char path[128];
  char* pointer = path;
  pointer += sprintf(pointer, "%s/%s", WiFi.hostname().c_str(), prefix);

  va_list args;
  va_start(args, prefix);
  while (const char* name = va_arg(args, char*)) {
    pointer += sprintf(pointer, "/%s", name);
  }
  va_end(args);

  return path;
}

// https://github.com/knolleary/pubsubclient/blob/master/examples/mqtt_esp8266/mqtt_esp8266.ino
void MQTTreconnect(bool wait) {
  // Loop until we're reconnected
  while (!MQTTclient.connected()) {
    messageSerial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (MQTTclient.connect(WiFi.hostname().c_str(), MQTTprefix("connected", 0), 0, true, "false")) {
      messageSerial.println("connected");
      MQTTclient.publish(MQTTprefix("connected", 0), "true", true);
      MQTTclient.publish(MQTTprefix("ESP", "IP", 0), WiFi.localIP().toString().c_str());
      MQTTclient.publish(MQTTprefix("ESP", "RSSI", 0), String(WiFi.RSSI()).c_str());
      MQTTclient.subscribe(MQTTprefix("set", "#", 0));
    }
    else {
      messageSerial.print("failed, rc=");
      messageSerial.print(MQTTclient.state());
      if (wait == false) {
        messageSerial.println();
        break;
      }
      messageSerial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      for (int i = 0; i < 5; ++i) {
        delay(1000);
        ArduinoOTA.handle();
      }
    }
  }
}
