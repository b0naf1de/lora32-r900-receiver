# Home Assistant integration

The firmware publishes everything via **MQTT discovery**, so the core sensors appear with **zero HA
configuration** (as long as the MQTT integration is set up and discovery is on, which is the default).

## What appears automatically

When the board hears a meter `<id>`, it publishes retained discovery configs that create a device
**`Neptune R900 (LoRa32) <id>`** with:

| Entity | Class | Notes |
|---|---|---|
| `sensor.neptune_r900_lora32_<id>_consumption` | `water` / `total_increasing` | gallons (already ÷10 from the raw tenths) |
| `sensor.neptune_r900_lora32_<id>_leak_now` | — | `None` / `Intermittent` / `Continuous` |
| `sensor.neptune_r900_lora32_<id>_rssi` | `signal_strength` | dBm-ish raw RSSI at decode |

A separate device **`R900 LoRa32 Reader`** exposes a `connectivity` binary sensor
(`...reader_online`) driven by the MQTT Last-Will + 5-minute retained heartbeat on
`lora32r900/availability`. It flips to **off** within ~5–6 min of the board losing power/WiFi.

## Optional: "is it still reading?" + offline alerts

The board being *online* (heartbeat) is not the same as it still *decoding* your meter. These helpers
add a data-freshness layer. Create them in the HA UI (Settings → Devices & Services → Helpers) or
via your preferred method:

1. **`input_datetime.water_meter_last_reading`** (date **and** time) — stamped on every decode.

2. **Automation: record last reading** — trigger on MQTT topic `lora32r900/+/state`, action
   `input_datetime.set_datetime` with `timestamp: "{{ now().timestamp() }}"` targeting the helper above.

3. **Template binary sensor `water_meter_reader_active`** (`connectivity`) — "have we decoded recently?":
   ```jinja
   {{ (now().timestamp() - (state_attr('input_datetime.water_meter_last_reading','timestamp') | float(0))) < 3600 }}
   ```
   (1-hour staleness window; widen/narrow to taste — meters that transmit less often need a larger window.)

4. **Automation: offline email alert** — notify when either the board drops *or* the data goes stale:
   - **board offline:** `...reader_online` is `off` for 2 min
   - **data stale:** `water_meter_reader_active` turns `off` *while* `...reader_online` is still `on`
     (distinguishes "board fine, meter not heard" from "board down")
   - **recovered:** `water_meter_reader_active` turns `on`

   Route the notify action to whatever notifier you use (email, mobile app, etc.).

## Dashboard

A simple Entities card pulling these together:

```yaml
type: entities
title: LoRa32 Meter Reader
entities:
  - entity: binary_sensor.r900_lora32_reader_reader_online
    name: Reader online
  - entity: binary_sensor.water_meter_reader_active
    name: Recent data (<1h)
  - entity: input_datetime.water_meter_last_reading
    name: Last reading
  - entity: sensor.neptune_r900_lora32_<id>_consumption
    name: Consumption
  - entity: sensor.neptune_r900_lora32_<id>_rssi
    name: Signal RSSI
```

## MQTT topics (reference)

| Topic | Retained | Payload |
|---|---|---|
| `lora32r900/<id>/state` | yes | JSON: `consumption`, `leaknow`, `backflow`, `rssi` |
| `lora32r900/availability` | yes | `online` / `offline` (LWT + 5-min heartbeat) |
| `homeassistant/.../config` | yes | discovery configs |
