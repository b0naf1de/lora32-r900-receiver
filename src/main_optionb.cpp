// SPDX-License-Identifier: GPL-2.0-or-later
/*
  lora32-r900-receiver — standalone Neptune R900 water-meter reader for the LilyGO LoRa32 (SX1276).
  Copyright (C) 2026  Eric Thomas

  The on-MCU R900 frame decoder (r900_decode / map16to6 / base-6 unpack, below) is a C port of
  rtl_433's devices/neptune_r900.c (Copyright (C) 2022 Jeffrey S. Ruby), which is GPL-2.0-or-later;
  this program therefore is licensed GPL-2.0-or-later. See LICENSE and README (Credits).

  ---
  SX1276 RSSI-envelope capture (bypasses the hardware OOK slicer).

  The hardware OOK data slicer on the SX127x splits R900's sustained 2x/3x pulses (proven across
  PEAK/AVERAGE/FIXED thresholds, AGC on/off, narrow/wide RxBw — the demod envelope AC-couples).
  RegRssiValue (0x11) is the RAW received signal strength, upstream of that slicer, so a sustained
  carrier reads continuously high. This firmware tunes one R900 channel in OOK continuous mode and,
  on burst onset, captures RegRssiValue as fast as possible into RAM, then dumps it to serial as hex.

  Desktop side (optionb_decode.py): threshold the magnitude trace -> pulse durations -> .ook -> rtl_433.

  Make-or-break unknown this answers: does RegRssiValue update fast enough (us/sample) to resolve a
  ~30.5 us R900 chip, and does the magnitude trace show the clean 1x/2x/3x structure the OOK slicer
  destroyed? Build/flash: pio run -e optionb -t upload
*/
#ifdef OPTIONB
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>   // RADIOLIB_LOW_LEVEL=1 (build flag) exposes getMod() for direct register access
#include <WiFi.h>
#include <PubSubClient.h>
#include <U8g2lib.h>     // SSD1306 OLED (LoRa32 V2.1.6: I2C SDA=21 SCL=22 RST=16)
#include <Wire.h>
#include "secrets.h"

// ttgo-lora32-v21 SX1276 pins: NSS=18 DIO0=26 RST=23 DIO1=33 ; SPI SCK=5 MISO=19 MOSI=27
SX1276 radio = new Module(18, 26, 23, 33);

#define RSSI_REG 0x11      // RADIOLIB_SX127X_REG_RSSI_VALUE_FSK ; dBm = -val/2
#define CS_PIN   18        // SX1276 NSS
#define CAP_N    15000     // ~59ms at 3.93us/sample -> covers a full R900 burst (~50-60ms)
static uint8_t cap[CAP_N];           // raw RSSI ring buffer (capture order)
static uint8_t lin[CAP_N];           // ring linearised to chronological order + median-filtered
static char    hexbuf[CAP_N * 2 + 1];

// ---- on-MCU R900 decode (faithful port of rtl_433 devices/neptune_r900.c) ----
#define BIT_US 30.5176f              // R900 chip = 1/32768 s
static const int8_t map16to6[16] = { -1,-1,-1,0,-1,1,2,-1,-1,5,4,-1,3,-1,-1,-1 };
static uint8_t gbits[2100];          // recovered bitstream (1 = carrier on)
static int     gnbits = 0;

struct R900 { uint32_t id, consumption; int backflow, leak, leaknow; };

// Majority-vote each bit over the central 50% of its window (robust to edge bleed / shallow depth).
static void recover(int thresh, float spb, float phase) {
  float guard = spb * 0.25f;
  gnbits = 0;
  for (int k = 0; ; k++) {
    int ia = (int)(phase + k * spb + guard);
    int ib = (int)(phase + (k + 1) * spb - guard);
    if (ib >= CAP_N) break;
    int on = 0, tot = 0;
    for (int i = ia; i <= ib; i++) { tot++; if (lin[i] < thresh) on++; }
    gbits[gnbits++] = (on * 2 >= tot) ? 1 : 0;
    if (gnbits >= (int)sizeof(gbits) - 4) break;
  }
}

