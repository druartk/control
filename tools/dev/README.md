# Developer automation

These scripts automate the existing ESP-IDF stable build, Windows-native ESP32
flashing, serial diagnostics, and usbipd WSL attachment. They do not change
firmware behavior, erase flash, or erase NVS.

## Project-root CLI

From the project root, use the wrapper for normal development:

```bash
./control build
./control flash
./control monitor
./control ui
```

`./control` supplies the pinned ESP-IDF and ESP-Matter paths, detects the
ESP32-S3 from runtime VID:PID `303a:1001` or bootloader VID:PID `303a:0009`,
and validates the Windows esptool installation. BUSID and Windows COM ports
are detected dynamically across USB re-enumeration.
The board currently requires manual bootloader mode before `flash` or
`flash-diag`; follow the printed instruction when prompted. The UI is the
existing dependency-free Web Serial page served at `http://localhost:8080`.

Use `./control status` for concise project and hardware state. Expert values
such as `CONTROL_USBIPD_BUSID` and `CONTROL_WINDOWS_ESPTOOL` remain supported.

## Build

```bash
./tools/dev/control.sh build
```

## Flash and automatic diagnostics

```bash
./tools/dev/control.sh flash-diag
```

`flash` and `flash-diag` build first, stop usbipd auto-attach watchers, release
the board to Windows, and wait for manual ESP32-S3 bootloader mode. The current
BUSID and Windows COM port are re-detected after every enumeration. A connection
failure with no serial data returns to the same physical prompt, up to three
attempts. `flash` leaves the board owned by Windows; `flash-diag` continues by
attaching it to WSL for diagnostics. Return the device to Windows for Chrome with:

```bash
./tools/dev/control.sh usb-stop
```

USB commands are also available directly:

```bash
./tools/dev/control.sh usb-start
./tools/dev/control.sh usb-status
./tools/dev/control.sh usb-stop
```

## Status

```bash
./tools/dev/control.sh status
```

## Interactive monitor

```bash
./tools/dev/control.sh monitor
```

The monitor is interactive and is mutually exclusive with both flashing and
Chrome Web Serial on the same port. `./control ui` stops every project
usbipd auto-attach watcher, releases an Attached board to Windows, and waits
for Windows COM enumeration before starting the server.

## Longer diagnostic capture

```bash
CONTROL_DIAG_SECONDS=60 ./tools/dev/control.sh diag
```

## Explicit port

```bash
CONTROL_PORT=/dev/ttyACM0 ./tools/dev/control.sh flash-diag
```

`CONTROL_PORT` selects the Linux port used by monitor/diag. Flashing always
targets Windows and gets the current COM port after bootloader enumeration;
it never runs esptool through `/dev/ttyACM*`. Monitor and diag target WSL.

## Windows protocol test

Select the current COM port in Chrome Web Serial, or pass that current port to
the optional Windows protocol test.

The test opens the supplied current port at 115200 with DTR and RTS disabled, waits ten seconds,
retries hello up to five times, then sends `get_status` and `get_configs`.
It ignores ordinary ESP-IDF log lines and matches protocol responses by
`request_id`. It does not save configuration.

## Environment overrides

The WSL script defaults to:

```text
CONTROL_IDF_EXPORT=/home/k/esp/esp-idf-v5.4.2/export.sh
CONTROL_MATTER_EXPORT=/home/k/esp/esp-matter/export.sh
CONTROL_BUILD_DIR=build_matter_stable
CONTROL_LOG_DIR=<project_root>/logs
SDKCONFIG=/tmp/control_sdkconfig_matter_stable
SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.recovery;sdkconfig.defaults.matter_stable
CONTROL_USBIPD_AUTO_ATTACH=1
CONTROL_USBIPD_BUSID=
CONTROL_USBIPD_VID=
CONTROL_USBIPD_PID=
CONTROL_USB_WAIT_SECONDS=20
CONTROL_FLASH_TRANSPORT=windows_com
CONTROL_WINDOWS_ESPTOOL=
```

`CONTROL_PORT`, `CONTROL_BUILD_DIR`, `CONTROL_LOG_DIR`, and the USB variables
may be overridden. Optional developer-only values can be placed in the
gitignored `tools/dev/control.local.env`, for example:

```text
CONTROL_USBIPD_BUSID=2-3
CONTROL_USBIPD_VID=303a
CONTROL_USBIPD_PID=1001
CONTROL_PORT=/dev/ttyACM0
```

Shell environment values take precedence over that file. The script checks
`usbipd --version` and `usbipd attach --help`; auto-attach support is required.
If the selected device is not shared, it prints the one-time command
`usbipd bind --busid <BUSID>` and never elevates or runs it automatically.

The watcher stores its Windows PID in `logs/usbipd-auto-attach.pid`. Its child
handles are intentionally not redirected: this prevents a long-running
usbipd process from keeping the WSL launcher attached.

Windows esptool is detected in this order: `CONTROL_WINDOWS_ESPTOOL`,
`py.exe -m esptool`, `python.exe -m esptool`, `esptool.exe`, then
`esptool.py`. If none is available, install it with:

```powershell
py -m pip install esptool
```

## Safety and port ownership

Chrome Web Serial and `idf.py monitor` cannot own the same serial port at the
same time. A USB device attached exclusively to WSL is unavailable to Chrome
on Windows. The browser still requires explicit user permission to select the
COM port. A device attached exclusively to WSL is also unavailable to Chrome
until `usb-stop` detaches it.

The toolkit does not bind devices or require an Administrator terminal. It only
attaches/detaches the explicitly selected device through the explicit USB
commands above; it never does so implicitly after `flash-diag`. It does not use
usbipd to kill unrelated processes or devices. A busy Linux
port is reported with its owning process and is never killed automatically.
The host must already have been shared once with `usbipd bind`.

`clean-build` requires explicit confirmation:

```bash
./tools/dev/control.sh clean-build --yes
```

It removes only `build_matter_stable` and
`/tmp/control_sdkconfig_matter_stable`, then rebuilds.
