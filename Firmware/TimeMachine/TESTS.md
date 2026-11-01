# Display diagnostics

`TimeMachine.ino` owns board constants, GPIO, I2C, the matrix driver, and OLED initialization. It constructs `Application(Application::Mode::Clock, matrix, oled)` explicitly, then calls application.begin()/tick(). Application owns configuration, Wi-Fi, HTTP/API, time, weather, and both display modes. In Mode::Tests it calls tests.begin()/tick(); in Mode::Clock it calls Clock.begin()/tick(). Application also services OLED scrolling. `Tests.h/.cpp` owns matrix initialization, diagnostic rendering, timing, state, and error reporting. The class receives the matrix driver, shared Display renderer, and OLED by reference and does not initialize their shared bus or service OLED scrolling.

Set `DISPLAY_TEST_MODE` in `DisplayTest.h`, then build/upload in the Arduino IDE.

| Mode | Behavior |
| --- | --- |
| `Clock` (default) | Real NTP clock in configured local timezone, 12-hour hours with AM/PM and a blinking colon; shows NTP before first sync. |
| `Fade` | Repeating two-second up/down gamma fade, with 500 ms on/off holds, all 13 digits plus AM, PM, and both colon LEDs. |
| `StartupShow` | Two-second all-segment/discrete ramp, 400 ms hold, then numeric wheels and rolling alphabet. Discretes are dark during the rolling count. |
| `Characters` | DS1 cycles A-Z/0-9; DS4/DS5 count 00-99 at 1 Hz. Other digits and discretes are dark. |
| `SegmentWalk` | All 118 wired digit segments, then AM, PM, Colon1, and Colon2, one at a time at 750 ms each, followed by one blank step. |
| `Thermal` | 10 seconds enabled/blank, 30 seconds all 122 populated channels, 2 seconds SW6/DS1 only, then pseudo clock. |
| `PseudoClock` | Starts OCT 24 2026 10:00:00 after reset; advances one second per real second. Main display is MMM DD YYYY HH MM; OLED/Serial include seconds. Handles Gregorian date rollovers; no NTP, RTC, persistence, timezone, or DST adjustment. Discretes are dark. |

All modes share the application wiring map, fonts, centralized gamma correction, and PWM profile. D1 is AM (`0x0E`), D2 is PM (`0x0F`), and D3/D4 form the hours:minutes colon (`0x1E`/`0x1F`). Unused channels and unwired decimal points remain disabled. Matrix I2C errors stop the diagnostic and disable U1. Character/frame updates have no deliberate black frame. Timing remains millis-based with delayed-loop catch-up and rollover handling.

## Current and brightness

The normal display current comes from persistent read/write config `gcc`, default `0x04`, range 0–255. Application loads and applies it before display initialization; the standalone driver fallback remains `0x02`. Diagnostic constants remain in `Tests.cpp`; `TEST_CURRENT=0x04` is a temporary thermal-load override. Other diagnostics inherit configured current, and thermal completion restores the latest configured value. `TEST_BRIGHTNESS=0xFF` requests full logical brightness. `TEST_MAX_PWM=0xFF` is the thermal load PWM ceiling. The normal PWM profile also currently permits `0xFF`.

`DisplayConfig.h` retains build-wide gamma/PWM profile definitions: `DISPLAY_GAMMA_ENABLED=1`, `DISPLAY_DEV_MODE=1`, and `DISPLAY_DEV_MAX_PWM=0xFF`. Gamma 2.2 is applied once by MatrixDriver before scaling to the selected hardware PWM ceiling. Ordinary diagnostics use the normal profile; thermal load phases explicitly use `TEST_MAX_PWM` and restore the normal profile before clock operation. MatrixDriver configures 26.7 kHz PWM (`PG3/0x00=0x11`).

## Thermal sequence

The sequence repeats on reset. After initialization U1 is enabled (SDB HIGH, SSD=1) with zero PWM for `THERMAL_BLANK_MS=10000`. All populated channels then light for `THERMAL_LOAD_MS=30000`, followed by `THERMAL_ROW_MS=2000` with only SW6/DS1 lit. Each load interval begins after its frame write completes. `THERMAL_ROW_SW` selects a fully wired alpha row (SW6-SW8).

The clock starts OCT 24 2026 10:00:00 at the end of both load phases. Existing hardcoded thermal status messages still describe a 30-second blank even though the actual configured blank is 10 seconds; this refactor preserves their behavior.

## Configuration boundary

The sketch owns one `MDS_Config` object and registers the application definitions from `SystemConfig.h`. MDS_Config manages values, validation, access rules, and LittleFS persistence; no config callbacks are needed in registry mode. See `Notes and Documentation/System-Configuration.md` for the registry and persistence contract. No automatic filesystem formatting, config saves, or firmware uploads are performed by this harness.

## Wi-Fi baseline integration

The harness also initializes MDS_WiFi and ticks it in loop. Display diagnostics continue to be serviced offline after Wi-Fi begin returns. Initial provisioning remains blocking for this extraction-only step, so startup/display servicing may wait while the portal runs. Existing no-SSID reboot behavior is retained. See Notes and Documentation/WiFi-Baseline.md.

## Startup report

