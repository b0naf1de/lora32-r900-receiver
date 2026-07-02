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
#include <WebServer.h>     // custom config portal (replaces WiFiManager): serves the SPA + JSON API
#include <DNSServer.h>     // captive-portal DNS so phones on the AP land on the setup page
#include <ArduinoJson.h>   // build/parse the /api/* JSON
#include <Preferences.h>   // NVS storage for the runtime config
#include <PubSubClient.h>
#include <U8g2lib.h>     // SSD1306 OLED (LoRa32 V2.1.6: I2C SDA=21 SCL=22 RST=16)
#include <Wire.h>
#include <Ticker.h>      // background flashing of the leak LED (independent of the capture loop)
// No compile-time secrets: the device always starts blank and is configured only via the setup portal.

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
static char cfgTopic[24] = "r900_meter";   // MQTT base topic (configurable in the portal, persisted NVS)
static char availTopic[40];                // "<cfgTopic>/availability" — built at runtime by buildTopics()
static char diagTopic[40];                 // "<cfgTopic>/diag/state"
static char battTopic[40];                 // "<cfgTopic>/batt/state"
static void buildTopics() {
  snprintf(availTopic, sizeof(availTopic), "%s/availability", cfgTopic);
  snprintf(diagTopic,  sizeof(diagTopic),  "%s/diag/state",   cfgTopic);
  snprintf(battTopic,  sizeof(battTopic),  "%s/batt/state",   cfgTopic);
}
static WiFiClient   espClient;
static PubSubClient mqtt(espClient);
static uint32_t     seenIds[8]; static int nSeen = 0;   // ids that already got a discovery message
static uint32_t     lastBeat = 0;                       // last heartbeat publish (ms)

// ---- runtime configuration (set via the setup portal, persisted in NVS; no compile-time defaults) ----
// This board (Lilygo T3 v1.6.x) has NO BOOT button, so the portal is triggered by DOUBLE-TAPPING
// the RST button. The "armed" flag is stored in NVS/flash, NOT RTC memory: the physical RST button
// drives the EN pin, a full hardware reset that WIPES RTC RAM on the ESP32 (RTC double-reset never
// fires). Flash survives it. Each boot arms the flag for ~3s; a 2nd reset in that window leaves it
// armed, and the next boot reads it set -> force portal.
#define PRODUCT_NAME  "R900 Reader"      // shown on the OLED + as the captive-portal heading
#define CFG_AP_NAME   "R900-Reader-Setup"
#define DRD_WINDOW_MS 5000               // double-tap window (time to press RST again for setup)
static Preferences  prefs;
static char         cfgSsid[33];         // WiFi SSID  (we manage creds ourselves, not WiFiManager)
static char         cfgWpass[64];        // WiFi password (never sent to the config page)
static char         cfgServer[40];       // MQTT broker host/IP
static uint16_t     cfgPort = 1883;      // MQTT port
static char         cfgUser[32];         // MQTT username
static char         cfgPass[48];         // MQTT password
// ---- watch-list: meters the device tracks, each with an optional nickname (empty list = watch ALL) ----
#define MAX_WATCH 8
#define NICK_LEN  10                     // max nickname length (chars)
struct Watch { uint32_t id; char nick[NICK_LEN + 1]; };
static Watch        gWatch[MAX_WATCH];
static int          gWatchN = 0;
static uint32_t     gPreferred = 0;      // top-priority meter: drives channel selection + shows first (0 = none)
static bool         watchAll() { return gWatchN == 0 && gPreferred == 0; }   // nothing configured = show/report all
static int          watchIdx(uint32_t id) { for (int i = 0; i < gWatchN; i++) if (gWatch[i].id == id) return i; return -1; }
static bool         isWatched(uint32_t id) { return watchAll() || id == gPreferred || watchIdx(id) >= 0; }
static const char  *nickOf(uint32_t id) { int i = watchIdx(id); return (i >= 0 && gWatch[i].nick[0]) ? gWatch[i].nick : nullptr; }
// label for the OLED / logs: the nickname if set, else "Meter: <id>"
static void meterLabel(uint32_t id, char *buf, size_t n) {
  const char *nk = nickOf(id);
  if (nk) snprintf(buf, n, "%s", nk); else snprintf(buf, n, "Meter: %u", (unsigned)id);
}
static bool         diagEnabled = false; // publish diagnostic sensors to MQTT (toggle in the portal, NVS)
static uint32_t     lastDiag = 0;        // last diagnostic publish (ms)
static uint32_t     lastBatt = 0;        // last battery publish (ms)
#define BATT_MS     300000UL             // publish battery every 5 min
#define BATT_PIN    35                   // LilyGO LoRa32 V2.1.6: VBAT/2 via 100K/100K divider on GPIO35
static void publishBatt();               // forward decl (defined below publishDiag)

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
        "{\"name\":\"%s\",\"uniq_id\":\"lora32r900_%s\",\"stat_t\":\"%s\",\"avty_t\":\"%s\","
        "\"ent_cat\":\"diagnostic\",\"unit_of_meas\":\"%s\",\"val_tpl\":\"%s\","
        "\"dev\":{\"ids\":[\"lora32r900_reader\"]}}", x.name, x.obj, diagTopic, availTopic, x.unit, x.tpl);
      mqtt.publish(t, p, true);
    } else {
      mqtt.publish(t, "", true);            // empty retained payload removes the entity from HA
    }
  }
}

static void publishBattDiscovery() {
  char t[80], p[420];
  struct { const char *obj, *name, *dev_cla, *unit, *icon, *tpl; } d[] = {
    {"battery",     "Battery",      "battery",    "%",  nullptr,             "{{ value_json.pct | round(0) }}"},
    {"power_source","Power Source", nullptr,      nullptr, "mdi:power-plug", "{{ value_json.src }}"},
  };
  for (auto &x : d) {
    snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%s/config", x.obj);
    char dca[32] = "", unt[32] = "", ico[40] = "";
    if (x.dev_cla) snprintf(dca, sizeof(dca), "\"dev_cla\":\"%s\",", x.dev_cla);
    if (x.unit)    snprintf(unt, sizeof(unt), "\"unit_of_meas\":\"%s\",", x.unit);
    if (x.icon)    snprintf(ico, sizeof(ico), "\"ic\":\"%s\",", x.icon);
    snprintf(p, sizeof(p),
      "{\"name\":\"%s\",\"uniq_id\":\"lora32r900_%s\",\"stat_t\":\"%s\",\"avty_t\":\"%s\","
      "\"ent_cat\":\"diagnostic\",%s%s%s\"val_tpl\":\"%s\","
      "\"dev\":{\"ids\":[\"lora32r900_reader\"]}}",
      x.name, x.obj, battTopic, availTopic, dca, unt, ico, x.tpl);
    mqtt.publish(t, p, true);
  }
}

