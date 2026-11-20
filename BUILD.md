# Time Machine Build and First Weather Upload

Firmware 0.6.0 includes optional OpenWeather over verified HTTPS. The configuration API reports the running firmware in the read-only, nonpersistent `version` field; schema remains 1. Weather is disabled until owAppKey is set and both coordinates are present; no requests are made without them. TLS requires synchronized time. The main LED display remains a clock. OLED clock updates remain disabled; transient D() messages still age off normally.

## Arduino IDE settings

Open `Firmware/TimeMachine/TimeMachine.ino`.

| Setting | Value |
| --- | --- |
| ESP32 Arduino core | 3.3.12 (tested) |
| Board | XIAO_ESP32C6 |
| Flash size | 4 MB |
| USB CDC On Boot | Enabled |
| Partition Scheme | **Time Machine 4MB OTA (1.75MB APP/384KB FS)** |
| Erase All Flash Before Sketch Upload | Disabled |
| Serial Monitor | 115200 baud |

The custom partition choice is installed by this task using an additive boards.local.txt entry and matching CSV in the installed core. If the option is not visible, choose Tools > Reload Board Data, then reopen Tools > Partition Scheme. Reloading the IDE window alone may leave cached board definitions in place. Existing partition menu choices/defaults are unchanged. Reinstall after a core update using the command below from the project root:

```sh
python3 tools/arduino/install-ota-layout.py /Users/mspencer/Library/Arduino15/packages/esp32/hardware/esp32/3.3.12
```

The sketch-local partitions.csv matches the menu table. Both application slots are 1,835,008 bytes (1.75 MiB). LittleFS is 393,216 bytes (384 KiB). NVS and OTA metadata offsets are unchanged. The previous default slot was too small once HTTPS was linked; an empty weather key disables runtime requests but does not remove TLS code from the image. Do not select the built-in No OTA/Huge APP schemes.

## LittleFS website transfers

Use `tools/lfs-download` to fetch and unpack the board's complete filesystem,
then edit its `www/` folder and use `tools/lfs-upload` to pack and upload it.
Raw .bin images are removed after successful transfers; use --keep-image to retain them.
Upload replaces the partition with the
local folder's contents, so retain `config.json` to preserve saved settings.
Both tools check the board's partition table against the sketch CSV first.
See [LittleFS transfer tools](tools/README.md) for commands, dependencies, and
an offline `--pack-only` option. Close the Serial Monitor before a transfer.

## Shared libraries

Sketchbook entries link to sibling MDS-Libraries: MDS_Config 0.7.0, MDS_Display 0.1.0, MDS_WiFi 0.3.0, MDS_HTTP 0.4.0, MDS_Time 0.2.0, and MDS_Weather 0.4.0. These shared folders must remain alongside the project. Other installed dependencies used for validation are ArduinoJson 7.4.3, WiFiManager 2.0.17, Adafruit GFX 1.13.0, Adafruit SSD1306 2.5.17, and Adafruit BusIO. Existing projects need not migrate.

Time requires 0.2.0; HTTP 0.4.0 adds typed listener/upload states; Weather requires 0.4.0 for its accepted-sample
update callback. These releases include the readiness-callback interfaces used by this firmware. Their 0.1.0 interfaces are incompatible; follow each library's
migration notes when updating another consumer.

## First upload with the new layout

The filesystem moves from 0x290000 to 0x390000. Existing application settings do not automatically move. Use USB for the first layout change; an ordinary OTA firmware update does not replace the partition table. Wi-Fi credentials remain in NVS; do not erase the whole flash.

1. While the old firmware is still running, back up its config API. Use the current WiFi IP printed by Serial; 192.168.0.120 is the earlier observed address.

   ```sh
   python3 tools/arduino/settings.py backup http://192.168.0.120 "/Users/mspencer/Documents/ChatGPT/Time Machine/settings-before-weather.json"
   ```

   Sensitive fields are omitted by the API. Retain any keys separately and retain any other filesystem files separately. The script only performs the requested backup; this task has not contacted or backed up the board.