static inline uint8_t getbyte(int k) { uint8_t v = 0; for (int j = 0; j < 8; j++) v = (v << 1) | gbits[k + j]; return v; }

// Search recovered bits for the (1-bit-shifted) preamble+sync, validate base-6 payload, extract fields.
// Ported from rtl_433 devices/neptune_r900.c (C) 2022 Jeffrey S. Ruby, GPL-2.0-or-later.
static bool r900_decode(R900 *out) {
  static const uint8_t pre[7] = {0x55, 0x55, 0x55, 0xa9, 0x66, 0x69, 0x65}; // 56 bits
  for (int start = 0; start + 56 + 168 <= gnbits; start++) {
    bool ok = true;
    for (int bi = 0; bi < 56 && ok; bi++)
      if (gbits[start + bi] != ((pre[bi >> 3] >> (7 - (bi & 7))) & 1)) ok = false;
    if (!ok) continue;
    int base = start + 56;
    int8_t base6[21]; bool valid = true;
    for (int c = 0; c < 21; c++) {
      uint8_t byte = getbyte(base + c * 8);
      int hi = map16to6[(byte >> 4) & 0xF], lo = map16to6[byte & 0xF];
      if (hi < 0 || lo < 0) { valid = false; break; }
      base6[c] = 6 * hi + lo;
    }
    if (!valid) continue;                       // strong implicit integrity check
    uint8_t pay[110]; int pn = 0;               // base6 -> 5 bits each (MSB first) = 105 bits
    for (int c = 0; c < 21; c++) { uint8_t d = base6[c];
      pay[pn++] = (d >> 4) & 1; pay[pn++] = (d >> 3) & 1; pay[pn++] = (d >> 2) & 1;
      pay[pn++] = (d >> 1) & 1; pay[pn++] = d & 1; }
    uint8_t b[13];                              // first 104 bits -> 13 bytes
    for (int i = 0; i < 13; i++) { uint8_t v = 0; for (int j = 0; j < 8; j++) v = (v << 1) | pay[i * 8 + j]; b[i] = v; }
    out->id          = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    out->consumption = (b[6] << 16) | (b[7] << 8) | b[8];
    out->backflow    = b[5] & 0x03;
    out->leak        = ((b[9] >> 1) & 0x0F) >> 1;
    out->leaknow     = b[9] & 0x03;
    return true;
  }
  return false;
}

// ---- WiFi + MQTT (Home Assistant auto-discovery) ----
// Publishes each decoded meter as a SEPARATE HA device "Neptune R900 (LoRa32) <id>" so it never
// collides with the production rtlamr2mqtt entity for the same meter. Non-blocking: never stalls RF.
#define AVAIL_TOPIC "lora32r900/availability"   // retained: only ever holds the latest (no log bloat)
static WiFiClient   espClient;
static PubSubClient mqtt(espClient);
static uint32_t     seenIds[8]; static int nSeen = 0;   // ids that already got a discovery message
static uint32_t     lastBeat = 0;                       // last heartbeat publish (ms)

// HA connectivity binary_sensor for the board itself (fed by LWT/birth/heartbeat on AVAIL_TOPIC).
static void publishReaderDiscovery() {
  char p[400];
  snprintf(p, sizeof(p),
    "{\"name\":\"Reader Online\",\"obj_id\":\"lora32r900_reader_online\",\"uniq_id\":\"lora32r900_reader_online\","
    "\"stat_t\":\"" AVAIL_TOPIC "\",\"dev_cla\":\"connectivity\",\"pl_on\":\"online\",\"pl_off\":\"offline\","
    "\"dev\":{\"ids\":[\"lora32r900_reader\"],\"name\":\"R900 LoRa32 Reader\",\"mf\":\"LilyGO\",\"mdl\":\"LoRa32 SX1276\"}}");
  mqtt.publish("homeassistant/binary_sensor/lora32r900_reader_online/config", p, true);
}

