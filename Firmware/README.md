# Time Machine Firmware

Current firmware: **0.6.0**. `/api/config` reports the running firmware through the read-only, nonpersistent `version` field. Configuration schema remains 1.

## License and source banners

Copyright (c) 2026 Marc D. Spencer. All Rights Reserved under the applicable license.
Firmware source, host tests, and firmware tooling are licensed under the
[Mozilla Public License 2.0](LICENSE). External dependencies retain their own licenses.

Every source banner identifies its file, firmware version, Git base revision, and
`SPDX-License-Identifier: MPL-2.0`, followed by the MPL source notice. `Git base`
records HEAD when the banners were refreshed; it does not claim that local edits
are committed. Firmware version comes from `VERSION` in `TimeMachine.ino`.
From this directory, refresh banners before preparing a release or source snapshot:

```sh
python3 tools/source-banners.py
python3 tools/source-banners.py --check
```

The script covers sketch sources, font tables, partition CSV, host tests/stubs,
and firmware tools. It preserves script shebangs and existing source comments.
Refreshing banners does not change the running firmware version or API behavior.

Open `TimeMachine/TimeMachine.ino`. Build settings, dependencies, OTA partitions, and upload instructions are in [BUILD.md](../BUILD.md).

Shared-library versions: MDS_Config 0.7.0, MDS_Display 0.1.0, MDS_WiFi 0.2.0, MDS_HTTP 0.2.0, MDS_Time 0.2.0, and MDS_Weather 0.4.0. WiFiManager 2.0.17 or newer is also required. Time and HTTP use readiness callbacks introduced in 0.2.0; Weather uses the accepted-sample update callback introduced in 0.4.0.

## Browser firmware upload

Open `http://BOARD_IP/upload` to upload the application firmware `.bin` file. A successful upload sends its HTTP response, then the application reboots into the new firmware. Failed or incomplete uploads do not request a reboot. Use the application binary, not the bootloader, partition table, or merged flash image. The running OTA partition layout must have room for the new application; this endpoint does not change the partition table or replace LittleFS files.

## Wi-Fi provisioning over A/B

The persistent `primary` setting selects the master role. At startup, a bridged master jumper defaults `primary` to `true`; an open jumper defaults it to `false`. Configuration is then loaded, and any saved `primary` value overrides the jumper default. `/api/config?primary=true` or `primary=false` changes and saves the role regardless of the jumper, and that saved choice survives reboot. Serial and OLED startup role messages report the effective configuration value.

A master broadcasts its saved SSID/password on both A/B ports when its role becomes active and every 10 seconds afterward. Its own Wi-Fi connection need not be ready. Each UART has its own timer and pending frame, so a blocked port does not stop the other port. Busy display queues defer credentials, and queued credentials can make a serial display request return HTTP 503. Frames drain in available UART-sized pieces without interleaving pings.

Any non-master receiving valid credentials on either port passes them to MDS_WiFi, replacing credentials for an old network even while it is still retrying that network. An open provisioning portal closes before the new connection starts. Identical credentials preserve the connection/retry attempt and avoid storage writes. Masters ignore incoming credentials. Receiving boards acknowledge frame receipt without retransmitting credentials; the master repeats them periodically for late-starting or newly attached slaves.

Application uses `RetrySavedNetworkAsync`: saved credentials retry in the background; no saved SSID opens a portal serviced by `tick()`. Manual portal configuration remains available while A/B reception, display, and button work continue. WiFiManager can still pause during portal startup, manual submission, and shutdown (including its one-second shutdown delay).

The physical links are trusted by convention: there is no sender authentication or encryption. Protocol status responses report framing receipt, independently of credential validation or Wi-Fi connection. By default, credential diagnostics report only accepted/rejected/ignored status. Credentials stay in platform Wi-Fi storage, outside the application config API.

See [Serial protocol](../Notes%20and%20Documentation/Serial-Protocol.md) for wire details and [Wi-Fi behavior](../Notes%20and%20Documentation/WiFi-Baseline.md) for connection policy.

## Display fade command

`GET /api/display?fade=128&speed=1000` fades the display to logical level 128 over one second. Level is 0–255; speed is the total duration in milliseconds (0–60000, default 350). Zero speed applies immediately. Level zero blacks out the display, and fading back to 255 restores full logical intensity within existing PWM/current limits.