VERSION is defined at the top of TimeMachine.ino (initially 0.1.0). Setup waits at most 1.5 seconds for USB CDC, prints version/role, dumps current registry values/access flags with sensitive fields redacted, and prints the selected diagnostic settings. Wi-Fi startup runs before the selected diagnostic, with explicit before/after/result/IP messages. The portal can still block for its existing timeout. Test selection, current/PWM settings, and timing are unchanged.

## HTTP API stub

The harness now services MDS_HTTP and ApplicationApi after Wi-Fi provisioning returns and the station has an IP. GET / or /api/config reads nonsensitive registry values; query parameters apply validated writable settings and save. The listener stops/restarts with connectivity; display diagnostics remain independent. See Notes and Documentation/HTTP-API.md for the request/response contract and host tests.

## Live brightness configuration

The `pwm` setting is a persistent RW global PWM ceiling (0-255), defaulting to the current normal profile maximum. The harness applies it before matrix initialization. API changes immediately refresh the active frame, including during holds; tests keep their existing logical brightness and per-test/profile ceilings beneath that overall limit. Cached logical levels preserve gamma-once behavior when dimming to zero and restoring brightness. `gcc` is persistent RW, defaults to `0x04`, and is applied before matrix initialization. Explicit API edits update hardware and save the value. Reads return the configured value without changing temporary thermal overrides; thermal completion restores the latest configured current.

## Real clock and time service

The harness explicitly selects Application::Mode::Clock; there is no default application mode. Clock owns real clock rendering; Tests owns diagnostics. DisplayTest contains only diagnostics, with StartupShow selected when no DISPLAY_TEST_MODE override is supplied. TimeController configures and services time independently of either display mode. MDS_Time runs background SNTP in both modes. Registry options timeZone, ntpServer1, ntpServer2, and ntpInterval control synchronization/conversion and can be updated through the existing config endpoint. Startup uses the configured timezone or default; a successful OpenWeather query then replaces runtime timeZone with the returned offset. TimeController applies the updated value on the next loop. See Notes and Documentation/Time-Service.md for architecture, DST/fixed timezone rules, waiting/offline behavior, tests, and the unchanged OTA-capable partition selection.

## Application current ownership

MatrixDriver.begin(channels, count) uses the configured GCC. Application selects it from config before initialization; the explicit-current begin overload remains compatible. The thermal test keeps its temporary hardware current separate and restores the latest configured value. See Notes and Documentation/Application-GCC.md for API, restoration, and validation details.

## Application ownership

See Notes and Documentation/Application-Architecture.md for the implemented ownership tree. To run diagnostics, change the explicit constructor argument in TimeMachine.ino to Application::Mode::Tests, then select a diagnostic in DisplayTest.h (or a build-wide DISPLAY_TEST_MODE). ClockDisplay has been replaced by Clock; it no longer configures or ticks the shared time service.

## Shared Display renderer

Application owns Display, which selects fonts and applies the physical digit layout above MatrixDriver. Clock mode, diagnostic text, and RST share it; raw segment/load diagnostics retain driver access. Tests::showText composes a fresh frame with indicators off, preserving diagnostic behavior; public Display::showText preserves indicators. See Notes and Documentation/Display-Rendering.md. Host tests in Firmware/tests/display cover padding/truncation, short/null input, alpha-only updates, indicator preservation, logical brightness, clear, and I2C failures with address/undefined-behavior sanitizers.

## Buffered startup lamp test (0.3.4)

Application now runs Display::lampTest() before initializing either selected mode: 350 ms fade up, 1000 ms all mapped segments/indicators fully on, then 350 ms fade down. Diagnostic timers start after that sequence. Display owns per-channel target/output buffers and generic nonblocking fadeUp/fadeDown/fadeTo methods. Run tests/display/run.sh for buffered animation checks and tests/application/run.sh for startup/service/reset integration; see Notes and Documentation/Display-Animations.md.

## Application state machine (0.3.5)

Application now uses State/transitionTo() instead of the lifecycle and reset flags. tests/application/run.sh runs lifecycle, reset, and explicit-state coverage. The read-only state() accessor supports transition assertions. Display timing/font checks follow the user-edited constants and glyphs. See Notes and Documentation/Application-States.md for states, transitions, entry actions, and service behavior.

## Scrolling text

Display exposes `scrollText(text, delayMs, direction)` for a nonblocking single pass, with `Display::Direction::Left` (default) or `Right`. Text enters from the opposite edge, shifts one character per delay, then exits completely. Application already services Display::tick(); normal display writes cancel scrolling. See Notes and Documentation/Display-Animations.md. Run tests/display/run.sh for bidirectional timing, font, lifecycle, and failure checks.

## Display action endpoint

`GET /api/display?message=...` shows static text; an empty message returns to the clock. `GET /api/display?scroll=...&speed=150&direction=left` queues one pass, then restores the prior message or clock. Speed/direction are optional (150 ms/left); actions are runtime-only and do not save config. `/api/config` now rejects message and scroll parameters. Application waits until the startup lamp test completes before consuming commands. HTTP and application host tests cover the endpoint boundary and playback. See Notes and Documentation/HTTP-API.md.

`slideText(text, delayMs, direction)` animates entry then holds at the far edge, using up to 13 characters. `/api/display?slide=...` exposes it with optional speed/direction, and the settled text becomes the static message. Sliding defaults to 150 ms/left through the API. Both directions and final-frame retention are covered by display/application tests.