// HA connectivity binary_sensor for the board itself (fed by LWT/birth/heartbeat on AVAIL_TOPIC).
static void publishReaderDiscovery() {
  char p[400];
  snprintf(p, sizeof(p),
    "{\"name\":\"Reader Online\",\"obj_id\":\"lora32r900_reader_online\",\"uniq_id\":\"lora32r900_reader_online\","
    "\"stat_t\":\"%s\",\"dev_cla\":\"connectivity\",\"pl_on\":\"online\",\"pl_off\":\"offline\","
    "\"dev\":{\"ids\":[\"lora32r900_reader\"],\"name\":\"R900 LoRa32 Reader\",\"mf\":\"LilyGO\",\"mdl\":\"LoRa32 SX1276\"}}",
    availTopic);
  mqtt.publish("homeassistant/binary_sensor/lora32r900_reader_online/config", p, true);
}

static void mqttEnsure() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected() || cfgServer[0] == '\0') return;
  String cid = "lora32r900-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  // LWT: if we drop without a clean disconnect, the broker retains "offline" on AVAIL_TOPIC.
  if (mqtt.connect(cid.c_str(), cfgUser, cfgPass, availTopic, 0, true, "offline")) {
    mqtt.publish(availTopic, "online", true);     // birth (retained, overwrites the previous value)
    lastBeat = millis();
    publishReaderDiscovery();
    publishBattDiscovery();
    publishBatt(); lastBatt = millis();
    publishDiagDiscovery(diagEnabled);           // create or remove the diagnostic sensors per the toggle
  }
}

static void publishDiscovery(uint32_t id) {
  char t[112], p[560];
  snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%u_consumption/config", id);
  snprintf(p, sizeof(p),
    "{\"name\":\"Consumption\",\"uniq_id\":\"lora32r900_%u_consumption\","
    "\"stat_t\":\"%s/%u/state\",\"json_attr_t\":\"%s/%u/state\",\"avty_t\":\"%s\","
    "\"unit_of_meas\":\"gal\",\"dev_cla\":\"water\","
    "\"stat_cla\":\"total_increasing\",\"sug_dsp_prc\":1,"
    "\"val_tpl\":\"{{ (value_json.consumption | int) / 10 }}\","  // R900 reports 1/10 gal; name rides as an attribute
    "\"dev\":{\"ids\":[\"lora32r900_%u\"],\"name\":\"Neptune R900 (LoRa32) %u\",\"mf\":\"Neptune\",\"mdl\":\"R900\"}}",
    id, cfgTopic, id, cfgTopic, id, availTopic, id, id);
  mqtt.publish(t, p, true);
  snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%u_leaknow/config", id);
  snprintf(p, sizeof(p),
    "{\"name\":\"Leak Now\",\"uniq_id\":\"lora32r900_%u_leaknow\",\"stat_t\":\"%s/%u/state\","
    "\"avty_t\":\"%s\","
    "\"val_tpl\":\"{{ value_json.leaknow }}\",\"dev\":{\"ids\":[\"lora32r900_%u\"]}}", id, cfgTopic, id, availTopic, id);
  mqtt.publish(t, p, true);
  snprintf(t, sizeof(t), "homeassistant/sensor/lora32r900_%u_rssi/config", id);
  snprintf(p, sizeof(p),
    "{\"name\":\"RSSI\",\"uniq_id\":\"lora32r900_%u_rssi\",\"stat_t\":\"%s/%u/state\","
    "\"avty_t\":\"%s\","
    "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\","
    "\"val_tpl\":\"{{ value_json.rssi }}\",\"dev\":{\"ids\":[\"lora32r900_%u\"]}}", id, cfgTopic, id, availTopic, id);
  mqtt.publish(t, p, true);
}

static void publishReading(const R900 &r, int rssiVal) {
  if (!isWatched(r.id)) return;        // report watched meters only (all when the watch-list is empty)
  mqttEnsure();
  if (!mqtt.connected()) { Serial.println("  (mqtt offline — serial only)"); return; }
  bool known = false; for (int i = 0; i < nSeen; i++) if (seenIds[i] == r.id) known = true;
  if (!known && nSeen < 8) { publishDiscovery(r.id); seenIds[nSeen++] = r.id; }
  const char *nk = nickOf(r.id);       // nickname rides along as an MQTT attribute (empty if unset)
  char t[64], p[224];
  snprintf(t, sizeof(t), "%s/%u/state", cfgTopic, r.id);
  snprintf(p, sizeof(p),
    "{\"id\":%u,\"name\":\"%s\",\"consumption\":%u,\"backflow\":%d,\"leak\":%d,\"leaknow\":%d,\"rssi\":%d}",
    r.id, nk ? nk : "", r.consumption, r.backflow, r.leak, r.leaknow, -rssiVal / 2);
  mqtt.publish(t, p, true);
  Serial.printf("  -> MQTT published (%s)\n", t);
}

// ---- onboard SSD1306 OLED status display ----
// reset = U8X8_PIN_NONE: do NOT drive GPIO16 (faults on the ESP32-PICO-D4 -> boot-loop). The panel
// powers up reset-free and ACKs on I2C, so no reset pulse is needed.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*rst*/ U8X8_PIN_NONE, /*scl*/ 22, /*sda*/ 21);
#define RX_ACTIVE_MS  600000UL        // "receiving" indicator on if any decode within 10 min
static bool     oledPresent = false;  // set true only if a panel ACKs at 0x3C (else run headless)
static uint32_t lastAnyMs = 0;        // millis() of ANY meter's last decode (RX-active indicator)

// ---- discovery table: every recently-heard meter (id + reading + signal). Drives the display
// rotation AND the config pick-list; persisted to NVS so the pick-list survives reboots. ----
#define MAX_METERS   8
#define METER_TTL_MS 900000UL         // 15 min: a meter not heard within this is pruned (stale removal)
#define ROT_MS       10000             // OLED dwells 10s on each shown meter before rotating to the next
#define ROT_BLINK_MS 100              // blank the meter label this long on each rotation (visual cue it changed)
struct MeterSlot { uint32_t id; R900 r; int rssi; uint32_t lastMs; };
static MeterSlot meters[MAX_METERS];
static int       nMeters = 0;
static int       rotPos  = 0;         // position within the VISIBLE (watched) subset
static uint32_t  rotAt   = 0;
static int       curMeterIdx = -1;    // index into meters[] currently in focus (-1 = none); drives OLED+LED
static int       gVisN   = 0;         // # of currently-visible (watched & heard) meters

static void saveDiscovered();         // defined later (needs Preferences)