2. Upload TimeMachine.ino by USB with the custom partition scheme. On a filesystem mount failure, startup logs the recovery, formats the config filesystem, and continues with defaults. This also applies to future filesystem corruption; saved application settings and all other LittleFS files are lost during recovery, while Wi-Fi credentials in NVS are retained. A separate preparation sketch is not required. Check for `Time Machine, V 0.6.0`, the Wi-Fi IP, `Time: synchronized`, and `Weather: disabled (no key)`.

3. Restore the writable settings:

   ```sh
   python3 tools/arduino/settings.py restore http://192.168.0.120 "/Users/mspencer/Documents/ChatGPT/Time Machine/settings-before-weather.json"
   ```

   Read-only schema, version, ipA, and ipB fields, plus volatile timeOffset, are excluded. GCC is now a persistent writable setting, defaults to 4 (0x04), and is included in restores. The main application first attempts config.begin(), then explicitly retries config.begin(MDS_Config::FormatPolicy::OnMountFailure) on mount failure. The shared library default remains non-formatting for existing projects.

Normal startup handles filesystem initialization and recovery; the separate preparation sketch has been removed. No upload or formatting has been performed by the agent.

## Enable optional weather

Copying RGB Clock's interface, use owAppKey, latitude, longitude, and weatherInterval. All are persistent RW; owAppKey is sensitive and never returned by the config API or startup dump. Latitude/longitude remain text like RGB Clock, with numeric range validation. weatherInterval is seconds, default 600, range 60-86400. Coordinates default to empty; no location is assumed.

```sh
curl --get http://192.168.0.120/api/config \
  --data-urlencode 'owAppKey=YOUR_KEY' \
  --data-urlencode 'latitude=YOUR_LATITUDE' \
  --data-urlencode 'longitude=YOUR_LONGITUDE'
```

Read `http://BOARD_IP/api/weather` for state, temperatureF, conditions, sample age/freshness, HTTP/error status, observation/last-success UTC, and the weather-provided timezone offset. An empty key (`/api/config?owAppKey=`) disables further requests and clears the cache; an already-running request may finish and is discarded. `waiting_time` means TLS is waiting for NTP; `waiting_location` means coordinates still need setting. At startup, the clock uses the configured timezone, or the default when none is saved. A successful weather query then replaces the runtime timezone with the returned UTC offset. Subsequent successful queries update that value; weather updates do not automatically save settings.

Requests use OpenWeather Current Weather v2.5, units=imperial, matching RGB Clock's Fahrenheit behavior. Fetching is on a worker task, so network waits do not block the clock or HTTP cache reads. No weather content is rendered on the main display or OLED. Basic Auth is still a deferred task under the existing explicitly approved unauthenticated local-network API policy.

## Command-line build

After the menu is installed:

```sh
/Applications/Arduino\ IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli \
  compile --config-file /Users/mspencer/.arduinoIDE/arduino-cli.yaml \
  --fqbn esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc,PartitionScheme=tm_ota \
  --build-path /tmp/time-machine-build Firmware/TimeMachine
```

Before menu installation, a staged build can use the default board plus `--build-property upload.maximum_size=1835008`; the sketch-local CSV supplies the physical layout. A default-layout image without the custom CSV does not fit and must not be uploaded as weather firmware.

## Validation

Host checks cover disabled/key/location/time/network gating, queued workers, HTTPS bundle selection, JSON/response bounds, caching/freshness, retry/rate-limit scheduling, rollover, stale-request results, key redaction, coordinate validation, API responses, and existing configuration/brightness/time behavior. The real ESP32-C6 build fits the new slots at about 79%; the build output is the exact size reference. The additive/idempotent menu installer is also checked. Hardware TLS/weather, actual flash migration, and runtime heap behavior still require the user's board/key test.

## Application and diagnostics

