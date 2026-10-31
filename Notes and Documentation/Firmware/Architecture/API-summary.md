# Time Machine API

Base URL: `http://BOARD_IP` (port 80). All endpoints use **GET** and return JSON. URL-encode query values.

## Endpoints

| Path | Purpose |
| --- | --- |
| `/api/config` or `/` | Read configuration; query parameters update and save settings. |
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
| `to` | `local`, `a`, or `b`; repeat to select multiple destinations | `local` |

- `colons=blink` cannot accompany either individual colon option, even if false.
- AM and PM are independent; both may be on.
- Fade accepts only its level, optional speed, and destinations; it preserves all indicator/blinking choices. Speed zero applies immediately. It never changes or saves `pwm`/`gcc`.
- Explicit `to` values replace the default: `to=a&to=b` excludes local.
- Duplicate options are rejected except `to`; duplicate destinations send once.
- Serial-routed commands must fit the 246-byte encoded payload limit.
- Received display commands execute locally. Latest pending local command wins.
- Success: **202** `{"accepted":true}` means queued, without remote acknowledgment.

```text
/api/display?message=HELLO&am=true&colontop=true&colonbottom=true
/api/display?appear=OCT2820260925&speed=50&direction=right&pm=true&colons=blink&to=a&to=b
/api/display?scroll=HELLO&speed=150&to=local&to=a
/api/display?message=
/api/display?fade=128&speed=1000
/api/display?fade=255&speed=0&to=local&to=a&to=b
```

## Configuration

A request without parameters reads current settings. Parameters apply together and save; successful requests return **200** with updated configuration. Secrets are omitted.

**RO** = read-only; **RW** = read/write. Sensitive RW settings can be updated but are omitted from responses.

The clock starts with the configured `timeZone`, or the default when none is saved. If OpenWeather is configured, a successful query then replaces this value with the returned UTC offset. The replacement is visible in configuration reads and takes effect without reboot; weather updates do not automatically save settings.

| Setting | Access | Accepted Value |
| --- | --- | --- |
| `schema` | RO | Schema version: 1. |
| `name` | RW | 1–32 bytes. |
| `primary` | RW | `true` or `false`; default false. Enables timing pings and saved Wi-Fi credential broadcasts on A and B. |
| `pwm`, `gcc` | RW | Decimal `0`–`255`; brightness ceiling and LED current. GCC defaults to 4. |
| `timeZone` | RW | POSIX timezone string, 1–128 bytes. Used initially, then replaced when OpenWeather returns a successful result. |
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

**400** invalid/unknown/duplicate/conflicting parameters · **403** read-only setting · **413** serial payload too large · **503** serial queue busy/unavailable · **500** allocation, hardware, or persistence failure.

Error body: `{"error":"...","applied":false,"saved":false}`. Configuration failures after runtime application report `applied:true`; a failed save leaves the runtime edit active.

## Serial Destinations

See the [Serial Summary](Serial-summary.md) for A/B connections, board roles, and serial delivery behavior.
