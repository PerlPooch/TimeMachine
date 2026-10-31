# Time Machine Serial Interface

The A/B links connect boards for display control, timing synchronization, and Wi-Fi setup. Serial communication works without a Wi-Fi connection. USB remains the diagnostic console.

Any board can connect safely to any other board through an A-to-B link. One board acts as the master, receiving display commands over HTTP and sending them through port A, port B, or both to directly connected boards. Every serial message travels exactly one hop and is never retransmitted.

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
/api/display?fade=128&speed=1000&to=a&to=b
```

Without `to`, the action affects only the local display. Received commands affect the display on the receiving board. Supported actions are `message`, `scroll`, `slide`, `appear`, and `fade`. See the [API Summary](API-summary.md) for options.

## Timing and Wi-Fi Sharing

The primary sends timing pings about once per second to align animation timing and blinking colons. Calendar time comes from the time service on each board.

The primary also broadcasts saved Wi-Fi credentials about once every ten seconds, including while offline. Non-primary boards accept those credentials and connect to the network. Primary boards ignore incoming credentials.

## Delivery

Messages are sent without acknowledgments or delivery confirmation. An HTTP **202** response means a command was accepted for transmission. HTTP **503** indicates a busy port, and **413** indicates a command that exceeds the serial size limit.

The primary originates periodic timing and credential messages.

Credential sharing assumes trusted physical connections. The serial protocol provides no encryption or sender authentication.
