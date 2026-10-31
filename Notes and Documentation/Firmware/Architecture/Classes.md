# Firmware Classes

This guide explains the intent of each functional class in the Time Machine firmware, the design used to achieve it, and the key methods. See [Architecture](Architecture.md) for the system overview, ownership diagram, and startup flow.

External libraries are described only where they interact with project classes. Namespaces, configuration definitions, and data structures are covered in the architecture overview.

## Application

### Intent

Coordinate the board as one system: start services in the right order, keep communication responsive, and manage startup, display faults, and reset requests.

### How It Works

`Application` owns the services and controllers and holds references to the matrix, OLED, and button supplied by the sketch. An explicit state machine determines which work can run. `transitionTo()` records each change and performs the associated entry actions, such as starting the lamp animation or enabling the clock. Shared services continue during that animation, a reset-button hold, and a matrix display fault.

Callbacks connect external events to application behavior:

- A Wi-Fi status callback writes messages to the OLED when available.
- A time status callback writes to USB and the OLED. After synchronization, it also displays the network address when connected.
- A weather status callback writes to USB and, after a successful query, asks `WeatherController` to replace the configured timezone with the returned offset.
- A display-routing callback connects `ApplicationApi` to `routeDisplayCommand()`. It returns whether the selected serial destinations could accept the command.

These callbacks capture `this` to reach the existing application instance. `Application` cannot be copied, keeping references and callbacks attached to the instance created by the sketch.

Serial servicing is independent for A and B. Each port has a receiver, a pending transmit buffer, and transmission progress. Writes use available UART space, so a busy port does not stall the other port. A frame finishes before another message starts on that port. Routing checks every selected transmit buffer before changing any of them, so a busy destination rejects the request before another destination is queued. Received messages are never retransmitted.

Credential messages are recognized before general payload logging, preventing credentials from appearing in USB output. Timing pings update display timing before logging can add delay. Receive work is bounded per port to leave time for other services.

A reset-button hold temporarily replaces the alpha digits with `RST`. Release restores the saved display output and application state. Holding for five seconds clears settings and Wi-Fi credentials, then reboots. The final hold check also reads the raw button state so a physical release can cancel reset during release debounce.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Loads configuration, starts services, installs callbacks, and starts the lamp animation |
| `tick()` | Advances the state machine, services communication, and updates controllers and display output |
| `transitionTo()` | Centralizes lifecycle changes and their entry actions |
| `routeDisplayCommand()` | Checks selected serial destinations and prepares outbound display frames |
| `serviceSerial()` | Receives messages, drains outgoing frames, and originates master timing and credential messages |
| `serviceButton()` and `factoryReset()` | Handle cancelable reset requests, erase settings, and initiate reboot |

## ApplicationApi

### Intent

Translate external requests into validated application actions, with the same display-command model for HTTP and incoming serial messages.

### How It Works

The API holds references to existing services and the display queue. `begin()` registers HTTP handlers once. Each handler captures `this` and calls the appropriate member method when a request arrives. The API instance and its dependencies must remain alive while those handlers are registered; copying the instance is disabled.

Display requests become `Display::DisplayCommand` values. Local commands enter the shared display queue. For serial destinations, the API invokes the routing callback supplied by `Application`, checks the returned acceptance result, and only then queues any local copy. The API handles request validation and command encoding; `Application` handles the physical ports.

Serial display payloads carry no routing fields. `receiveDisplayCommand()` validates and decodes them, then adds them directly to the local queue without calling the routing callback. This implements the one-hop communication rule.

Configuration requests collect changes before submitting them together for validation. Brightness and current changes are applied at runtime before settings are saved. If persistence fails, the response distinguishes settings already applied from settings successfully saved. Sensitive values are excluded from configuration responses. Display actions remain separate from saved settings.

Weather requests return cached data and status, so an HTTP request does not wait for a weather fetch. The weather endpoint is registered only when a weather service is supplied.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Registers configuration, display, and optional weather handlers |
| `setDisplayRouter()` | Supplies the callback used to request serial delivery |
| `configRequest()` | Validates setting changes, applies display settings, saves configuration, and reports the result |
| `displayRequest()` | Validates an action and sends it to the selected local and serial destinations |
| `encodeDisplayCommand()` and `receiveDisplayCommand()` | Convert between structured commands and serial payloads |
| `weatherRequest()` | Returns cached weather and retrieval status |

