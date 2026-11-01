# Time Machine

Please see the [Project Website](https://home.marcspencer.com/time-machine/) for detailed information, full API, and build details. 

Time Machine brings the past, present, and future to life as a set of three clocks in red, green,
and yellow. Gloriously oversized, each clock uses 0.8-inch LED displays in a case roughly 14 inches
wide. What began as a hallway joke has become a collaborative hardware and software project, with a
small batch planned for friends at the cost of parts.

Each custom circuit board combines an ESP32-C6 controller with three 16-segment alphanumeric
displays for the month and ten seven-segment displays for the day, year, and time. Across a complete
set, that adds up to 39 displays and 354 individually controllable character segments—no Hollywood
magic required. A dedicated LED driver controls brightness, a small OLED provides status, and serial
links connect the clocks.

The Arduino-based firmware provides NTP time synchronization, timezone and daylight-saving support,
and optional OpenWeather integration. An HTTP API controls configuration, text, and built-in
animations, allowing the clocks to display much more than dates and times. The clocks can work
independently or together, and the MPL-2.0 open-source firmware welcomes contributions, experiments,
and new ideas.

