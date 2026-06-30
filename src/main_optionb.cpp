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
#include <WiFiManager.h>   // tzapu: captive-portal provisioning for WiFi + MQTT params
#include <Preferences.h>   // NVS storage for the runtime config
#include <PubSubClient.h>
#include <U8g2lib.h>     // SSD1306 OLED (LoRa32 V2.1.6: I2C SDA=21 SCL=22 RST=16)
#include <Wire.h>
#include <Ticker.h>      // background flashing of the leak LED (independent of the capture loop)
#include "secrets.h"     // optional compile-time DEFAULTS; the captive portal overrides these at runtime

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

// ---- runtime configuration (set via the captive portal, persisted in NVS) ----
// secrets.h supplies the compile-time defaults; the portal overrides them and stores to flash.
// This board (Lilygo T3 v1.6.x) has NO BOOT button, so the portal is triggered by DOUBLE-TAPPING
// the RST button. The "armed" flag is stored in NVS/flash, NOT RTC memory: the physical RST button
// drives the EN pin, a full hardware reset that WIPES RTC RAM on the ESP32 (RTC double-reset never
// fires). Flash survives it. Each boot arms the flag for ~3s; a 2nd reset in that window leaves it
// armed, and the next boot reads it set -> force portal.
#define PRODUCT_NAME  "R900 Reader"      // shown on the OLED + as the captive-portal heading
#define CFG_AP_NAME   "R900-Reader-Setup"
#define DRD_WINDOW_MS 5000               // double-tap window (time to press RST again for setup)
static Preferences  prefs;
static char         cfgServer[40];       // MQTT broker host/IP
static uint16_t     cfgPort = 1883;      // MQTT port
static char         cfgUser[32];         // MQTT username
static char         cfgPass[48];         // MQTT password
static uint32_t     gMeterId = 0;        // the meter the OLED tracks (0 = none/any)
static bool         diagEnabled = false; // publish diagnostic sensors to MQTT (toggle in the portal, NVS)
static uint32_t     lastDiag = 0;        // last diagnostic publish (ms)
#define DIAG_TOPIC  "lora32r900/diag/state"