The sketch owns hardware and explicitly constructs Application with Application::Mode::Clock. Application owns services and dispatches the selected mode. To run diagnostics, select Application::Mode::Tests in the constructor, then choose the diagnostic in DisplayTest.h; tests no longer include a real Clock enum value. Time and optional weather remain serviced in either mode. See Notes and Documentation/Application-Architecture.md.

## Wi-Fi after a power failure

TimeMachine selects the optional saved-network recovery policy. Saved credentials start background station attempts while the clock/application runs; a missing AP or delayed DHCP does not trigger provisioning or reboot. Each attempt allows 20 seconds and retries after a 10-second pause, indefinitely. With no saved credentials, a nonblocking provisioning portal opens and is serviced by tick(), allowing A/B traffic and other application work to continue. Test recovery by starting the board while the AP/DHCP service is unavailable, then bringing the network online; expect normal application servicing followed by an IP and HTTP availability. See Notes and Documentation/WiFi-Baseline.md.

## Button factory reset

Press GPIO 9's active-low button to replace the three alpha characters with RST. Release before five seconds to restore the prior frame and resume the active mode. Hold for five seconds to delete `/config.json` and `/config.json.tmp`, erase saved Wi-Fi credentials, and reboot into provisioning. Other LittleFS files, including website pages and images, are preserved. Formatting occurs only during startup recovery after a filesystem mount failure. Firmware/OTA application slots and the partition table remain intact. Application configuration (including weather key/location) must be entered again. Button polling continues while the portal waits for input; WiFiManager startup, manual submission, and shutdown can still briefly pause servicing. See Notes and Documentation/Button-Factory-Reset.md. No board reset/erase has been performed by the agent.

## Startup display animation (0.3.4)

Firmware 0.3.4 adds two 128-byte Display frame buffers and nonblocking fades. After service initialization, startup lights every mapped segment and indicator with a 350 ms fade up, a full one-second hold, and a 350 ms fade down, then starts Clock or Tests. Existing application current/PWM limits apply; services and the reset button remain active. See Notes and Documentation/Display-Animations.md.

## Application lifecycle (0.3.5)

Firmware 0.3.5 replaces separate lifecycle/reset flags with Application::State and logged transitionTo() entry actions. State dispatch preserves the startup segment test, shared services, reset cancellation/resume, erase failure retry, and reboot behavior. The current segment-test constants remain 750 ms ramps and a 2000 ms hold. No fonts, display current, PWM limits, or shared MDS modules are changed. See Notes and Documentation/Application-States.md.

## Wi-Fi provisioning over A/B

The live persistent `primary` setting controls the master role, as it does timestamped pings. Masters broadcast saved SSID/password on both ports immediately when the role becomes active and every 10 seconds afterward, including while offline. Non-masters accept valid credentials on either port and let MDS_WiFi close the portal, replace saved credentials, and connect in the background. Identical credentials leave the connection/retry process intact. Masters ignore incoming credential commands. Transmissions share the existing independent per-port display queues; busy queues defer broadcasts.

Time Machine now selects RetrySavedNetworkAsync from MDS_WiFi 0.3.0. Manual portal setup remains available on unconfigured clocks. Protocol status responses report framing receipt; received messages are never retransmitted, and there is no sender authentication; the physical links follow the master-only sending convention. Recognized credential payloads are excluded from USB diagnostics. See Firmware/README.md and Notes and Documentation/Serial-Protocol.md. Hardware validation remains pending.

## Connected Board Addresses

No additional configuration is needed for serial IP discovery. Every board announces its available IP on both A/B ports. `/api/config` returns directly connected addresses as read-only `ipA` and `ipB`; the values are runtime-only. Announcements refresh every ten seconds, and recorded addresses clear after 30 seconds without a valid update or on a disconnect announcement.

## Stored Time Offset

MDS_Config 0.7.0 is required for the signed 64-bit timeOffset setting. The API accepts signed unit input or strict UTC date/time input and stores only seconds. The setting is runtime-only, is excluded from restore edits, and is not applied to displayed or system time yet. Relative input works before synchronization; absolute input requires synchronized UTC.