// indices of meters that should be shown/reported: the watched ones, or ALL if the watch-list is empty.
// The preferred meter (if heard + watched) is ordered first so it leads the rotation.
static int visibleMeters(int *out) {
  int n = 0;
  if (gPreferred && isWatched(gPreferred))
    for (int i = 0; i < nMeters; i++) if (meters[i].id == gPreferred) { out[n++] = i; break; }
  for (int i = 0; i < nMeters; i++) {
    if (!isWatched(meters[i].id)) continue;
    if (gPreferred && meters[i].id == gPreferred) continue;   // already placed first
    out[n++] = i;
  }
  return n;
}
static void pruneMeters() {           // drop stale meters; persist if the set changed
  uint32_t now = millis(); int w = 0; bool changed = false;
  for (int i = 0; i < nMeters; i++) {
    if (now - meters[i].lastMs < METER_TTL_MS) meters[w++] = meters[i];
    else changed = true;
  }
  nMeters = w;
  if (changed) saveDiscovered();
}
static void upsertMeter(const R900 &r, int rssi) {   // add or refresh a meter; evict the oldest if full
  uint32_t now = millis();
  for (int i = 0; i < nMeters; i++)
    if (meters[i].id == r.id) { meters[i].r = r; meters[i].rssi = rssi; meters[i].lastMs = now; return; }
  int slot;
  if (nMeters < MAX_METERS) slot = nMeters++;
  else { slot = 0; for (int i = 1; i < nMeters; i++) if (meters[i].lastMs < meters[slot].lastMs) slot = i; }
  meters[slot].id = r.id; meters[slot].r = r; meters[slot].rssi = rssi; meters[slot].lastMs = now;
  saveDiscovered();                   // a new/replaced meter id -> persist the discovered set
}
// advance rotation among the VISIBLE subset; record which meter is in focus (for the OLED + leak LED)
static void tickRotation() {
  pruneMeters();
  int idx[MAX_METERS]; gVisN = visibleMeters(idx);
  if (gVisN == 0) { curMeterIdx = -1; rotPos = 0; return; }
  if (rotPos >= gVisN) rotPos = 0;
  if (millis() - rotAt >= ROT_MS) { rotPos = (rotPos + 1) % gVisN; rotAt = millis(); }
  curMeterIdx = idx[rotPos];
}

// ---- watched frequency + auto-hop fallback ----
#define DECODE_TIMEOUT_MS   900000UL  // once a meter's been heard: 15 min silence -> hop. Covers the
                                      // ~12 min worst-case revisit (median ~2.3 min) without false-hopping.
#define DISCOVER_TIMEOUT_MS 180000UL  // hop every 3 min while actively discovering (no meter locked yet)
// How long to hold this channel before hopping:
//  - watch-all mode (nothing configured): keep sweeping every 3 min to surface EVERY meter around, even
//    after decodes — otherwise the first decode locks us on one window and we only see its traffic.
//  - a specific meter is watched: park & hold (15 min) once we've heard it, hop fast (3 min) until then.
static uint32_t hopTimeout() {
  if (watchAll()) return DISCOVER_TIMEOUT_MS;
  return (lastAnyMs == 0) ? DISCOVER_TIMEOUT_MS : DECODE_TIMEOUT_MS;
}
static float    watchFreq = 916.300f; // currently parked frequency (shown in the OLED header)
static float    goodFreq  = 0;        // last frequency that produced a decode (persisted in NVS)
static uint32_t lastDecodeRef = 0;    // millis() baseline for the decode-timeout (reset on boot/decode/hop)

// ---- channel productivity: survey ranking + runtime interferer detection ----
// The boot survey scores every window by duty; we hop in *cleanest-first* order (chanRank) instead of
// linearly, so discovery jumps to the productive ~916 region rather than crawling through the band's
// interferer zone (911-band survey flagged 913.5-914.5 as bursty-but-undecodable). Duty alone can't
// spot a *bursty* interferer (moderate average duty), so we also watch a runtime signal: bursts that
// keep arriving on a channel but never decode == interferer -> bail early and remember it.
#define N_WINDOWS 40
static float    chanRank[N_WINDOWS];  // survey windows, sorted ascending by duty (cleanest first)
static int      nRank = 0;
static uint32_t chanArriveMs = 0;     // millis() we parked on the current watchFreq
static int      chanNoDecode = 0;     // # bursts seen with NO decode since arriving on this channel
static int      chanDecodes  = 0;     // # decodes since arriving (>0 => proven-good, never bail)
#define BURST_BAIL_MS   45000UL       // give a channel this long, and...
#define BURST_BAIL_MIN  3             // ...if >=3 bursts arrived but none decoded, it's an interferer -> hop
// short memory of recently-bailed (interferer) channels so the ranked hop doesn't re-park on them
#define BAIL_MEM     4
#define BAIL_TTL_MS  600000UL         // 10 min
static float    bailFreq[BAIL_MEM];
static uint32_t bailAt[BAIL_MEM];
static int      bailPos = 0;
static bool isBailed(float f) {
  for (int i = 0; i < BAIL_MEM; i++)
    if (bailAt[i] && fabsf(bailFreq[i] - f) < 0.01f && millis() - bailAt[i] < BAIL_TTL_MS) return true;
  return false;
}
static void markBailed(float f) { bailFreq[bailPos] = f; bailAt[bailPos] = millis(); bailPos = (bailPos + 1) % BAIL_MEM; }
static void resetChanStats() { chanArriveMs = millis(); chanNoDecode = 0; chanDecodes = 0; }
// soft memory of just-tried windows so hops sweep DIFFERENT clean windows (cover the productive core in
// rank order) instead of snapping back to the single best one; entries expire after ~2 revisit periods.
#define VISIT_MEM    6
#define VISIT_TTL_MS 300000UL         // 5 min (~2x the ~2.3 min median revisit)
static float    visitFreq[VISIT_MEM];
static uint32_t visitAt[VISIT_MEM];
static int      visitPos = 0;
static bool isRecent(float f) {
  for (int i = 0; i < VISIT_MEM; i++)
    if (visitAt[i] && fabsf(visitFreq[i] - f) < 0.01f && millis() - visitAt[i] < VISIT_TTL_MS) return true;
  return false;
}
static void markVisited(float f) { visitFreq[visitPos] = f; visitAt[visitPos] = millis(); visitPos = (visitPos + 1) % VISIT_MEM; }