// HA diagnostic sensors (watched freq / last-known-good / seconds since last decode). Published only
// when diagEnabled; passing en=false sends empty retained configs to REMOVE them from HA.
static void publishDiagDiscovery(bool en) {
  struct { const char *obj, *name, *tpl, *unit; } d[] = {
    {"watch_freq",   "Watch Frequency",        "{{ value_json.freq }}",    "MHz"},
    {"good_freq",    "Last-Known-Good Freq",   "{{ value_json.good }}",    "MHz"},
    {"since_decode", "Since Last Decode",      "{{ value_json.since_s }}", "s"},
  };
  char t[96], p[420];
  for (auto &x : d) {
    snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%s/config", x.obj);
    if (en) {
      snprintf(p, sizeof(p),
        "{\"name\":\"%s\",\"uniq_id\":\"lora32r900_%s\",\"stat_t\":\"" DIAG_TOPIC "\",\"avty_t\":\"" AVAIL_TOPIC "\","
        "\"ent_cat\":\"diagnostic\",\"unit_of_meas\":\"%s\",\"val_tpl\":\"%s\","
        "\"dev\":{\"ids\":[\"lora32r900_reader\"]}}", x.name, x.obj, x.unit, x.tpl);
      mqtt.publish(t, p, true);
    } else {
      mqtt.publish(t, "", true);            // empty retained payload removes the entity from HA
    }
  }
}

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
  if (WiFi.status() != WL_CONNECTED || mqtt.connected() || cfgServer[0] == '\0') return;
  String cid = "lora32r900-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  // LWT: if we drop without a clean disconnect, the broker retains "offline" on AVAIL_TOPIC.
  if (mqtt.connect(cid.c_str(), cfgUser, cfgPass, AVAIL_TOPIC, 0, true, "offline")) {
    mqtt.publish(AVAIL_TOPIC, "online", true);   // birth (retained, overwrites the previous value)
    lastBeat = millis();
    publishReaderDiscovery();
    publishDiagDiscovery(diagEnabled);           // create or remove the diagnostic sensors per the toggle
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

// ---- watched frequency + auto-hop fallback ----
#define DECODE_TIMEOUT_MS 2700000UL   // 45 min with no decode -> hop to a different frequency (adjustable)
static float    watchFreq = 916.300f; // currently parked frequency (shown in the OLED header)
static float    goodFreq  = 0;        // last frequency that produced a decode (persisted in NVS)
static uint32_t lastDecodeRef = 0;    // millis() baseline for the decode-timeout (reset on boot/decode/hop)

// ---- leak LED (the board's one user LED, GPIO25; the red power LED is hardwired = power indicator) ----
#define LEAK_LED_PIN  25              // LED_BUILTIN on this board (ttgo-lora32-v21new)
static Ticker  leakTicker;
static void    leakToggle() { static bool on = false; on = !on; digitalWrite(LEAK_LED_PIN, on); }
// 0=none -> off, 1=intermittent -> slow flash, 2=continuous/now -> fast flash
static void    setLeakLed(int leaknow) {
  leakTicker.detach();
  if      (leaknow == 1) leakTicker.attach_ms(1500, leakToggle);  // slow flash (1.5s) = intermittent leak
  else if (leaknow == 2) leakTicker.attach_ms(500, leakToggle);   // fast flash (0.5s) = definite leak now
  else                   digitalWrite(LEAK_LED_PIN, LOW);         // no leak -> off
}

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
  // top status bar: WiFi icon + MQTT + RX flags + watched frequency (right-aligned)
  drawWifi(0, wifi);
  drawFlag(18, "MQTT", mq);
  drawFlag(52, "RX", rx);
  char fb[10]; snprintf(fb, sizeof(fb), "%.2f", watchFreq);   // e.g. 916.25
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(128 - (int)strlen(fb) * 5, 9, fb);
  u8g2.drawHLine(0, 13, 128);
  // body
  u8g2.setFont(u8g2_font_6x12_tf);
  if (!haveMy) {
    u8g2.drawStr(0, 27, "Waiting for meter");
    char m[24]; snprintf(m, sizeof(m), gMeterId ? "%u ..." : "(any)", (unsigned)gMeterId);
    u8g2.drawStr(0, 41, m);
    uint32_t el = millis() - lastDecodeRef;                       // countdown to the channel-hop timeout
    uint32_t rem = (DECODE_TIMEOUT_MS > el) ? (DECODE_TIMEOUT_MS - el) / 1000 : 0;
    char c[26]; snprintf(c, sizeof(c), "New chan in %lu:%02lu",
                         (unsigned long)(rem / 60), (unsigned long)(rem % 60));
    u8g2.drawStr(0, 58, c);
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

// shown on the OLED while the WiFiManager captive portal is open (first boot or BOOT-button forced)
static void portalOnOLED(WiFiManager *) {
  if (!oledPresent) return;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "WiFi/MQTT setup");
  u8g2.drawHLine(0, 15, 128);
  u8g2.drawStr(0, 30, "1. Join WiFi:");
  u8g2.drawStr(6, 42, CFG_AP_NAME);
  u8g2.drawStr(0, 58, "2. open 192.168.4.1");
  u8g2.sendBuffer();
}

// Load saved config (or secrets.h defaults) from NVS, then run WiFiManager. Blocks in the captive
// portal only on first setup, if creds are missing, or when BOOT is held during reset.
static void runProvisioning(bool forcePortal) {
  prefs.begin("r900", false);
  prefs.getString("server", AIO_SERVER).toCharArray(cfgServer, sizeof(cfgServer));
  cfgPort = prefs.getUShort("port", AIO_SERVERPORT);
  prefs.getString("user", AIO_USERNAME).toCharArray(cfgUser, sizeof(cfgUser));
  prefs.getString("pass", AIO_KEY).toCharArray(cfgPass, sizeof(cfgPass));
  gMeterId = prefs.getUInt("meter", MY_METER_ID);
  diagEnabled = prefs.getBool("diag", false);

  // If WiFi was never stored but secrets.h provides creds, connect with them first (no portal needed).
  // Wait for a full connection (WL_CONNECTED implies DHCP done on ESP32) so we don't race autoConnect.
  if (WiFi.SSID().isEmpty() && strlen(WLAN_SSID) > 0) {
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    for (int i = 0; i < 32 && WiFi.status() != WL_CONNECTED; i++) delay(250);   // up to 8s
  }

  char portBuf[8]; snprintf(portBuf, sizeof(portBuf), "%u", cfgPort);
  char meterBuf[12]; snprintf(meterBuf, sizeof(meterBuf), "%u", (unsigned)gMeterId);
  char diagBuf[2]; snprintf(diagBuf, sizeof(diagBuf), "%d", diagEnabled ? 1 : 0);
  WiFiManager wm;
  WiFiManagerParameter p_server("server", "MQTT server (IP/host)",      cfgServer, sizeof(cfgServer) - 1);
  WiFiManagerParameter p_port  ("port",   "MQTT port",                  portBuf,   sizeof(portBuf) - 1);
  WiFiManagerParameter p_user  ("user",   "MQTT username",              cfgUser,   sizeof(cfgUser) - 1);
  WiFiManagerParameter p_pass  ("pass",   "MQTT password",              cfgPass,   sizeof(cfgPass) - 1);
  WiFiManagerParameter p_meter ("meter",  "Meter ID (0 = any)",         meterBuf,  sizeof(meterBuf) - 1);
  WiFiManagerParameter p_diag  ("diag",   "Diagnostics to MQTT (1/0)",  diagBuf,   1);
  wm.addParameter(&p_server); wm.addParameter(&p_port); wm.addParameter(&p_user);
  wm.addParameter(&p_pass);   wm.addParameter(&p_meter);  wm.addParameter(&p_diag);
  wm.setAPCallback(portalOnOLED);
  wm.setConfigPortalTimeout(180);   // give up after 3 min and run offline rather than block forever
  wm.setBreakAfterConfig(true);
  wm.setTitle(PRODUCT_NAME);        // portal heading (replaces the default "WiFiManager" brand)
  static const char *portalMenu[] = {"wifi", "info", "sep", "restart", "exit"};
  wm.setMenu(portalMenu, 5);        // adds a Restart button to the portal
  // the page configures more than WiFi, so relabel the "Configure WiFi" button to just "Configure"
  wm.setCustomHeadElement(
    "<script>addEventListener('load',function(){var b=document.querySelector(\"form[action='/wifi'] button\");if(b)b.textContent='Configure';});</script>");

  Serial.printf("WiFiManager: %s\n", forcePortal ? "double-reset -> forcing setup portal" : "autoConnect");
  if (forcePortal) wm.startConfigPortal(CFG_AP_NAME);
  else             wm.autoConnect(CFG_AP_NAME);

  // Wait for DHCP to actually assign an address before reporting (avoids printing/using 0.0.0.0).
  for (int i = 0; i < 20 && WiFi.status() == WL_CONNECTED && WiFi.localIP() == IPAddress(0, 0, 0, 0); i++)
    delay(250);

  // Read back whatever the portal holds (edited values if shown, else the seeded defaults) and persist.
  strncpy(cfgServer, p_server.getValue(), sizeof(cfgServer) - 1);
  cfgPort = (uint16_t)atoi(p_port.getValue());
  strncpy(cfgUser, p_user.getValue(), sizeof(cfgUser) - 1);
  strncpy(cfgPass, p_pass.getValue(), sizeof(cfgPass) - 1);
  gMeterId = strtoul(p_meter.getValue(), nullptr, 10);
  diagEnabled = (atoi(p_diag.getValue()) != 0);
  prefs.putString("server", cfgServer); prefs.putUShort("port", cfgPort);
  prefs.putString("user", cfgUser);     prefs.putString("pass", cfgPass);
  prefs.putUInt("meter", gMeterId);     prefs.putBool("diag", diagEnabled);

  WiFi.setAutoReconnect(true);
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("WiFi %s  IP %s  meter=%u  mqtt=%s:%u\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), (unsigned)gMeterId, cfgServer, cfgPort);
  else
    Serial.println("WiFi not connected — continuing RF/serial only");
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
#define PREF_FREQ  916.0f    // when the band is uniformly clean, bias toward this (R900 hop-set sweet spot)

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
    // lowest duty wins; on a tie prefer the window nearest PREF_FREQ (the meter's productive region,
    // so a uniformly-clean band parks near 916 MHz rather than the arbitrary band edge)
    if (dutyPM < bestDuty - 5 ||
        (abs(dutyPM - bestDuty) <= 5 && fabsf(f - PREF_FREQ) < fabsf(best - PREF_FREQ))) {
      bestDuty = dutyPM; bestNoise = nf; best = f;
    }
  }
  watchFreq = best;
  Serial.printf("=> watching %.3f MHz (duty=%d.%d%%, cleanest window)\n", watchFreq, bestDuty / 10, bestDuty % 10);
  retune(watchFreq);
}

// measure the noise floor on the current channel and set the burst trigger threshold
static void measureNoise() {
  SPI.beginTransaction(spiSet);
  long acc = 0;
  for (int i = 0; i < 4000; i++) acc += fastRssi();
  SPI.endTransaction();
  noiseVal = acc / 4000;
  trigVal  = noiseVal - 30;            // ~15 dB above floor (lower val = stronger) — strong bursts only
  Serial.printf("noise_val=%d (%.1f dBm)  trigVal=%d (%.1f dBm)  on %.3f MHz\n",
                noiseVal, -noiseVal / 2.0, trigVal, -trigVal / 2.0, watchFreq);
}

// remember the frequency that just decoded a meter (persist to NVS for the next boot)
static void saveGoodFreq(float f) {
  if (f == goodFreq) return;
  goodFreq = f;
  Preferences p; p.begin("r900", false); p.putFloat("goodfreq", f); p.end();
  Serial.printf("saved last-known-good frequency %.3f MHz\n", f);
}

// decode-timeout fallback: step to the next band window, skipping obvious interferers
static void hopToNextFreq() {
  const int windows = (int)((SCAN_HI - SCAN_LO) / SCAN_STEP) + 1;
  float f = watchFreq;
  for (int tries = 0; tries < windows; tries++) {
    f += SCAN_STEP; if (f > SCAN_HI + 0.001f) f = SCAN_LO;
    retune(f);
    SPI.beginTransaction(spiSet);
    long acc = 0; for (int i = 0; i < 1000; i++) acc += fastRssi();
    int nf = acc / 1000, trig = nf - 30;
    uint32_t below = 0, tot = 0, t0 = millis();
    while (millis() - t0 < 200) { if (fastRssi() < trig) below++; tot++; }
    SPI.endTransaction();
    if ((int)(below * 1000ULL / tot) <= 200) break;    // clean enough (<=20% duty) — park here
  }
  watchFreq = f;
  retune(watchFreq);
  measureNoise();
  lastDecodeRef = millis();
  Serial.printf("=> hopped to %.3f MHz (decode-timeout fallback)\n", watchFreq);
}

// publish the diagnostic JSON (watched freq, last-known-good, seconds since last decode)
static void publishDiag() {
  if (!diagEnabled || !mqtt.connected()) return;
  char p[160];
  snprintf(p, sizeof(p), "{\"freq\":%.3f,\"good\":%.3f,\"since_s\":%lu}",
           watchFreq, goodFreq, (unsigned long)((millis() - lastDecodeRef) / 1000));
  mqtt.publish(DIAG_TOPIC, p, true);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println("=== OPTIONB RSSI-envelope capture de-risk ===");
  pinMode(LEAK_LED_PIN, OUTPUT); digitalWrite(LEAK_LED_PIN, LOW);   // leak LED off until we have a reading

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

  // Double-tap-RST detector (flag in NVS so it survives the EN-pin reset). A 2nd reset within
  // DRD_WINDOW_MS leaves the flag armed -> this boot forces the WiFi/MQTT setup portal.
  Preferences drd;
  drd.begin("drd", false);
  bool forcePortal = drd.getBool("armed", false);
  if (forcePortal) {
    drd.putBool("armed", false);               // consume the trigger
    drd.end();
    Serial.println("double-reset detected -> launching setup portal (skipping survey)");
    if (oledPresent) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x12_tf);
      u8g2.drawStr(0, 11, PRODUCT_NAME);
      u8g2.drawHLine(0, 14, 128);
      u8g2.drawStr(0, 34, "Launching");
      u8g2.drawStr(0, 50, "WiFi/MQTT setup...");
      u8g2.sendBuffer();
    }
    runProvisioning(true);                      // open the captive portal NOW, before any radio work
    Serial.println("setup portal closed -> restarting");
    delay(300);
    ESP.restart();                              // reboot into normal operation with the new config
  } else {
    drd.putBool("armed", true);                // arm; commits to flash immediately
    drd.end();
    if (oledPresent) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x12_tf);
      u8g2.drawStr(0, 11, PRODUCT_NAME);
      u8g2.drawHLine(0, 14, 128);
      u8g2.drawStr(0, 34, "Press RST again now");
      u8g2.drawStr(0, 50, "= WiFi/MQTT setup");
      u8g2.sendBuffer();
    }
    uint32_t w = millis();                      // hold the arm window so a 2nd tap can be caught
    while (millis() - w < DRD_WINDOW_MS) delay(10);
    drd.begin("drd", false);
    drd.putBool("armed", false);                // disarm: window passed, a later lone reset won't trigger
    drd.end();
    if (oledPresent) {                          // restore the boot screen for the upcoming survey
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x12_tf);
      u8g2.drawStr(0, 26, PRODUCT_NAME);
      u8g2.drawStr(0, 42, "scanning band...");
      u8g2.sendBuffer();
    }
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

  { Preferences p; p.begin("r900", true); goodFreq = p.getFloat("goodfreq", 0.0f); p.end(); }

  surveyChannels();   // self-calibrate: pick the cleanest window in the R900 band (portable to any site)

  // prefer the last frequency that actually decoded a meter, if we have one (faster reacquisition)
  if (goodFreq >= SCAN_LO && goodFreq <= SCAN_HI) {
    watchFreq = goodFreq;
    retune(watchFreq);
    Serial.printf("=> parking on last-known-good %.3f MHz\n", watchFreq);
  }

  measureNoise();                  // noise floor + trigger threshold on the chosen channel
  lastDecodeRef = millis();        // start the decode-timeout clock

  // WiFi + MQTT last (keeps survey/noise timing clean). Captive-portal provisioning: connects with
  // saved/seed creds, or opens the "R900-Reader-Setup" AP (double-tap RST to force it).
  runProvisioning(forcePortal);
  mqtt.setServer(cfgServer, cfgPort);
  mqtt.setBufferSize(640);
  mqtt.setKeepAlive(300);   // > worst-case idle gap (capture blocks up to the 240s re-survey) so LWT won't false-fire
  mqttEnsure();             // connect now -> publishes birth "online" + reader-online discovery at boot

  drawDisplay();            // status bar + "waiting for meter"
  Serial.println("waiting for bursts (RSSI val drops below trigVal)...");
}

