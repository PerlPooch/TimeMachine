# Time Machine API

Base URL: `http://BOARD_IP` (port 80). All endpoints use **GET** and return JSON. URL-encode query values.

## Endpoints

| Path | Purpose |
| --- | --- |
| `/api/config` or `/` | Read configuration and status; query parameters update settings. |
| `/api/display` | Queue a local and/or remote display command; never saves settings. |
| `/api/weather` | Read cached weather and fetch status. |

## Display

Supply exactly one action:

| Parameter | Behavior | Text Limit |
| --- | --- | --- |
| `message=TEXT` | Hold text; empty releases the override. | 13 bytes |
| `scroll=TEXT` | Scroll once, then restore previous message or clock. | 512 bytes |
| `slide=TEXT` | Slide in and hold. | 13 bytes |
| `appear=TEXT` | Reveal groups of 3, 2, 4, 2, and 2 characters, then hold. | 13 bytes |
| `fade=LEVEL` | Fade global logical intensity, preserving current content. | Level 0–255 |

| Option | Values | Default |
| --- | --- | --- |
| `speed` | Text motion: `1`–`60000` ms per step/chunk; fade: `0`–`60000` ms total duration | Text motion: `150`; fade: `350` |
| `direction` | `left`, `right`; text motion only | `left` |
| `am`, `pm` | Each `true` or `false` | `false` |
| `colontop`, `colonbottom` | Each `true` or `false` | `false` |
| `colons` | `blink` (both dots, synchronized one second on/off) | No blinking |
| `to` | `local`, `a`, `b`, or `all`; repeat to select multiple destinations | `local` |

- `colons=blink` cannot accompany either individual colon option, even if false.
- AM and PM are independent; both may be on.
- Fade accepts only its level, optional speed, and destinations; it preserves all indicator/blinking choices. Speed zero applies immediately. It never changes or saves `pwm`/`gcc`.
- Explicit `to` values replace the default: `to=a&to=b` excludes local. `to=all` selects the local display, port A, and port B.
- Duplicate options are rejected except `to`; duplicate destinations send once.
- Serial-routed commands must fit the 246-byte encoded payload limit.
- Received display commands execute locally. Latest pending local command wins.
- Success: **202** `{"accepted":true}` means queued. HTTP does not wait for serial receipt statuses, which confirm framing rather than execution.

```text
/api/display?message=HELLO&am=true&colontop=true&colonbottom=true
/api/display?appear=OCT2820260925&speed=50&direction=right&pm=true&colons=blink&to=a&to=b
/api/display?scroll=HELLO&speed=150&to=local&to=a
/api/display?message=
/api/display?fade=128&speed=1000
/api/display?fade=255&speed=0&to=all
```

## Configuration

A request without parameters reads current settings. Parameters apply together; persistent settings are saved, while runtime-only settings remain in memory. Local updates return **200** with configuration from the HTTP board. Secrets are omitted.

Use `to=local`, `to=a`, `to=b`, or `to=all` to select destinations; repeat `to` to combine them. The default is local. Remote-only updates return **202** `{"accepted":true}` after queueing. A request with `to` must include at least one setting; remote reads are not supported. Read-only fields remain read-only on every board.

Updates are validated before any selected port is queued or local settings change. Each receiver validates and applies the group locally, saving only persistent fields. HTTP does not wait for remote execution, and a later failure on one board does not undo changes on another.

**RO** = read-only; **RW** = read/write. Sensitive RW settings can be updated but are omitted from responses.

`ipA` and `ipB` identify directly connected boards. They are runtime values and are never saved. An address is empty until reported, clears after 30 seconds without a valid update, and clears immediately when a disconnect announcement arrives. Attempts to write either field return HTTP 403.

The clock starts with the configured `timeZone`, or the default when none is saved. If OpenWeather is configured, a successful query then replaces this value with the returned UTC offset. The replacement is visible in configuration reads and takes effect without reboot; weather updates do not automatically save settings.