static void mqttEnsure() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  String cid = "lora32r900-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  // LWT: if we drop without a clean disconnect, the broker retains "offline" on AVAIL_TOPIC.
  if (mqtt.connect(cid.c_str(), AIO_USERNAME, AIO_KEY, AVAIL_TOPIC, 0, true, "offline")) {
    mqtt.publish(AVAIL_TOPIC, "online", true);   // birth (retained, overwrites the previous value)
    lastBeat = millis();
    publishReaderDiscovery();
  }
}

static void publishDiscovery(uint32_t id) {
  char t[112], p[560];
  snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%u_consumption/config", id);
  snprintf(p, sizeof(p),
    "{\"name\":\"Consumption\",\"uniq_id\":\"lora32r900_%u_consumption\","
    "\"stat_t\":\"lora32r900/%u/state\",\"avty_t\":\"" AVAIL_TOPIC "\","
    "\"unit_of_meas\":\"gal\",\"dev_cla\":\"water\","
    "\"stat_cla\":\"total_increasing\",\"sug_dsp_prc\":1,"
    "\"val_tpl\":\"{{ (value_json.consumption | int) / 10 }}\","  // R900 reports 1/10 gal
    "\"dev\":{\"ids\":[\"lora32r900_%u\"],\"name\":\"Neptune R900 (LoRa32) %u\",\"mf\":\"Neptune\",\"mdl\":\"R900\"}}",
    id, id, id, id);
  mqtt.publish(t, p, true);
  snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%u_leaknow/config", id);
  snprintf(p, sizeof(p),
    "{\"name\":\"Leak Now\",\"uniq_id\":\"lora32r900_%u_leaknow\",\"stat_t\":\"lora32r900/%u/state\","
    "\"avty_t\":\"" AVAIL_TOPIC "\","
    "\"val_tpl\":\"{{ value_json.leaknow }}\",\"dev\":{\"ids\":[\"lora32r900_%u\"]}}", id, id, id);
  mqtt.publish(t, p, true);
  snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%u_rssi/config", id);
  snprintf(p, sizeof(p),
    "{\"name\":\"RSSI\",\"uniq_id\":\"lora32r900_%u_rssi\",\"stat_t\":\"lora32r900/%u/state\","
    "\"avty_t\":\"" AVAIL_TOPIC "\","
    "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\","
    "\"val_tpl\":\"{{ value_json.rssi }}\",\"dev\":{\"ids\":[\"lora32r900_%u\"]}}", id, id, id);
  mqtt.publish(t, p, true);
}

static void publishReading(const R900 &r, int rssiVal) {
  mqttEnsure();
  if (!mqtt.connected()) { Serial.println("  (mqtt offline — serial only)"); return; }
  bool known = false; for (int i = 0; i < nSeen; i++) if (seenIds[i] == r.id) known = true;
  if (!known && nSeen < 8) { publishDiscovery(r.id); seenIds[nSeen++] = r.id; }
  char t[64], p[200];
  snprintf(t, sizeof(t), "lora32r900/%u/state", r.id);
  snprintf(p, sizeof(p),
    "{\"id\":%u,\"consumption\":%u,\"backflow\":%d,\"leak\":%d,\"leaknow\":%d,\"rssi\":%d}",
    r.id, r.consumption, r.backflow, r.leak, r.leaknow, -rssiVal / 2);
  mqtt.publish(t, p, true);
  Serial.printf("  -> MQTT published (%s)\n", t);
}

// ---- onboard SSD1306 OLED status display ----
// reset = U8X8_PIN_NONE: do NOT drive GPIO16 (faults on the ESP32-PICO-D4 -> boot-loop). The panel
// powers up reset-free and ACKs on I2C, so no reset pulse is needed.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*rst*/ U8X8_PIN_NONE, /*scl*/ 22, /*sda*/ 21);
#ifndef MY_METER_ID
#define MY_METER_ID   0u              // the meter the OLED tracks — set yours in secrets.h
#endif
#define RX_ACTIVE_MS  600000UL        // "receiving" indicator on if any decode within 10 min
static bool     oledPresent = false;  // set true only if a panel ACKs at 0x3C (else run headless)
static R900     myReading;            // last reading of MY_METER_ID
static bool     haveMy   = false;
static uint32_t myMs     = 0;         // millis() of MY_METER_ID's last decode
static uint32_t lastAnyMs = 0;        // millis() of ANY meter's last decode (RX-active indicator)