#define PRE 700   // samples kept BEFORE the trigger (~2.7ms) so the preamble onset is captured

static void serviceMqtt() {
  mqttEnsure();                        // connect/maintain (publishes birth + reader discovery on connect)
  if (mqtt.connected()) {
    mqtt.loop();                       // services keepalive PINGs
    if (millis() - lastBeat > 300000) { mqtt.publish(AVAIL_TOPIC, "online", true); lastBeat = millis(); }  // 5-min heartbeat
    if (diagEnabled && millis() - lastDiag > 30000) { publishDiag(); lastDiag = millis(); }                // 30s diagnostics
  }
}

// Capture one burst on the current channel and try to decode it. Pure capture + DSP + decode; the
// caller decides what to do with the result (publish / hop / tally). Returns:
//   -1 = idle (no trigger within maxIdleMs)   0 = burst seen, no valid decode   1 = decoded (fills *r)
static int captureOnce(R900 *r, int *rssiOut, uint32_t maxIdleMs) {
  // Continuous ring-buffer fill with uniform per-sample work, so the pre-trigger preamble is
  // captured at the same dt as the rest. Trigger = 5 consecutive strong samples.
  uint32_t n = 0; long trigN = -1; int consec = 0; uint32_t tTrig = 0, tEnd = 0;
  uint32_t waitStart = millis();
  SPI.beginTransaction(spiSet);
  for (;;) {
    uint8_t v = fastRssi();
    cap[n % CAP_N] = v;
    if (trigN < 0) {
      if (v < trigVal) { if (++consec >= 5) { trigN = n; tTrig = micros(); } }
      else consec = 0;
      if ((n & 0x3FFFF) == 0 && millis() - waitStart > maxIdleMs) { SPI.endTransaction(); return -1; }
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
  *rssiOut = mn;
  bool r900like = (above >= 700 && above <= 9000);     // bursty packet, not a continuous carrier
  Serial.printf("BURST dt=%.3fus/sample N=%d min=%d(%.1fdBm) samples_in_burst=%d %s\n",
                dt, CAP_N, mn, -mn / 2.0, above, r900like ? "R900LIKE" : "skip-continuous");
  if (!r900like) return 0;

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
  if (lo < 0) return 0;
  int rmn = 255, rhist[256]; memset(rhist, 0, sizeof(rhist));
  for (long k = lo; k <= hi; k++) { if (lin[k] < rmn) rmn = lin[k]; rhist[lin[k]]++; }
  int racc = 0, roff = noise, rcount = hi - lo + 1;
  for (int v = 0; v < 256; v++) { racc += rhist[v]; if (racc >= (int)(rcount * 0.75)) { roff = v; break; } }

  // Sweep threshold / clock / phase; first valid R900 frame wins (frac 0.30 usually hits first).
  float spb0 = BIT_US / dt;
  static const float fracs[] = {0.30f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f, 0.70f};
  static const float muls[]  = {0.98f, 0.99f, 1.0f, 1.01f, 1.02f};
  uint32_t t0 = millis();
  for (unsigned fi = 0; fi < 7; fi++) {
    int thresh = rmn + (int)(fracs[fi] * (roff - rmn));
    for (unsigned mi = 0; mi < 5; mi++) {
      float spb = spb0 * muls[mi];
      for (float ph = 0; ph <= 2 * spb; ph += 0.5f) {
        recover(thresh, spb, ph);
        if (gnbits < 240) continue;
        if (r900_decode(r)) {
          Serial.printf("*** R900 DECODE  id=%u consumption=%u backflow=%d leak=%d leaknow=%d "
                        "(frac=%.2f spb=%.2f phase=%.1f) %ums ***\n",
                        r->id, r->consumption, r->backflow, r->leak, r->leaknow,
                        fracs[fi], spb, ph, (unsigned)(millis() - t0));
          return 1;
        }
      }
    }
  }
  Serial.printf("no decode (%ums sweep)\n", (unsigned)(millis() - t0));
  return 0;
}

#ifdef FREQ_COVERAGE_TEST
// ===== one-off coverage test: park each band window for COV_DWELL_MS, log every decode to serial =====
// Build: pio run -e coverage -t upload. Capture serial to a file; analyse the COV-* lines in post.
#define COV_DWELL_MS 1500000UL    // 25 min per window (>= ~2 R900 hop cycles)

void loop() {
  static bool announced = false;
  if (!announced) {
    Serial.printf("=== COVERAGE TEST: %.0f-%.0f MHz step %.2f, %.1f min/window, sweeps repeat ===\n",
                  SCAN_LO, SCAN_HI, SCAN_STEP, COV_DWELL_MS / 60000.0);
    announced = true;
  }
  for (float f = SCAN_LO; f <= SCAN_HI + 0.001f; f += SCAN_STEP) {
    watchFreq = f;
    retune(f);
    measureNoise();
    Serial.printf("COV-WIN-START f=%.3f noise=%d t=%lu\n", f, noiseVal, (unsigned long)millis());
    int decodes = 0, bursts = 0;
    uint32_t winStart = millis();
    while (millis() - winStart < COV_DWELL_MS) {
      serviceMqtt();
      R900 r; int rssi;
      int res = captureOnce(&r, &rssi, 5000);
      if (res >= 0) bursts++;                 // a burst triggered (decoded or not)
      if (res == 1) {
        decodes++;
        Serial.printf("COV-DEC f=%.3f id=%u cons=%u rssi=%d leak=%d leaknow=%d t=%lu\n",
                      f, r.id, r.consumption, rssi, r.leak, r.leaknow, (unsigned long)millis());
        publishReading(r, rssi);              // keep HA fed so the meter doesn't look offline mid-test
      }
    }
    Serial.printf("COV-WIN-DONE f=%.3f decodes=%d bursts=%d noise=%d t=%lu\n",
                  f, decodes, bursts, noiseVal, (unsigned long)millis());
  }
  Serial.printf("COV-SWEEP-DONE t=%lu — starting next pass\n", (unsigned long)millis());
}

#else
void loop() {
  serviceMqtt();
  R900 r; int rssi;
  int res = captureOnce(&r, &rssi, 3000);     // returns within ~3s if idle so MQTT/OLED stay serviced
  if (res < 0) {                              // idle: refresh display, check the decode-timeout fallback
    drawDisplay();
    if (millis() - lastDecodeRef > DECODE_TIMEOUT_MS) {
      Serial.printf("no decode for %lu min — hopping frequency\n", DECODE_TIMEOUT_MS / 60000);
      hopToNextFreq();
    }
    return;
  }
  if (res == 1) {
    publishReading(r, rssi);                  // rssi = strongest RSSI val in the burst
    lastAnyMs = millis();                     // any decode -> RX-active indicator
    lastDecodeRef = millis();                 // reset the decode-timeout clock
    saveGoodFreq(watchFreq);                  // this frequency works -> remember it for next boot
    if (gMeterId && r.id == gMeterId) { myReading = r; myMs = millis(); haveMy = true; setLeakLed(r.leaknow); }
    drawDisplay();
  } else {
    delay(800);                               // burst seen but no decode -> brief backoff
  }
}
#endif
#endif