// ---- leak LED (the board's one user LED, GPIO25; the red power LED is hardwired = power indicator) ----
#define LEAK_LED_PIN  25              // LED_BUILTIN on this board (ttgo-lora32-v21new); active-high (HIGH = lit)
static Ticker  leakTicker;
static void    leakToggle() { static bool on = false; on = !on; digitalWrite(LEAK_LED_PIN, on); }
// 0=none -> off, 1=intermittent -> slow flash, 2=continuous/now -> fast flash
static void    setLeakLed(int leaknow) {
  leakTicker.detach();
  if      (leaknow == 1) leakTicker.attach_ms(1500, leakToggle);  // slow flash (1.5s) = intermittent leak
  else if (leaknow == 2) leakTicker.attach_ms(500, leakToggle);   // fast flash (0.5s) = definite leak now
  else                   digitalWrite(LEAK_LED_PIN, LOW);         // no leak -> off
}
// drive the leak LED from the meter currently in focus on the display (curMeterIdx).
// Guarded so we only re-arm the flash ticker when the leak state actually changes.
static void updateLeakLed() {
  static int last = -1;
  int want = (curMeterIdx >= 0) ? meters[curMeterIdx].r.leaknow : 0;
  if (want != last) { setLeakLed(want); last = want; }
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

// render one full OLED frame. showMeterTitle=false leaves the meter's id/nickname line blank
// (used for the brief rotation blink so the eye registers that the screen changed).
static void renderFrame(bool showMeterTitle) {
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
  if (curMeterIdx < 0) {
    // nothing in focus yet: listening (watch-all) or waiting for the watched meter(s)
    uint32_t el  = millis() - (watchAll() ? chanArriveMs : lastDecodeRef);  // countdown to the next channel hop
    uint32_t rem = (hopTimeout() > el) ? (hopTimeout() - el) / 1000 : 0;
    char c[26]; snprintf(c, sizeof(c), "New chan in %lu:%02lu",
                         (unsigned long)(rem / 60), (unsigned long)(rem % 60));
    if (watchAll()) {
      u8g2.drawStr(0, 30, "Listening for meters");
      u8g2.drawStr(0, 46, c);                                    // <-- countdown to the frequency change
      if (!wifi) u8g2.drawStr(0, 62, "2x RST = setup");          // OOBE discoverability hint
    } else {
      u8g2.drawStr(0, 27, gWatchN > 1 ? "Waiting for meters" : "Waiting for meter");
      char nm[20]; meterLabel(gPreferred ? gPreferred : gWatch[0].id, nm, sizeof(nm));
      u8g2.drawStr(0, 41, nm);
      u8g2.drawStr(0, 58, c);
    }
  } else {
    MeterSlot &m = meters[curMeterIdx];
    const char *nk = nickOf(m.id);
    // Static part (never blinks): the preferred '*' plus the "Meter: " label — shown for BOTH ids and
    // nicknames. Only the id/nickname VALUE blinks on rotation.
    char l[28];
    char stat[16]; snprintf(stat, sizeof(stat), "%sMeter: ", (gPreferred && m.id == gPreferred) ? "*" : "");
    u8g2.drawStr(0, 26, stat);
    if (showMeterTitle) {
      char val[16];
      if (nk) snprintf(val, sizeof(val), "%s", nk);
      else    snprintf(val, sizeof(val), "%u", (unsigned)m.id);
      u8g2.drawStr(u8g2.getStrWidth(stat), 26, val);            // id/nickname — blanked during the rotation blink
    }
    char ix[8]; snprintf(ix, sizeof(ix), "%d/%d", rotPos + 1, gVisN);
    u8g2.drawStr(128 - (int)strlen(ix) * 6, 26, ix);             // compact "2/5" indicator, top-right
    snprintf(l, sizeof(l), "Leak: %s", leakText(m.r.leak));      u8g2.drawStr(0, 38, l);
    snprintf(l, sizeof(l), "Now: %s", leaknowText(m.r.leaknow)); u8g2.drawStr(0, 50, l);
    uint32_t ago = (millis() - m.lastMs) / 1000;
    if (ago < 60) snprintf(l, sizeof(l), "Updated: %lus ago", (unsigned long)ago);
    else          snprintf(l, sizeof(l), "Updated: %lum %lus ago", (unsigned long)(ago / 60), (unsigned long)(ago % 60));
    u8g2.drawStr(0, 62, l);
  }
  u8g2.sendBuffer();
}

// draw the OLED. When the rotation advances to a DIFFERENT meter, briefly blank the id/nickname line
// first (a short flash) so it's obvious the screen changed before the new meter is written.
static void drawDisplay() {
  if (!oledPresent) return;
  static uint32_t shownId = 0;
  uint32_t curId = (curMeterIdx >= 0) ? meters[curMeterIdx].id : 0;
  bool blink = (curId && shownId && curId != shownId);   // rotated to a new meter (not first appearance)
  shownId = curId;
  if (blink) { renderFrame(false); delay(ROT_BLINK_MS); } // flash the label blank, then draw the new one
  renderFrame(true);
}

#ifdef SCREENSHOT
// DEV/DOCS ONLY (build env:screenshot): whenever the OLED image changes, dump the raw 1024-byte
// U8g2 framebuffer as hex over serial so experiments/capture_screens.py can rebuild it as a PNG.
// Not compiled into the normal firmware. Call this from every loop; it self-throttles + de-dupes.
static void maybeDumpScreen() {
  if (!oledPresent) return;
  static uint32_t prevHash = 1; static uint32_t lastMs = 0;
  if (millis() - lastMs < 250) return;                 // throttle
  uint8_t *b = u8g2.getBufferPtr();
  const int N = u8g2.getBufferTileWidth() * u8g2.getBufferTileHeight() * 8;   // 16*8*8 = 1024
  uint32_t h = 2166136261u; for (int i = 0; i < N; i++) h = (h ^ b[i]) * 16777619u;  // FNV-1a
  if (h == prevHash) return;                           // screen unchanged since last dump
  prevHash = h; lastMs = millis();
  Serial.printf("---SCREEN %d %d---\n", 128, 64);
  for (int i = 0; i < N; i++) Serial.printf("%02x", b[i]);
  Serial.println("\n---ENDSCREEN---");
}
#endif

// shown on the OLED while the config portal is open (double-tap RST / FORCE_SETUP)
static void portalOnOLED() {
  if (!oledPresent) return;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "Setup mode");
  u8g2.drawHLine(0, 15, 128);
  u8g2.drawStr(0, 30, "Join WiFi:");
  u8g2.drawStr(6, 42, CFG_AP_NAME);
  u8g2.drawStr(0, 58, "then open 192.168.4.1");
  u8g2.sendBuffer();
}

// Wipe all persisted config + data (NVS r900 + drd) and stored WiFi creds, then reboot to OOBE state.
static void factoryReset() {
  Serial.println("FACTORY RESET — clearing stored config + data, rebooting");
  if (oledPresent) {
    u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 32, "Factory reset...");
    u8g2.drawStr(0, 48, "erasing settings");
    u8g2.sendBuffer();
  }
  Preferences p;
  p.begin("r900", false); p.clear(); p.end();   // blank; loadConfig has no defaults to fall back to
  p.begin("drd",  false); p.clear(); p.end();
  WiFi.disconnect(true, true);     // also erase stored WiFi credentials from NVS
  delay(600);
  ESP.restart();
}