Endpoint parameters and responses are described in the [API Summary](API-summary.md).

## Display

### Intent

Provide one renderer for clock content, messages, indicators, and animations, while preserving content as brightness and display effects change.

### How It Works

`Display` owns the command queue and uses a shared `MatrixDriver`. Characters are mapped to the physical digit positions, selecting the appropriate font for each destination. A logical frame contains brightness values for segments and indicators; hardware conversion is left to the driver.

Content and output intensity are separate. The target frame retains content at full logical intensity, while the output frame applies the current fade level. Fading to black therefore preserves content, and fading back restores it. The startup lamp animation temporarily overlays all populated segments without discarding target edits. `flush()` compares output with the cached driver values and skips unchanged writes.

Clock snapshots continue to be stored while an explicit message is visible. A temporary scroll restores the held message or clock when finished; a completed slide or grouped reveal becomes the new held message. Animated text is composed for the destination digits on every step, so characters retain the correct font as they move between different digit types.

Animation advances through `tick()`. Delayed passes calculate how many steps have elapsed instead of adding a fresh delay after each pass. Scroll, slide, and grouped reveal share the same motion machinery. Fade intensity uses a smooth easing curve, with gamma correction applied later by `MatrixDriver`.

Timing synchronization adjusts a display-only millisecond offset. When that offset changes, stored animation timestamps shift by the same amount, preserving elapsed durations. This aligns blinking colons without changing button, network, or serial timing. Calendar values still come from the time service.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Initializes the populated display channels through `MatrixDriver` |
| `commands()` and `tick()` | Expose command submission and perform command execution, rendering, and animation |
| `setMessage()`, `scrollText()`, `slideText()`, and `appearText()` | Hold text, move text, or reveal fixed calendar groups |
| `fadeTo()`, `fadeUp()`, and `fadeDown()` | Change intensity while retaining content and motion |
| `lampTest()` and `cancelLampTest()` | Start or end the temporary startup lamp animation |
| `drawText()`, `setIndicator()`, and `show()` | Compose text and indicators in a frame, then apply that frame |
| `showAlpha()` and `frame()` | Support temporary alpha overlays and display snapshots |
| `synchronizeMillis()` and `resetMillisDelta()` | Align display timing with the master or restore local timing |

## Display::CommandQueue

### Intent

Give display command producers one submission interface and let `Display` execute commands during the update cycle.

### How It Works

This nested class is owned by `Display`. `Clock` and `ApplicationApi` receive references to it, keeping command production separate from rendering.

The implementation stores one pending command. New explicit commands replace pending work, while clock updates yield to a pending explicit command. `Clock` retries when a snapshot is not accepted. This favors requested actions without requiring producers to inspect display state.

Commands own their text. The copy form of `post()` checks that text was copied successfully, while the move form transfers the prepared command. `take()` transfers pending work to the renderer and clears the pending state.

### Key Methods

| Method | Purpose |
| --- | --- |
| `post()` | Submits a command and reports whether it was accepted |
| `take()` | Transfers a pending command to the consumer |

## MatrixDriver

### Intent

Turn logical segment brightness into LED output, keeping hardware control and brightness conversion in one place.

### How It Works

The driver uses the I²C bus supplied by the sketch. Initialization blanks the hardware before enabling output, clears all channels, and enables only the populated channels identified by `Display`. This also avoids a stale lit frame after a controller reset.

Brightness values remain logical values in the cache. Gamma conversion and PWM limits are applied only when writing hardware. Changing a brightness ceiling redraws from those original values, avoiding repeated gamma correction and allowing brightness to return after a blackout. The effective PWM ceiling is the lower of the configured brightness limit and the active output profile limit.

Configured current and active hardware current are stored separately. Normal settings can be selected before initialization, while a temporary hardware adjustment need not replace the configured value.

Channel writes are divided into bounded I²C transfers. A write failure records an error, marks the driver unavailable, and disables hardware output. `Application` can then stop matrix rendering while continuing other services.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Blanks and configures the hardware and enables the supplied channels |
| `setBrightness()`, `writeBrightness()`, and `fillBrightness()` | Write one channel, a frame range, or a uniform range |
| `setBrightnessLimit()` | Applies a brightness ceiling and refreshes output from cached logical values |
| `setConfiguredCurrent()` and `setGlobalCurrent()` | Set normal current or adjust the active hardware current |
| `brightness()`, `ready()`, and `lastError()` | Expose cached brightness and driver status |

