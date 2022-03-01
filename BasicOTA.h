#pragma once

#include <ArduinoOTA.h>

// https://github.com/esp8266/Arduino/blob/master/libraries/ArduinoOTA/examples/BasicOTA/BasicOTA.ino
void OTAsetup() {
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(WiFi.hostname().c_str());
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    messageSerial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    messageSerial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    messageSerial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    messageSerial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      messageSerial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      messageSerial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      messageSerial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      messageSerial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      messageSerial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  messageSerial.println("Ready");
  messageSerial.print("IP address: ");
  messageSerial.println(WiFi.localIP());
}