Fade preserves text, clock updates, motion, and indicator/blinking choices. It is runtime-only and does not save or change `pwm`/`gcc`. Supply exactly one action; direction, text actions, and indicator parameters cannot accompany fade. Use `to=all` for the local display, port A, and port B, for example `/api/display?fade=0&speed=1000&to=all`. Individual destinations remain available through `to=local`, `to=a`, and `to=b`. Acceptance returns HTTP 202; a busy serial queue returns 503.

## Serial frame debugging

Uncomment `#define DEBUG_SERIAL_FRAMES` in `TimeMachine/SerialProtocol.h` or
pass `-DDEBUG_SERIAL_FRAMES` as a compiler flag. The USB Serial Monitor shows
complete A/B frames as `Serial A RX:`, `Serial B TX:`, and their counterparts.
The trace includes raw Wi-Fi credentials and configuration payloads. Framing
errors are reported in both builds.

USB diagnostics use a zero transmit timeout so an unopened or stalled Serial
Monitor cannot delay the display and network services. Debug output may be
dropped when the USB transmit buffer is full.

## LittleFS website

Static requests map `/` to `/www/` and `/assets/` to `/www/assets/`.
The ESP32 directory handler uses `index.htm`, so `/` serves `/www/index.htm`.
For example, `/assets/logo.png` serves `/www/assets/logo.png`, with its content
type selected from the file extension. Missing files return HTTP 404.

API routes are registered first and remain under `/api/`; `/` no longer reads
or updates configuration. Website files must be copied into LittleFS separately.
The firmware does not create a page or provide a file-upload endpoint.

Use the project-level `tools/lfs-download` and `tools/lfs-upload` for USB
filesystem transfers. See [LittleFS transfer tools](../tools/README.md) for the
download/edit/upload workflow and offline image packing.

## Factory reset and LittleFS files

Hold the GPIO 9 button for five seconds to delete `/config.json` and any
unfinished `/config.json.tmp` save, clear saved Wi-Fi credentials, and reboot.
Other LittleFS files, including website pages and images, are preserved.
Missing configuration files are harmless; a deletion or Wi-Fi erase failure
stops the reset until the button is released and held again.

Filesystem formatting is reserved for startup recovery when LittleFS fails to
mount. That recovery clears the entire filesystem, including website files.

## Validation

```sh
ARDUINOJSON_SRC=/Users/mspencer/Documents/Arduino/libraries/ArduinoJson/src \
  sh tests/application/run.sh
```

Application host tests cover both ports, offline broadcasts, ten-second timing and rollover, partial writes, display queue coexistence, live role changes, credential replacement during provisioning/retries, repeated credentials, malformed envelopes, and opt-in frame tracing. They also retain lifecycle, button reset, display routing, and timestamp synchronization checks. The owner confirmed A/B provisioning working on hardware. Fade HTTP/rendering/serial checks run with `sh tests/http/run.sh` (using the same ARDUINOJSON_SRC setting); hardware fade playback still needs a bench test.

## Timezone Selection

The clock starts with the configured `timeZone`, or the default when no value is saved. If OpenWeather is configured, a successful query then replaces the timezone with the returned UTC offset. NTP supplies UTC time. The change is visible through `/api/config` and applies without reboot; weather updates do not automatically save settings.

## Serial Protocol Responses

The receiver returns `{"status":"ok"}` for a complete, correctly framed message. Framing failures return `error-length`, `error-terminator`, or `error-timeout` in the same JSON status field. Responses return through the input port using normal serial framing. They confirm protocol receipt, rather than execution, and do not themselves receive acknowledgments. HTTP 202 continues to mean accepted for queueing and does not wait for a serial response.

## Connected Board Addresses

Read `/api/config` to find directly connected boards through `ipA` and `ipB`. These fields are read-only, are never saved, and are empty until an address is reported. Every board announces an available or changed IP on both serial ports, then every ten seconds. A field clears after 30 seconds without a valid update or when an empty disconnect announcement arrives. Received announcements remain one hop and are never retransmitted.

## Stored Time Offset

`/api/config` accepts runtime-only `timeOffset` input such as `%2B5d` (URL-encoded `+5d`), `-2h`, or `2026-10-31T12:00:00Z`. Relative units are lowercase `s`, `m`, `h`, and `d`; absolute input uses only the shown UTC format and requires synchronized time. Reads return signed seconds. The value resets to zero at startup, is never saved, and does not affect clock or display behavior yet. See the API Summary in the Architecture documentation for the complete input rules.