## Clock

### Intent

Supply current local time to the display while keeping time acquisition and rendering independent.

### How It Works

`Clock` reads the external time service and publishes structured commands containing calendar values, AM/PM choices, and colon behavior. It holds a reference to the command queue and has no direct access to `Display` or `MatrixDriver`.

The initial command marks time as unavailable. Once valid time is available, updates are published when the time changes. A configuration-version check forces a fresh snapshot after time settings change, even if the UTC second has not changed. A rejected queue submission is retried on a later update pass. USB status output is limited to changes of minute.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Enables publishing, resets tracking, and publishes the initial waiting state |
| `tick()` | Detects time or configuration changes and publishes an updated snapshot |
| `publish()` | Builds a display command for valid local time or an unavailable-time state |

## TimeController

### Intent

Connect application time settings to the external time service without making that service depend on application configuration.

### How It Works

At startup, the controller applies the configured timezone or default, along with server and update-interval settings. It reads the same settings during later updates, including any timezone written after a successful weather query. The time service recognizes unchanged options, avoiding unnecessary reinitialization.

Network readiness is passed to the service independently of display activity. Configuration errors are reported to USB and, when available, the OLED. Repeated identical errors are suppressed until the error changes or configuration succeeds. Service status callbacks are installed by `Application`.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Records OLED availability and applies the initial settings |
| `configure()` | Translates application settings into time options and reports configuration errors |
| `tick()` | Rechecks settings and updates the service with network readiness |

## WeatherController

### Intent

Connect application weather settings and readiness information to the external weather service, and use successful results to set the timezone.

### How It Works

The controller reads the API key, location, and update interval on each update pass. It supplies those options to the weather service, followed by network and clock readiness. The service recognizes unchanged options and decides when a request is eligible.

This keeps settings and application readiness decisions outside the reusable library. Weather retrieval remains independent of HTTP reads and display output. `Application` installs the weather status callback. On a `Weather: updated` notification, that callback calls `applyTimezone()` to replace `timeZone` with the offset from the successful result. The existing time controller then applies the changed setting.

The initial timezone is established before weather retrieval. Each successful result replaces that value with a POSIX timezone string derived from the returned UTC offset, reversing the sign convention and preserving fractional-hour offsets. The update changes runtime configuration without automatically saving it.

### Key Methods

| Method | Purpose |
| --- | --- |
| `tick()` | Applies current settings and updates the weather service with network and clock readiness |
| `applyTimezone()` | Replaces the configured timezone after a successful weather query |

## Button

### Intent

Provide a stable button state while leaving the meaning of a press to `Application`.

### How It Works

The input is active-low and uses a pull-up. A raw change starts a debounce interval; the public stable state changes only after the input remains steady for that interval. `tick()` reports a transition once, while `pressed()` remains available for hold detection.

`rawPressed()` exposes the immediate input separately. `Application` uses it at the reset deadline so a physical release can cancel reset before the stable release is reported. Elapsed-time comparisons use unsigned subtraction to work across timer rollover.

### Key Methods

| Method | Purpose |
| --- | --- |
| `begin()` | Configures the input and captures the initial state |
| `tick()` | Updates debounce state and reports a stable transition |
| `pressed()` | Reports the stable pressed state |
| `rawPressed()` | Reports the most recently sampled physical state |

## SerialProtocol::Receiver

### Intent

Assemble complete serial messages from arriving bytes while keeping incomplete or malformed traffic from blocking application work.

### How It Works

`Application` owns one receiver for each port. Each instance tracks the length byte, the exact number of payload bytes, and the final terminator. A newline inside a valid payload is treated as data because the payload length determines framing.

The receiver returns a result rather than dispatching commands or invoking callbacks. `Application` decides whether a complete payload contains credentials, timing information, or a display action. This keeps framing independent of message meaning.

Invalid lengths and terminators enter a discard state until a newline or idle timeout allows recovery. `expire()` also abandons incomplete messages after an idle timeout, even when no new byte arrives. Completed payload data remains available after parser reset so `Application` can read it immediately.

### Key Methods

| Method | Purpose |
| --- | --- |
| `feed()` | Accepts one byte and reports completion or a framing error |
| `expire()` | Detects an idle timeout and resets incomplete reception |
| `payload()` and `length()` | Expose the completed message for application dispatch |
| `reset()` | Returns the parser to the length-reading state |
