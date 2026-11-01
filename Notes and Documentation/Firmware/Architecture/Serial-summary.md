# Time Machine Serial Interface

The A/B links connect boards for display control, configuration, timing synchronization, Wi-Fi setup, and discovery of connected boards. Serial communication works without a Wi-Fi connection. USB remains the diagnostic console.

Any board can connect safely to any other board through an A-to-B link. One board acts as the master, receiving display commands and configuration updates over HTTP and sending them through port A, port B, or both to directly connected boards. Every serial message travels exactly one hop and is never retransmitted.

## Connections

Both ports use **115200 baud, 8 data bits, no parity, and 1 stop bit (8N1)**. The serial signals use **3.3 V logic**; the +5 V pin supplies power.

| Connector | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
| --- | --- | --- | --- | --- |
| A / J2 | +5 V | GND | TX | RX |
| B / J3 | +5 V | GND | RX | TX |

The reversed TX and RX positions support a straight-through A-to-B connection.

## Board Roles

Both ports receive messages on every board. The master also supplies timing and Wi-Fi settings to the connected group. The configuration API calls this role `primary`. Set `primary=true` through the [HTTP Configuration API](API-summary.md); the default is `false`. The jumper label does not select this role.

## Display Control

Send display actions to the master through the HTTP API, selecting either serial port or both:

```text
/api/display?message=HELLO&to=a
/api/display?scroll=HELLO&speed=150&to=local&to=b
/api/display?fade=128&speed=1000&to=all
```

Without `to`, the action affects only the local display. `to=all` selects the local display, port A, and port B. Received commands affect the display on the receiving board. Supported actions are `message`, `scroll`, `slide`, `appear`, and `fade`. See the [API Summary](API-summary.md) for options.

## Configuration Updates

Send writable settings through the same destination choices as display commands:

```text
/api/config?timeOffset=%2B5d&to=b
/api/config?timeOffset=-2h&to=all
```

The HTTP board converts offset input to seconds before sending. Each selected board applies the received value locally, and the clock uses the offset for display output; updates are never forwarded. Read-only settings cannot be changed, and runtime-only settings such as `timeOffset` are never saved. Other settings retain their normal persistence behavior. The serial `ok` response confirms framing, not successful application. See the [API Summary](API-summary.md).

## Timing and Wi-Fi Sharing

The primary sends timing pings about once per second to align animation timing and blinking colons. Calendar time comes from the time service on each board.

The primary also broadcasts saved Wi-Fi credentials about once every ten seconds, including while offline. Non-primary boards accept those credentials and connect to the network. Primary boards ignore incoming credentials.

## Connected Board Addresses

Every board announces its IP on both ports when the address becomes available or changes, then every ten seconds. Each receiver exposes the address as read-only `ipA` or `ipB` through `/api/config`, according to the input port. These values identify directly connected boards and are never saved. They clear after 30 seconds without an update or when an empty disconnect announcement arrives.

## Delivery

The receiver returns a protocol status on the same port. A response such as `{"status":"ok"}` confirms valid message framing, independently of command execution or Wi-Fi connection. An HTTP **202** response means a command was accepted for transmission; it does not wait for the serial response. HTTP **503** indicates a busy port, and **413** indicates a command that exceeds the serial size limit.

The primary originates periodic timing and credential messages.

Credential sharing assumes trusted physical connections. The serial protocol provides no encryption or sender authentication.

## Protocol Responses

| Status | Meaning |
| --- | --- |
| `ok` | A complete, correctly framed message was received |
| `error-length` | The declared payload length exceeds the supported maximum |
| `error-terminator` | The expected end-of-frame byte was incorrect |
| `error-timeout` | Reception stopped before the frame was complete |

Responses use the same framing as other serial messages. They travel one hop, are never retransmitted, and do not generate another acknowledgment. Without a checksum, payload corruption that preserves valid framing cannot be detected.
