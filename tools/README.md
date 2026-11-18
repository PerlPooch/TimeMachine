# LittleFS transfer tools

## Firmware build and upload

From the project root, build the application and upload it to one or more boards:

```sh
tools/build && tools/upload 192.168.0.120 192.168.0.121
```

`upload` requires `curl`, `jq`, and the application binary at
`Firmware/bin/TimeMachine.ino.bin`. It reads the current version before uploading,
then polls `/api/config` up to 30 times for recovery after the reboot. Every HTTP
request has connection and overall timeouts. A failed board is reported and the
remaining boards are still processed; the command exits nonzero if any board fails.

For USB serial, close the Serial Monitor and use the companion command:

```sh
tools/build && tools/upload-serial /dev/cu.usbmodemYOUR_PORT
```

Additional port arguments upload the same build to more boards. The command uses
Arduino CLI's normal serial upload/reset recipe with the project's OTA layout,
including the bootloader, partition table, OTA boot metadata, and application.
It uses existing files in `Firmware/bin` and does not compile. Failed ports are
reported, remaining ports are processed, and any failure makes the command exit
nonzero. For `upload-serial`, `ARDUINO_CLI` and `ARDUINO_CLI_CONFIG` override the
executable and configuration paths; their defaults match `tools/build`.

## LittleFS download and upload

`lfs-download` reads and unpacks the board's filesystem. `lfs-upload` packs a
local filesystem folder and writes it back. Both are for this project's
4 MiB ESP32-C6 layout.

They require Python 3.8+, `mklittlefs`, and esptool 5+. Installed ESP32 Arduino
tools are discovered automatically; `--mklittlefs PATH` and `--esptool PATH`
override discovery. Close the Arduino Serial Monitor before connecting.

## Download, edit, upload

Run from the project root, replacing the serial port and local workspace paths:

```sh
tools/lfs-download --port /dev/cu.usbmodemYOUR_PORT /path/to/lfs-work

# Edit or copy files into /path/to/lfs-work/www/.

tools/lfs-upload --port /dev/cu.usbmodemYOUR_PORT /path/to/lfs-work
```

Download replaces the output folder automatically. Old local files absent from
the board are removed. Both tools remove the sibling `/path/to/lfs-work.bin`
after a successful transfer and final reset. Use `--keep-image` to retain it:

```sh
tools/lfs-download --port /dev/cu.usbmodemYOUR_PORT /path/to/lfs-work --keep-image
tools/lfs-upload --port /dev/cu.usbmodemYOUR_PORT /path/to/lfs-work --keep-image
```

Download's `--image PATH` selects another raw backup location and follows the
same cleanup policy. Upload's `--keep-image` saves the newly packed data to the
sibling `.bin`, replacing an older image there.

The new image is downloaded and unpacked before existing outputs are replaced.
Failed transfers leave the previous folder and backup intact. If unpacking fails
and no backup exists yet, the raw image is retained for recovery.

The folder is the filesystem root, with this layout:

```text
lfs-work/
├── config.json
└── www/
    ├── index.htm
    └── assets/
        ├── style.css
        └── logo.png
```

**Upload replaces the entire LittleFS partition with this folder's contents.**
It does not merge files from the running board. Keep `config.json` in the folder
to preserve configuration. Download again before later edits if settings or
files on the board have changed. A folder containing only `www/` deliberately
removes saved configuration when uploaded.

Dumps contain configuration, potentially including the weather API key. Keep
them outside the Git checkout. Downloaded folders and raw backups are private
to the current user by default.

## Build an image without a board

```sh
tools/lfs-upload /path/to/lfs-work --pack-only /tmp/time-machine-lfs.bin
```

The output is always kept, independently of `--keep-image`. It must not already
exist and must be outside the source folder.
Packing excludes dotfiles and dot-directories at every depth, including
`.DS_Store`, `.git`, and their contents. This applies to uploads and `--pack-only`;
the original working folder is left untouched. Downloads still extract all files
already present on the board.

## Partition and error handling

The tools read `Firmware/TimeMachine/partitions.csv`; the current `spiffs`
partition is LittleFS at `0x390000`, size `0x60000` (384 KiB). The label is
retained from the ESP32 partition convention. Block/page sizes are 4096/256.
Use `--partition-file PATH` and `--partition LABEL` to select another table entry.

Before transferring filesystem data, the tools read the board's partition table
at `0x8000` and compare the selected entry with the CSV. A mismatch, invalid
table/checksum, or flagged partition stops the transfer. Upload packs and checks
the image size before contacting the board, so oversized content cannot reach
flash. Neither command erases the whole chip or replaces firmware or Wi-Fi NVS.

The board keeps its flasher stub active between the table check and data transfer.
After a successful download/unpack or upload, the tool waits one second, then
uses the normal Arduino uploader's entry/exit reset sequence.
The transfer itself
does not reboot it early. Offline `--pack-only` never contacts or resets a board.
If a transfer fails, press the board's reset button to resume its application.

Intermediate commands use `--after no-reset-stub`; following commands use
`--before no-reset-no-sync` to reuse that stub. The final read-only chip query
uses `--before default-reset --after hard-reset`, establishing the control-line
states before restarting. Skipping the entry sequence did not restart the
connected XIAO reliably. See
[Espressif's reset options](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/esptool/advanced-options.html).
Firmware USB logging must also be nonblocking (`Serial.setTxTimeoutMs(0)`), or
the running application can stall when no Serial Monitor is consuming output.
The complete download/reset sequence was verified on the connected ESP32-C6
revision 0.2 with that firmware setting and the USB monitor closed. Upload uses
the same reset helper; filesystem upload is covered by the simulated tests.

## Tests

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/tests -v
```

Tests use real `mklittlefs` images and a simulated esptool device. They check
binary/configuration preservation, edited-image round trips, layout/checksum
failures, output replacement/rollback, oversized input, and damaged/truncated
downloads, and delayed resets after successful transfers. No real board is contacted.

## Optional graphical editor

[LittleFS Explorer](https://github.com/isdsi/littlefs_explorer) offers partition
inspection, dump/flash operations, file editing, and drag-and-drop through a GUI.
Its documented setup uses Python, esptool, littlefs-python, and PySide6, with
Windows setup scripts. macOS and this ESP32-C6 board have not been verified here.

The existing `arduino/` helpers provide OTA layout installation and HTTP settings
backup/restore; these tools perform complete LittleFS image transfers instead.