// Persist the discovered-meters table (id + signal) so the config pick-list survives reboots.
static void saveDiscovered() {
  struct __attribute__((packed)) D { uint32_t id; uint8_t rssi; } blob[MAX_METERS];
  for (int i = 0; i < nMeters; i++) { blob[i].id = meters[i].id; blob[i].rssi = (uint8_t)meters[i].rssi; }
  Preferences p; p.begin("r900", false);
  if (nMeters) p.putBytes("disc", blob, nMeters * sizeof(D)); else p.remove("disc");
  p.end();
}
// Load the persisted discovered list into the live table so the setup pick-list shows prior
// discoveries immediately; live decodes during setup then refresh/extend it.
static void loadDiscovered() {
  struct __attribute__((packed)) D { uint32_t id; uint8_t rssi; } blob[MAX_METERS];
  Preferences p; p.begin("r900", true);
  size_t sz = p.getBytesLength("disc"); int n = 0;
  if (sz && sz <= sizeof(blob)) { p.getBytes("disc", blob, sz); n = sz / sizeof(D); }
  p.end();
  nMeters = 0;
  uint32_t now = millis();
  for (int i = 0; i < n && i < MAX_METERS; i++) {
    meters[i].id = blob[i].id; meters[i].rssi = blob[i].rssi; meters[i].lastMs = now;
    memset(&meters[i].r, 0, sizeof(meters[i].r)); meters[i].r.id = blob[i].id;
    nMeters++;
  }
}
// ===================================================================================================
// Custom config portal: serves a single-page app + JSON API from our own WebServer (replaces
// WiFiManager). Runs only in setup mode (double-tap RST / FORCE_SETUP). Save persists WITHOUT
// rebooting; Apply reboots. Reachable on the SoftAP (192.168.4.1) and the home IP if creds work.
// ===================================================================================================
static WebServer     webServer(80);
static DNSServer     dnsServer;
static volatile bool gApply = false, gFactory = false;

// Placeholder page (the polished SPA from experiments/ui/redesign-device.html gets embedded next).
#include "portal_page.h"   // PROGMEM PORTAL_PAGE, auto-generated from experiments/ui/redesign-device.html

// All config comes from NVS (set via the portal). No compile-time defaults: a fresh or factory-reset
// device is blank (empty WiFi/MQTT, watch-all) until the user configures it in the setup portal.
static void loadConfig() {
  prefs.begin("r900", false);
  prefs.getString("ssid",   "").toCharArray(cfgSsid,   sizeof(cfgSsid));
  prefs.getString("wpass",  "").toCharArray(cfgWpass,  sizeof(cfgWpass));
  prefs.getString("server", "").toCharArray(cfgServer, sizeof(cfgServer));
  cfgPort = prefs.getUShort("port", 1883);
  prefs.getString("user",   "").toCharArray(cfgUser,   sizeof(cfgUser));
  prefs.getString("pass",   "").toCharArray(cfgPass,   sizeof(cfgPass));
  prefs.getString("topic", "r900_meter").toCharArray(cfgTopic, sizeof(cfgTopic));
  if (cfgTopic[0] == '\0') strcpy(cfgTopic, "r900_meter");
  buildTopics();
  diagEnabled = prefs.getBool("diag", false);
  if (prefs.isKey("watchn")) {
    gWatchN = prefs.getInt("watchn", 0); if (gWatchN > MAX_WATCH) gWatchN = MAX_WATCH;
    if (gWatchN > 0) prefs.getBytes("watch", gWatch, gWatchN * sizeof(Watch));
    gPreferred = prefs.getUInt("pref", 0);
  } else { gWatchN = 0; gPreferred = 0; }             // never configured -> watch all
  prefs.end();
}

// Connect STA with stored creds (standalone-first). Returns whether connected.
static bool connectWifi(uint32_t timeoutMs) {
  if (cfgSsid[0] == '\0') return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSsid, cfgWpass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(150);
  WiFi.setAutoReconnect(true);
  return WiFi.status() == WL_CONNECTED;
}

