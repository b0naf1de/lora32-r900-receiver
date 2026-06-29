/*
  LilyGO LoRa32 V2.1.6 (ESP32-PICO-D4 + SX1276) — Neptune R900 receiver, Phase 1 bring-up.

  Goal of this firmware: prove the SX1276 inits on this exact board and that bursts are
  reaching the pulse pipeline near the 911-917 MHz meter band. Decode is NOT expected yet —
  the two source patches (MINIMUM_PULSE_LENGTH, RSSI-interrupt) are not applied here.

  Based on rtl_433_ESP/example/OOK_Receiver. The callback just prints the raw JSON message
  (avoids the ArduinoJson v5/v6 API mismatch in the upstream example), and loop() emits a
  periodic getStatus() heartbeat so a passive observer can tell "alive but quiet" from "stuck".
*/

#include <Arduino.h>
#include <ArduinoLog.h>
#include <rtl_433_ESP.h>

#ifndef RF_MODULE_FREQUENCY
#  define RF_MODULE_FREQUENCY 433.92
#endif

#define JSON_MSG_BUFFER 512

char messageBuffer[JSON_MSG_BUFFER];

rtl_433_ESP rf; // use -1 to disable transmitter

int count = 0;

void rtl_433_Callback(char* message) {
  Log.notice(F("Received message : %s" CR), message);
  count++;
}

static unsigned long nextHeartbeat = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
#ifndef LOG_LEVEL
#  define LOG_LEVEL LOG_LEVEL_VERBOSE
#endif
  Log.begin(LOG_LEVEL, &Serial);
  Log.notice(F(" " CR));
  Log.notice(F("****** LoRa32 R900 Phase-1 bring-up ******" CR));
  Log.notice(F("Frequency: %F MHz" CR), (double)RF_MODULE_FREQUENCY);

  rf.initReceiver(RF_MODULE_RECEIVER_GPIO, RF_MODULE_FREQUENCY);
  rf.setCallback(rtl_433_Callback, messageBuffer, JSON_MSG_BUFFER);
  rf.enableReceiver();
  Log.notice(F("****** setup complete ******" CR));
  rf.getModuleStatus();
}

void loop() {
  rf.loop();

  // ~30 s heartbeat: rich status dump + decoded-message count since boot.
  if (millis() > nextHeartbeat) {
    nextHeartbeat = millis() + 30000;
    Log.notice(F("--- heartbeat: %d msgs decoded since boot ---" CR), count);
    rf.getStatus();
  }
}