static const char *leakText(int l) {
  static const char *t[] = {"none", "1-2 days", "3-7 days", "8-14 days", "15-21 days", "22-34 days", "35+ days"};
  return (l >= 0 && l <= 6) ? t[l] : "?";
}
static const char *leaknowText(int n) { return n == 0 ? "None" : n == 1 ? "Intermittent" : n == 2 ? "Continuous" : "?"; }

// small WiFi glyph (stacked top-arcs) at left x; filled arcs = connected, dot+slash = not
static void drawWifi(int x, bool on) {
  if (on) {
    u8g2.drawCircle(x + 6, 10, 7, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    u8g2.drawCircle(x + 6, 10, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    u8g2.drawDisc(x + 6, 10, 1);
  } else {
    u8g2.drawDisc(x + 6, 10, 1);
    u8g2.drawLine(x + 1, 2, x + 11, 10);   // slash = offline
  }
}
// a small labelled status box: filled = on, hollow = off
static void drawFlag(int x, const char *label, bool on) {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(x, 9, label);
  int bx = x + (int)strlen(label) * 5 + 2;
  if (on) u8g2.drawBox(bx, 3, 7, 7); else u8g2.drawFrame(bx, 3, 7, 7);
}

static void drawDisplay() {
  if (!oledPresent) return;
  bool wifi = (WiFi.status() == WL_CONNECTED);
  bool mq   = mqtt.connected();
  bool rx   = (lastAnyMs != 0) && (millis() - lastAnyMs < RX_ACTIVE_MS);
  u8g2.clearBuffer();
  // top status bar: WiFi icon + MQTT + RX flags
  drawWifi(0, wifi);
  drawFlag(20, "MQTT", mq);
  drawFlag(78, "RX", rx);
  u8g2.drawHLine(0, 13, 128);
  // body
  u8g2.setFont(u8g2_font_6x12_tf);
  if (!haveMy) {
    u8g2.drawStr(0, 30, "Waiting for meter");
    char m[24]; snprintf(m, sizeof(m), "%u ...", (unsigned)MY_METER_ID);
    u8g2.drawStr(0, 44, m);
  } else {
    char l[24];
    snprintf(l, sizeof(l), "Meter %u", (unsigned)myReading.id);   u8g2.drawStr(0, 26, l);
    snprintf(l, sizeof(l), "Leak: %s", leakText(myReading.leak)); u8g2.drawStr(0, 38, l);
    snprintf(l, sizeof(l), "Now: %s", leaknowText(myReading.leaknow)); u8g2.drawStr(0, 50, l);
    uint32_t ago = (millis() - myMs) / 1000;
    if (ago < 60) snprintf(l, sizeof(l), "Updated %lus ago", (unsigned long)ago);
    else          snprintf(l, sizeof(l), "Updated %lum %lus ago", (unsigned long)(ago / 60), (unsigned long)(ago % 60));
    u8g2.drawStr(0, 62, l);
  }
  u8g2.sendBuffer();
}

static int noiseVal = 184;
static int trigVal  = 170;
static SPISettings spiSet(8000000, MSBFIRST, SPI_MODE0); // SX1276 max ~10MHz

// RadioLib's SPIgetRegValue measured ~45us/sample (per-call txn + masking overhead) — far too slow
// for R900's 30us chip. Raw SPI with CS via direct GPIO register is ~10x faster. Caller wraps the
// tight loop in one SPI.beginTransaction/endTransaction.
static inline uint8_t fastRssi() {
  GPIO.out_w1tc = (1u << CS_PIN); // CS low
  SPI.transfer(RSSI_REG & 0x7f);  // read address (MSB=0)
  uint8_t v = SPI.transfer(0x00);
  GPIO.out_w1ts = (1u << CS_PIN); // CS high
  return v;
}
static inline int readRssi() { // single read with its own transaction (trigger/setup use)
  SPI.beginTransaction(spiSet);
  uint8_t v = fastRssi();
  SPI.endTransaction();
  return v;
}

// ---- self-calibration: pick the cleanest R900 window with NO external oracle ----
// R900 meters hop uniformly over ~50 channels (911-920 MHz), so any window in the comb catches the
// meter ~1/50 of its bursts; the only thing that ruins a channel is a continuous interferer. So we
// sweep the band and park on the lowest-DUTY window (fraction of a dwell sitting below the noise
// floor): a continuous carrier reads high duty, a clean channel reads ~0% (just noise + rare bursts).
#define SCAN_LO    911.0f
#define SCAN_HI    920.0f
#define SCAN_STEP  0.25f     // MHz ~ one RxBw window
#define SCAN_DWELL 400       // ms per channel
static float watchFreq = 916.300f;

static void retune(float f) { radio.standby(); radio.setFrequency(f); radio.setOOK(true); radio.receiveDirect(); delayMicroseconds(800); }

static void surveyChannels() {
  Serial.println("=== channel survey (no oracle): find cleanest R900 window ===");
  float best = watchFreq; int bestDuty = 100000, bestNoise = 0;
  for (float f = SCAN_LO; f <= SCAN_HI + 0.001f; f += SCAN_STEP) {
    retune(f);
    SPI.beginTransaction(spiSet);
    long acc = 0; for (int i = 0; i < 1000; i++) acc += fastRssi();   // noise floor on this channel
    int nf = acc / 1000, trig = nf - 30;
    uint32_t below = 0, tot = 0, t0 = millis();                        // duty over the dwell
    while (millis() - t0 < SCAN_DWELL) { if (fastRssi() < trig) below++; tot++; }
    SPI.endTransaction();
    int dutyPM = (int)(below * 1000ULL / tot);                         // per-mille active
    Serial.printf("  %.3f MHz  noise=%d(%.0fdBm)  duty=%d.%d%%  %s\n",
                  f, nf, -nf / 2.0, dutyPM / 10, dutyPM % 10, dutyPM > 200 ? "<-- carrier/interferer" : "");
    // lowest duty wins; tie-break on the quietest floor (higher RSSI val = lower power)
    if (dutyPM < bestDuty - 5 || (abs(dutyPM - bestDuty) <= 5 && nf > bestNoise)) {
      bestDuty = dutyPM; bestNoise = nf; best = f;
    }
  }
  watchFreq = best;
  Serial.printf("=> watching %.3f MHz (duty=%d.%d%%, cleanest window)\n", watchFreq, bestDuty / 10, bestDuty % 10);
  retune(watchFreq);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println("=== OPTIONB RSSI-envelope capture de-risk ===");

  // OLED is OPTIONAL — probe at 0x3C first with a short timeout so a missing/mis-wired panel
  // can't stall the I2C bus and watchdog-reset the boot (root cause of the earlier boot loop).
  Wire.begin(21, 22);
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  Wire.beginTransmission(0x3C);
  oledPresent = (Wire.endTransmission() == 0);
  Serial.printf("OLED %s\n", oledPresent ? "found @0x3C" : "absent — running headless");
  if (oledPresent) {
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 26, "R900 reader");
    u8g2.drawStr(0, 42, "scanning band...");
    u8g2.sendBuffer();
  }

  SPI.begin(5, 19, 27, 18);
  // RxBw WIDE (250kHz max): RSSI must track R900's 30us carrier-off gaps. 100kHz was too slow
  // (slow decay -> gaps missed -> mostly-1 bitstream). Wider = faster envelope (more noise, OK).
  int st = radio.beginFSK(916.300, 4.8, 5.0, 250.0, 10, 16); // freq,br,freqDev,rxBw,pwr,preamble (915.662 had a continuous interferer)
  Serial.printf("beginFSK=%d\n", st);
  radio.setOOK(true);
  radio.setRSSIConfig(RADIOLIB_SX127X_RSSI_SMOOTHING_SAMPLES_2); // fastest tracking
  st = radio.receiveDirect();
  Serial.printf("receiveDirect=%d\n", st);

  surveyChannels();   // self-calibrate: pick the cleanest window in the R900 band (portable to any site)

  // measure poll rate + noise floor on the CHOSEN channel (the tight-loop path, one transaction)
  SPI.beginTransaction(spiSet);
  uint32_t t0 = micros();
  long acc = 0;
  for (int i = 0; i < 4000; i++) acc += fastRssi();
  uint32_t t1 = micros();
  SPI.endTransaction();
  noiseVal = acc / 4000;
  trigVal  = noiseVal - 30;             // ~15 dB above floor (lower val = stronger) — strong bursts only
  Serial.printf("poll=%.3f us/sample  noise_val=%d (%.1f dBm)  trigVal=%d (%.1f dBm)\n",
                (t1 - t0) / 4000.0, noiseVal, -noiseVal / 2.0, trigVal, -trigVal / 2.0);

  // WiFi + MQTT last (async; keeps survey/noise timing clean). Non-blocking: RF runs regardless.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  if (WiFi.status() == WL_CONNECTED) Serial.printf("WiFi %s  IP %s\n", WLAN_SSID, WiFi.localIP().toString().c_str());
  else Serial.println("WiFi not connected — continuing RF/serial only");
  mqtt.setServer(AIO_SERVER, AIO_SERVERPORT);
  mqtt.setBufferSize(640);
  mqtt.setKeepAlive(300);   // > worst-case idle gap (capture blocks up to the 240s re-survey) so LWT won't false-fire
  mqttEnsure();             // connect now -> publishes birth "online" + reader-online discovery at boot

  drawDisplay();            // status bar + "waiting for meter"
  Serial.println("waiting for bursts (RSSI val drops below trigVal)...");
}

#define PRE 700   // samples kept BEFORE the trigger (~2.7ms) so the preamble onset is captured

void loop() {
  mqttEnsure();                        // connect/maintain (publishes birth + reader discovery on connect)
  if (mqtt.connected()) {
    mqtt.loop();                       // services keepalive PINGs; called each loop (<= ~243s apart < keepalive)
    if (millis() - lastBeat > 300000) { mqtt.publish(AVAIL_TOPIC, "online", true); lastBeat = millis(); }  // 5-min heartbeat (retained)
  }

  // Continuous ring-buffer fill with uniform per-sample work (read + compare), so the
  // pre-trigger preamble is captured at the same dt as the rest. Trigger = 5 consecutive
  // strong samples. After trigger, keep filling until PRE samples precede + the rest follow.
  uint32_t n = 0; long trigN = -1; int consec = 0; uint32_t tTrig = 0, tEnd = 0;
  uint32_t waitStart = millis();
  SPI.beginTransaction(spiSet);
  for (;;) {
    uint8_t v = fastRssi();
    cap[n % CAP_N] = v;
    if (trigN < 0) {
      if (v < trigVal) { if (++consec >= 5) { trigN = n; tTrig = micros(); } }
      else consec = 0;
      // window gone quiet (dead or env changed) — bail out and re-survey
      if ((n & 0x3FFFF) == 0 && millis() - waitStart > 240000) { SPI.endTransaction();
        Serial.println("channel quiet 4min — re-surveying"); surveyChannels(); return; }
      // refresh OLED ~1/s while idle (consec==0 -> no burst forming; I2C is off the radio SPI bus)
      if (consec == 0 && (n & 0x1FFF) == 0) {
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw > 1000) { drawDisplay(); lastDraw = millis(); }
      }
    } else if ((long)n - trigN >= CAP_N - PRE) { tEnd = micros(); break; }
    n++;
  }
  SPI.endTransaction();

  float dt = (float)(tEnd - tTrig) / (n - trigN);     // uniform post-trigger sample period
  long start = trigN - PRE;                            // first sample (chronological) to keep

  // Linearise the ring into chronological order, then median-3 (kills single-sample glitches).
  int mn = 255, above = 0;
  for (long k = 0; k < CAP_N; k++) {
    uint8_t x = cap[(start + k) % CAP_N];
    lin[k] = x;
    if (x < mn) mn = x; if (x < trigVal) above++;
  }
  bool r900like = (above >= 700 && above <= 9000);     // bursty packet, not a continuous carrier
  Serial.printf("BURST dt=%.3fus/sample N=%d min=%d(%.1fdBm) samples_in_burst=%d %s\n",
                dt, CAP_N, mn, -mn / 2.0, above, r900like ? "R900LIKE" : "skip-continuous");
  if (!r900like) { delay(800); return; }

#ifdef DUMP_RAW
  for (long k = 0; k < CAP_N; k++) sprintf(hexbuf + k * 2, "%02x", lin[k]);
  Serial.print("CAP:"); Serial.println(hexbuf);
#endif

  // median-3 in place (keep the original previous sample so neighbours aren't corrupted)
  uint8_t prevOrig = lin[0];
  for (int i = 1; i < CAP_N - 1; i++) {
    uint8_t a = prevOrig, bb = lin[i], c = lin[i + 1];
    prevOrig = lin[i];
    lin[i] = a + bb + c - max(a, max(bb, c)) - min(a, min(bb, c));
  }

  // Burst-local on/off levels: RSSI slow-decay leaves the in-burst "off" level far below the
  // global noise floor, so threshold within the burst's own band (not the global floor).
  int hist[256]; memset(hist, 0, sizeof(hist));
  for (long k = 0; k < CAP_N; k++) hist[lin[k]]++;
  int acc = 0, noise = mn; for (int v = 0; v < 256; v++) { acc += hist[v]; if (acc >= (int)(CAP_N * 0.90)) { noise = v; break; } }
  int globMid = (mn + noise) / 2;
  long lo = -1, hi = -1;
  for (long k = 0; k < CAP_N; k++) if (lin[k] < globMid) { if (lo < 0) lo = k; hi = k; }
  if (lo < 0) { delay(800); return; }
  int rmn = 255, rhist[256]; memset(rhist, 0, sizeof(rhist));
  for (long k = lo; k <= hi; k++) { if (lin[k] < rmn) rmn = lin[k]; rhist[lin[k]]++; }
  int racc = 0, roff = noise, rcount = hi - lo + 1;
  for (int v = 0; v < 256; v++) { racc += rhist[v]; if (racc >= (int)(rcount * 0.75)) { roff = v; break; } }

  // Sweep threshold / clock / phase; first valid R900 frame wins (frac 0.30 usually hits first).
  float spb0 = BIT_US / dt;
  static const float fracs[] = {0.30f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f, 0.70f};
  static const float muls[]  = {0.98f, 0.99f, 1.0f, 1.01f, 1.02f};
  R900 r; bool got = false;
  uint32_t t0 = millis();
  for (unsigned fi = 0; fi < 7 && !got; fi++) {
    int thresh = rmn + (int)(fracs[fi] * (roff - rmn));
    for (unsigned mi = 0; mi < 5 && !got; mi++) {
      float spb = spb0 * muls[mi];
      for (float ph = 0; ph <= 2 * spb && !got; ph += 0.5f) {
        recover(thresh, spb, ph);
        if (gnbits < 240) continue;
        if (r900_decode(&r)) { got = true;
          Serial.printf("*** R900 DECODE  id=%u consumption=%u backflow=%d leak=%d leaknow=%d "
                        "(frac=%.2f spb=%.2f phase=%.1f) %ums ***\n",
                        r.id, r.consumption, r.backflow, r.leak, r.leaknow,
                        fracs[fi], spb, ph, (unsigned)(millis() - t0));
          publishReading(r, mn);   // mn = strongest RSSI val in the burst
          lastAnyMs = millis();    // any decode -> RX-active indicator
          if (r.id == MY_METER_ID) { myReading = r; myMs = millis(); haveMy = true; }
          drawDisplay();
        }
      }
    }
  }
  if (!got) Serial.printf("no decode (%ums sweep)\n", (unsigned)(millis() - t0));
  delay(800);
}
#endif