static void apJson(JsonArray &aps, int n) {
  struct AP { String ssid; int rssi; bool enc; } list[24]; int m = 0;
  for (int i = 0; i < n && m < 24; i++) {            // dedupe by SSID, keep the strongest signal
    String ss = WiFi.SSID(i); if (!ss.length()) continue;   // skip hidden networks
    int found = -1; for (int j = 0; j < m; j++) if (list[j].ssid == ss) { found = j; break; }
    if (found >= 0) { if (WiFi.RSSI(i) > list[found].rssi) list[found].rssi = WiFi.RSSI(i); continue; }
    list[m].ssid = ss; list[m].rssi = WiFi.RSSI(i); list[m].enc = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN); m++;
  }
  for (int i = 1; i < m; i++) {                       // insertion-sort strongest first
    AP k = list[i]; int j = i - 1;
    while (j >= 0 && list[j].rssi < k.rssi) { list[j + 1] = list[j]; j--; }
    list[j + 1] = k;
  }
  for (int i = 0; i < m && i < 16; i++) {
    JsonObject a = aps.createNestedObject();
    a["ssid"] = list[i].ssid; a["rssi"] = list[i].rssi; a["enc"] = list[i].enc;
  }
}
// GET /api/state — config + AP scan + discovered/watched meters (passwords as "set" flags only)
static void apiState() {
  DynamicJsonDocument d(4096);
  d["ap"]   = WiFi.isConnected() ? WiFi.SSID() : String("");
  d["ip"]   = WiFi.isConnected() ? WiFi.localIP().toString() : String("");
  d["ssid"] = cfgSsid;
  d["wifiPwSet"] = (cfgWpass[0] != '\0');
  d["server"] = cfgServer; d["port"] = cfgPort; d["user"] = cfgUser;
  d["mqttPwSet"] = (cfgPass[0] != '\0'); d["topic"] = cfgTopic; d["diag"] = diagEnabled;
  int n = WiFi.scanComplete();
  if (n < 0) WiFi.scanNetworks(true);          // kick async scan if none ready
  JsonArray aps = d.createNestedArray("aps");
  apJson(aps, n < 0 ? 0 : n);
  // meters: merge discovered (NVS) + watched
  // Meters come from the live table (meters[]): in setup mode it's seeded from NVS by loadDiscovered()
  // and refreshed by the live discovery loop, so the pick-list updates as new meters are heard.
  JsonArray ms = d.createNestedArray("meters");
  bool added[MAX_WATCH] = {false};
  for (int i = 0; i < nMeters; i++) {
    JsonObject m = ms.createNestedObject();
    m["id"] = meters[i].id; m["dbm"] = -(int)meters[i].rssi / 2;
    int wi = watchIdx(meters[i].id);
    m["nick"] = (wi >= 0) ? gWatch[wi].nick : ""; m["watched"] = (wi >= 0);
    m["preferred"] = (meters[i].id == gPreferred);
    if (wi >= 0) added[wi] = true;
  }
  for (int i = 0; i < gWatchN; i++) {
    if (added[i]) continue;
    JsonObject m = ms.createNestedObject();
    m["id"] = gWatch[i].id; m["dbm"] = nullptr; m["nick"] = gWatch[i].nick;   // null = not heard yet
    m["watched"] = true; m["preferred"] = (gWatch[i].id == gPreferred);
  }
  String out; serializeJson(d, out);
  webServer.send(200, "application/json", out);
}
// GET /api/scan — fresh Wi-Fi scan
static void apiScan() {
  WiFi.scanNetworks(true); delay(50);
  int n = WiFi.scanComplete();
  DynamicJsonDocument d(2048); JsonArray aps = d.createNestedArray("aps");
  apJson(aps, n < 0 ? 0 : n);
  String out; serializeJson(d, out);
  webServer.send(200, "application/json", out);
}
// POST /api/save — persist config; blank passwords keep the stored value; never reboots
static void apiSave() {
  DynamicJsonDocument d(3072);
  if (deserializeJson(d, webServer.arg("plain"))) { webServer.send(400, "application/json", "{\"ok\":false}"); return; }
  if (d.containsKey("ssid")) strncpy(cfgSsid, d["ssid"] | "", sizeof(cfgSsid) - 1);
  const char *wp = d["wifiPw"] | ""; if (wp[0]) strncpy(cfgWpass, wp, sizeof(cfgWpass) - 1);  // blank = keep
  strncpy(cfgServer, d["server"] | "", sizeof(cfgServer) - 1);
  cfgPort = d["port"] | 1883;
  strncpy(cfgUser, d["user"] | "", sizeof(cfgUser) - 1);
  const char *mp = d["mqttPw"] | ""; if (mp[0]) strncpy(cfgPass, mp, sizeof(cfgPass) - 1);     // blank = keep
  strncpy(cfgTopic, d["topic"] | "r900_meter", sizeof(cfgTopic) - 1);
  if (cfgTopic[0] == '\0') strcpy(cfgTopic, "r900_meter");
  diagEnabled = d["diag"] | false;
  gWatchN = 0;
  for (JsonObject w : d["watch"].as<JsonArray>()) {
    if (gWatchN >= MAX_WATCH) break;
    uint32_t id = w["id"] | 0; if (!id) continue;
    gWatch[gWatchN].id = id;
    strncpy(gWatch[gWatchN].nick, w["nick"] | "", NICK_LEN); gWatch[gWatchN].nick[NICK_LEN] = 0;
    gWatchN++;
  }
  gPreferred = d["preferred"] | 0;
  if (gPreferred && watchIdx(gPreferred) < 0 && gWatchN < MAX_WATCH) {
    gWatch[gWatchN].id = gPreferred; gWatch[gWatchN].nick[0] = 0; gWatchN++;
  }
  Preferences p; p.begin("r900", false);
  p.putString("ssid", cfgSsid); p.putString("wpass", cfgWpass);
  p.putString("server", cfgServer); p.putUShort("port", cfgPort);
  p.putString("user", cfgUser); p.putString("pass", cfgPass);
  p.putString("topic", cfgTopic); p.putBool("diag", diagEnabled);
  p.putInt("watchn", gWatchN);
  if (gWatchN) p.putBytes("watch", gWatch, gWatchN * sizeof(Watch)); else p.remove("watch");
  p.putUInt("pref", gPreferred);
  p.end();
  buildTopics();
  webServer.send(200, "application/json", "{\"ok\":true}");
}
static void apiApply()   { webServer.send(200, "application/json", "{\"ok\":true}"); gApply = true; }
static void apiFactory() { webServer.send(200, "application/json", "{\"ok\":true}"); gFactory = true; }
// POST /api/testwifi {ssid, wifiPw} — try the creds live (blank pw = test the stored one), report
// connected/failed WITHOUT rebooting, then drop STA so the portal stays AP-only.
static void apiTestWifi() {
  DynamicJsonDocument d(512);
  if (deserializeJson(d, webServer.arg("plain"))) { webServer.send(400, "application/json", "{\"ok\":false}"); return; }
  const char *ssid = d["ssid"] | "";
  const char *pw   = d["wifiPw"] | "";
  if (!ssid[0]) { webServer.send(200, "application/json", "{\"connected\":false,\"error\":\"no ssid\"}"); return; }
  const char *pass = pw[0] ? pw : cfgWpass;              // blank = test the stored password
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(150);
  bool ok = (WiFi.status() == WL_CONNECTED);
  String ip = ok ? WiFi.localIP().toString() : String("");
  WiFi.disconnect(false, false);                        // drop STA, keep the AP + portal up
  DynamicJsonDocument r(256);
  r["connected"] = ok; r["ip"] = ip;
  String out; serializeJson(r, out);
  webServer.send(200, "application/json", out);
}
// Captive-portal landing: possibly a single small screen that steers the user to a FULL browser, because the
// OS captive-portal mini-window is fixed/cramped and a poor fit for the rich config page.
static const char INTRO_PAGE[] PROGMEM = R"INTRO(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>R900 Reader Setup</title>
<style>body{margin:0;background:#0f141b;color:#e6e9ef;font-family:-apple-system,Segoe UI,Roboto,sans-serif;
display:flex;min-height:100vh;align-items:center;justify-content:center;text-align:center;padding:24px;box-sizing:border-box}
.c{max-width:380px}h1{font-size:1.4em;margin:0 0 4px}.s{color:#8b94a7;margin:0 0 20px;font-size:.95em}
.u{display:block;background:#10b981;color:#04241b;border-radius:12px;padding:16px;font-family:ui-monospace,Menlo,monospace;
font-size:1.2em;font-weight:700;margin:16px 0;cursor:pointer;user-select:all}
.n{color:#8b94a7;font-size:.86em;line-height:1.55}a.x{color:#7aa2f7;font-size:.85em}</style></head><body><div class=c>
<h1>R900 Reader — Setup</h1><p class=s>Connected to the setup network.</p>
<p>If this pop-up is too small for setup, open this address in your phone or computer's <b>web browser</b>:</p>
<div class=u id=addr onclick="cp()">http://192.168.4.1/setup</div>
<p class=n>Tip: tap the address to copy it, then paste it into your browser's address bar.</p>
<p class=n><a class=x href="http://192.168.4.1/setup">…or continue in this window anyway</a></p>
<script>function cp(){var t='http://192.168.4.1/setup',e=document.getElementById('addr');
try{navigator.clipboard&&navigator.clipboard.writeText(t)}catch(x){}
try{var r=document.createRange();r.selectNodeContents(e);var s=getSelection();s.removeAllRanges();s.addRange(r);document.execCommand('copy');s.removeAllRanges()}catch(x){}
var o=e.textContent;e.textContent='Copied ✓';setTimeout(function(){e.textContent=o},1200);}</script>
</div></body></html>)INTRO";

static void handleIntro() { webServer.send_P(200, "text/html", INTRO_PAGE); }
static void handleRoot()  { webServer.send_P(200, "text/html", PORTAL_PAGE); }

// radio helpers (defined later) used by the setup-mode discovery loop
static void retune(float f);
static void measureNoise();
static void hopToNextFreq();
static void saveGoodFreq(float f);
static int  captureOnce(R900 *r, int *rssiOut, uint32_t maxIdleMs);
static void loadDiscovered();
#define SETUP_HOP_MS 120000UL     // setup mode: hop to another window if no meter heard for 2 min

// Blocking config portal (AP-only). ALSO discovers meters while open: the SX1276 (SPI) decode loop
// runs concurrently with the WiFi AP + web server (independent radios), so the meter pick-list
// populates live and the search hops frequencies to keep looking. Reboots on apply/factory.
static void runPortal() {
  portalOnOLED();
  watchFreq = (goodFreq >= 911.0f && goodFreq <= 920.0f) ? goodFreq : 916.0f;   // known-good, else productive default
  retune(watchFreq);
  measureNoise();
  loadDiscovered();                                 // show meters heard on earlier runs immediately
  WiFi.mode(WIFI_AP_STA);                            // AP for the portal; STA up only for the network scan
  WiFi.softAP(CFG_AP_NAME);
  WiFi.scanNetworks(true);
  dnsServer.start(53, "*", WiFi.softAPIP());
  webServer.on("/",            HTTP_GET,  handleIntro);   // captive landing -> steer to a full browser
  webServer.on("/setup",       HTTP_GET,  handleRoot);    // the full config SPA
  webServer.on("/api/state",   HTTP_GET,  apiState);
  webServer.on("/api/scan",    HTTP_GET,  apiScan);
  webServer.on("/api/save",    HTTP_POST, apiSave);
  webServer.on("/api/apply",   HTTP_POST, apiApply);
  webServer.on("/api/factory",  HTTP_POST, apiFactory);
  webServer.on("/api/testwifi", HTTP_POST, apiTestWifi);
  // OS captive-detection probes (hotspot-detect.html, generate_204, ncsi.txt, …) get a 302 to the
  // intro — a redirect triggers the "Sign in to network" banner more reliably than a 200 body.
  webServer.onNotFound([]() {
    webServer.sendHeader("Location", "http://192.168.4.1/", true);
    webServer.send(302, "text/plain", "");
  });
  webServer.begin();
  Serial.printf("config portal up @ 192.168.4.1 (setup: http://192.168.4.1/setup) — discovering on %.3f MHz\n", watchFreq);
  uint32_t start = millis(), lastHeard = millis();
  for (;;) {
    dnsServer.processNextRequest();
    webServer.handleClient();
#ifdef SCREENSHOT
    maybeDumpScreen();
#endif
    R900 r; int rssi;
    if (captureOnce(&r, &rssi, 250) == 1) {          // short idle keeps the web server responsive
      upsertMeter(r, rssi); saveGoodFreq(watchFreq); lastHeard = millis();
    }
    if (millis() - lastHeard > SETUP_HOP_MS) { hopToNextFreq(); lastHeard = millis(); }  // keep searching
    if (gFactory) factoryReset();                  // never returns
    if (gApply) { delay(300); ESP.restart(); }
#ifndef FORCE_SETUP
    if (millis() - start > 600000UL) ESP.restart();  // 10-min safety: resume metering if abandoned
#endif
  }
}

// Load saved config from NVS, then either run the setup portal (forced) or connect with stored creds
// and run locally. Re-open the portal anytime via double-tap RST.
static void runProvisioning(bool forcePortal) {
  loadConfig();
  if (forcePortal) { runPortal(); return; }   // runPortal never actually returns (reboots on apply/factory)
  bool ok = connectWifi(9000);                 // standalone-first: stored creds, else run local
  Serial.printf("WiFi %s  watch=%d pref=%u  mqtt=%s:%u\n",
                ok ? WiFi.localIP().toString().c_str() : "(offline)",
                gWatchN, (unsigned)gPreferred, cfgServer, cfgPort);
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
  float rf[N_WINDOWS]; int rd[N_WINDOWS]; nRank = 0;                   // (freq,duty) pairs to rank
  for (float f = SCAN_LO; f <= SCAN_HI + 0.001f && nRank < N_WINDOWS; f += SCAN_STEP) {
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
    rf[nRank] = f; rd[nRank] = dutyPM; nRank++;
    // lowest duty wins; on a tie prefer the window nearest PREF_FREQ (the meter's productive region,
    // so a uniformly-clean band parks near 916 MHz rather than the arbitrary band edge)
    if (dutyPM < bestDuty - 5 ||
        (abs(dutyPM - bestDuty) <= 5 && fabsf(f - PREF_FREQ) < fabsf(best - PREF_FREQ))) {
      bestDuty = dutyPM; bestNoise = nf; best = f;
    }
  }
  // rank windows cleanest-first (insertion sort by duty; tie -> nearer PREF_FREQ) for survey-ordered hopping
  for (int i = 0; i < nRank; i++) {
    int m = i;
    for (int j = i + 1; j < nRank; j++)
      if (rd[j] < rd[m] || (rd[j] == rd[m] && fabsf(rf[j] - PREF_FREQ) < fabsf(rf[m] - PREF_FREQ))) m = j;
    float tf = rf[i]; rf[i] = rf[m]; rf[m] = tf; int td = rd[i]; rd[i] = rd[m]; rd[m] = td;
    chanRank[i] = rf[i];
  }
  watchFreq = best;
  Serial.printf("=> watching %.3f MHz (duty=%d.%d%%, cleanest window); ranked %d windows\n",
                watchFreq, bestDuty / 10, bestDuty % 10, nRank);
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

// quick live duty check on a candidate window (per-mille active over 200ms)
static int liveDuty(float f) {
  retune(f);
  SPI.beginTransaction(spiSet);
  long acc = 0; for (int i = 0; i < 1000; i++) acc += fastRssi();
  int nf = acc / 1000, trig = nf - 30;
  uint32_t below = 0, tot = 0, t0 = millis();
  while (millis() - t0 < 200) { if (fastRssi() < trig) below++; tot++; }
  SPI.endTransaction();
  return (int)(below * 1000ULL / tot);
}

// hop to the next window in survey-productivity order (cleanest first), skipping the current channel,
// recently-bailed interferers, and windows that fail a live duty check. Falls back to linear stepping
// if the survey ranking is unavailable. Chosen over linear stepping so discovery jumps to the productive
// region instead of crawling into the band's interferer zone.
static void hopToNextFreq() {
  int skipped = 0; float pick = -1;
  markVisited(watchFreq);                                 // don't snap straight back to the window we're leaving
  if (nRank > 0) {
    // walk ranked windows best-first; skip current, interferers, and just-tried windows
    for (int i = 0; i < nRank; i++) {
      float f = chanRank[i];
      if (fabsf(f - watchFreq) < 0.01f) continue;
      if (isBailed(f)) { skipped++; continue; }
      if (isRecent(f)) continue;                          // tried lately — give the next-best window a turn
      if (liveDuty(f) <= 200) { pick = f; break; }        // clean enough (<=20% duty) — park here
      skipped++;
    }
    if (pick < 0)                                         // all top windows tried/bailed: best clean non-bailer
      for (int i = 0; i < nRank; i++) {
        float f = chanRank[i];
        if (fabsf(f - watchFreq) < 0.01f || isBailed(f)) continue;
        if (liveDuty(f) <= 200) { pick = f; break; }
      }
    if (pick < 0) pick = chanRank[0];                     // everything dirty: fall back to the cleanest ranked
  } else {                                                // no survey ranking: legacy linear step
    const int windows = (int)((SCAN_HI - SCAN_LO) / SCAN_STEP) + 1;
    float f = watchFreq;
    for (int tries = 0; tries < windows; tries++) {
      f += SCAN_STEP; if (f > SCAN_HI + 0.001f) f = SCAN_LO;
      if (liveDuty(f) <= 200) break;
    }
    pick = f;
  }
  watchFreq = pick;
  retune(watchFreq);
  measureNoise();
  lastDecodeRef = millis();
  resetChanStats();
  Serial.printf("=> hopped to %.3f MHz (ranked; skipped %d interferer/dirty)\n", watchFreq, skipped);
}

// Battery voltage via GPIO35 (VBAT/2 through 100K+100K divider). Average 16 samples for noise.
// Power source heuristic: TP4054 holds VBAT at ~4.2V while charging from USB; below 4100mV = battery only.
static void publishBatt() {
  if (!mqtt.connected()) return;
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(BATT_PIN);
  uint32_t half_mv = sum / 16;
  uint32_t vbat_mv = half_mv * 2;
  int pct = constrain((int)((vbat_mv - 3300) * 100 / (4200 - 3300)), 0, 100);
  const char *src = (vbat_mv > 4100) ? "usb" : "batt";
  char p[64];
  snprintf(p, sizeof(p), "{\"pct\":%d,\"mv\":%lu,\"src\":\"%s\"}", pct, (unsigned long)vbat_mv, src);
  mqtt.publish(battTopic, p, true);
}

// publish the diagnostic JSON (watched freq, last-known-good, seconds since last decode)
static void publishDiag() {
  if (!diagEnabled || !mqtt.connected()) return;
  char p[160];
  snprintf(p, sizeof(p), "{\"freq\":%.3f,\"good\":%.3f,\"since_s\":%lu}",
           watchFreq, goodFreq, (unsigned long)((millis() - lastDecodeRef) / 1000));
  mqtt.publish(diagTopic, p, true);
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

  // Radio up early so BOTH normal metering AND the setup portal can decode/discover meters.
  SPI.begin(5, 19, 27, 18);
  // RxBw WIDE (250kHz max): RSSI must track R900's 30us carrier-off gaps (narrower is too slow).
  int st = radio.beginFSK(916.300, 4.8, 5.0, 250.0, 10, 16); // freq,br,freqDev,rxBw,pwr,preamble
  Serial.printf("beginFSK=%d\n", st);
  radio.setOOK(true);
  radio.setRSSIConfig(RADIOLIB_SX127X_RSSI_SMOOTHING_SAMPLES_2); // fastest tracking
  st = radio.receiveDirect();
  Serial.printf("receiveDirect=%d\n", st);
  { Preferences p; p.begin("r900", true); goodFreq = p.getFloat("goodfreq", 0.0f); p.end(); }

  // Double-tap-RST detector (flag in NVS so it survives the EN-pin reset). A 2nd reset within
  // DRD_WINDOW_MS leaves the flag armed -> this boot forces the WiFi/MQTT setup portal.
  Preferences drd;
  drd.begin("drd", false);
  bool forcePortal = drd.getBool("armed", false);
#ifdef FORCE_SETUP
  forcePortal = true;   // dev aid: force the setup portal every boot (for autonomous browser testing)
#endif
  if (forcePortal) {
    drd.putBool("armed", false);               // consume the trigger
    drd.end();
    Serial.println("setup mode -> config portal (with live meter discovery)");
    runProvisioning(true);                      // -> runPortal(); never returns (reboots on apply/factory)
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

  // (radio already initialised above, before the portal branch)
  surveyChannels();   // self-calibrate: pick the cleanest window in the R900 band (portable to any site)

  // prefer the last frequency that actually decoded a meter, if we have one (faster reacquisition)
  if (goodFreq >= SCAN_LO && goodFreq <= SCAN_HI) {
    watchFreq = goodFreq;
    retune(watchFreq);
    Serial.printf("=> parking on last-known-good %.3f MHz\n", watchFreq);
  }

  measureNoise();                  // noise floor + trigger threshold on the chosen channel
  lastDecodeRef = millis();        // start the decode-timeout clock
  resetChanStats();                // start per-channel interferer-bail stats

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
    if (millis() - lastBeat > 300000) { mqtt.publish(availTopic, "online", true); lastBeat = millis(); }   // 5-min heartbeat
    if (diagEnabled && millis() - lastDiag > 30000) { publishDiag(); lastDiag = millis(); }                // 30s diagnostics
    if (millis() - lastBatt > BATT_MS) { publishBatt(); lastBatt = millis(); }                            // 5-min battery
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
      if ((n & 0x7FFF) == 0 && millis() - waitStart > maxIdleMs) { SPI.endTransaction(); return -1; }  // ~130us check granularity (keeps the setup portal responsive at short maxIdle)
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
#define COV_DWELL_MS 900000UL     // 15 min per window (~1.5 R900 hop cycles) -> full 911-920 sweep in ~9h

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
#ifdef SCREENSHOT
  maybeDumpScreen();
#endif
  R900 r; int rssi;
  int res = captureOnce(&r, &rssi, 3000);     // returns within ~3s if idle so MQTT/OLED stay serviced
  if (res == 0) chanNoDecode++;               // burst arrived here but didn't decode
  // interferer bail: if this channel keeps producing bursts that never decode, it's an interferer (not
  // a meter mid-revisit-gap) — hop early and remember it, instead of dwelling the full hop timeout.
  if (chanDecodes == 0 && chanNoDecode >= BURST_BAIL_MIN && millis() - chanArriveMs >= BURST_BAIL_MS) {
    Serial.printf("interferer: %d bursts, 0 decodes in %lus on %.3f MHz — bailing\n",
                  chanNoDecode, (unsigned long)((millis() - chanArriveMs) / 1000), watchFreq);
    markBailed(watchFreq); hopToNextFreq();
    tickRotation(); updateLeakLed(); drawDisplay();
    return;
  }
  if (res < 0) {                              // idle: refresh display, check the hop timer
    tickRotation(); updateLeakLed();
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 10000) { drawDisplay(); lastDraw = millis(); }
    // watch-all: measure time PARKED (chanArriveMs) so continuous decodes don't pin us to one window;
    // watching a specific meter: measure time since its last decode (lastDecodeRef) to park & hold.
    uint32_t held = millis() - (watchAll() ? chanArriveMs : lastDecodeRef);
    if (held > hopTimeout()) {
      Serial.printf("%s %lu min on %.3f — hopping to sweep for more meters\n",
                    watchAll() ? "discovery:" : "no decode for", hopTimeout() / 60000, watchFreq);
      hopToNextFreq();
    }
    return;
  }
  if (res == 1) {
    chanDecodes++;                            // this channel is productive — never interferer-bail it
    upsertMeter(r, rssi);                     // discovery: record EVERY meter (before the watch filter)
    publishReading(r, rssi);                  // reports watched meters only (all if the watch-list is empty)
    lastAnyMs = millis();                     // any decode -> RX-active indicator
    // The hop / good-freq clock tracks the PREFERRED meter (or any decode if none is set): if the
    // preferred meter isn't heard on this channel, we time out and hop even while others still decode.
    if (gPreferred == 0 || r.id == gPreferred) { lastDecodeRef = millis(); saveGoodFreq(watchFreq); }
    tickRotation(); updateLeakLed(); drawDisplay();
  } else {
    delay(800);                               // burst seen but no decode -> brief backoff
  }
}
#endif
#endif