| Setting | Access | Accepted Value |
| --- | --- | --- |
| `schema` | RO | Configuration schema version: 1. |
| `version` | RO | Running firmware version: `0.5.1`. Runtime status; never saved. |
| `ipA` | RO | IPv4 address reported by the board connected to port A, or empty. |
| `ipB` | RO | IPv4 address reported by the board connected to port B, or empty. |
| `name` | RW | 1–32 bytes. |
| `primary` | RW | `true` or `false`; default false. Enables timing pings and saved Wi-Fi credential broadcasts on A and B. |
| `pwm`, `gcc` | RW | Decimal `0`–`255`; brightness ceiling and LED current. GCC defaults to 4. |
| `timeZone` | RW | POSIX timezone string, 1–128 bytes. Used initially, then replaced when OpenWeather returns a successful result. |
| `timeOffset` | RW | Signed 64-bit seconds; runtime-only, default `0`. Accepts a signed amount and unit, or a strict UTC date/time. See Time Offset below. |
| `ntpServer1`, `ntpServer2` | RW | Up to 64 bytes; first must be nonempty. |
| `ntpInterval` | RW | `60`–`86400` seconds. |
| `owAppKey` | RW | Up to 64 bytes; empty disables weather; never returned. |
| `latitude`, `longitude` | RW | Decimal text within −90…90 / −180…180; empty allowed. |
| `weatherInterval` | RW | `60`–`86400` seconds; default 600. |

```text
/api/config
/api/config?name=Time%20Machine&primary=true
/api/config?pwm=128&gcc=4
```

## Weather

`GET /api/weather` returns readiness/freshness, fetch/error status, and timestamps; available samples include temperature (°F), conditions, location, and timezone offset. Reads do not trigger a network fetch. Successful background queries also set the system timezone from the returned offset. Reading this endpoint does not change the timezone.

## Errors

**400** invalid/unknown/duplicate/conflicting parameters · **403** read-only setting · **413** serial payload too large · **503** serial queue busy/unavailable or synchronized time required · **500** allocation, hardware, or persistence failure.

Error body: `{"error":"...","applied":false,"saved":false}`. Configuration failures after runtime application report `applied:true`; a failed save leaves the runtime edit active.

## Serial Destinations

See the [Serial Summary](Serial-summary.md) for A/B connections, board roles, and serial delivery behavior.

## Time Offset

`timeOffset` is a read/write, runtime-only signed offset in seconds, defaulting to `0`. The clock adds it to current UTC before converting to the configured local timezone. Changes take effect on the next clock update, including values received over A/B. System time remains unchanged. It resets to zero at startup and is never saved. Without synchronized time, or when the shifted date cannot be represented within years 0001–9999, the clock shows dashes. Setting `0` (or `+0s`) restores the unshifted clock.

Set it using one of these input forms:

| Input | Meaning |
| --- | --- |
| `+5d` | Set the offset to 432000 seconds |
| `-2h` | Set the offset to −7200 seconds |
| `+90m` | Set the offset to 5400 seconds |
| `-30s` | Set the offset to −30 seconds |
| `0` or `+0s` | Reset the offset to zero |
| `2026-10-31T12:00:00Z` | Set the offset to this UTC timestamp minus current UTC |

Bare `0` clears the offset without requiring synchronized time. Other relative input requires an explicit sign, an integer amount, and one lowercase unit: `s`, `m`, `h`, or `d`. A day is exactly 24 hours. Each request replaces the offset rather than adding to it. Absolute input accepts only `YYYY-MM-DDTHH:MM:SSZ`, with a real Gregorian date, years 0001–9999, and UTC indicated by the final `Z`. Fractional seconds, alternate separators, and timezone offsets are not accepted.

For serial destinations, the HTTP board converts the input to seconds once and sends that value to every selected board. Absolute input therefore uses synchronized UTC on the HTTP board, not on each receiver.

Absolute input requires synchronized time; otherwise the API returns HTTP 503. Relative input works before synchronization. Invalid input or a result outside the signed 64-bit range returns HTTP 400. Reads return a JSON integer containing seconds, not the original input string.

URL-encode a literal plus sign as `%2B`:

```text
/api/config?timeOffset=%2B5d
/api/config?timeOffset=%2B5d&to=b
/api/config?timeOffset=-2h
/api/config?timeOffset=0
/api/config?timeOffset=2026-10-31T12:00:00Z
```

Requests containing only runtime settings skip saving. A mixed request still validates all changes together and saves only persistent fields.
