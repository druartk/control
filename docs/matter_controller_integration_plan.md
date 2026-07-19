# Control ESP-Matter Controller Integration Plan

Status date: 2026-07-14

Target hardware:

- LILYGO T5 4.7 Plus V2.4
- PCB date 2024-12-03
- ESP32-S3-WROOM-1-N16R8
- Wi-Fi/BLE available
- No native 802.15.4 radio; do not plan Thread commissioning on this board

Current safe firmware baseline:

- Display, touch, Wi-Fi, IKEA known-IP diagnostics, and Matter code entry are working.
- Matter commissioning is stubbed.
- `CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL` is disabled by default.
- The vendored LILYGO display driver and full-refresh sequence must remain untouched.

## Local availability audit

Checked locations:

- `$ESP_MATTER_PATH`: not set.
- `~/esp/esp-matter`: not present.
- `/opt/esp-matter`: not present.
- `/tmp/esp-matter-v1.5`: present, but only as a partial/sparse checkout.

Local partial checkout:

- Path: `/tmp/esp-matter-v1.5`
- Branch: `release/v1.5`
- Commit: `8da2466dcab079e0721b68e3583558d9253dfcd5`
- Usable as build dependency: no.
- Reason: most repository files are not checked out and show as deleted in `git status`; only top-level metadata such as `README.md`, `RELEASE_NOTES.md`, and `idf_component.yml` is present.

The partial checkout confirms that official ESP-Matter v1.5 contains:

- `components/esp_matter_controller`
- `examples/controller`
- `docs/en/controller.rst`
- controller pairing/read/write/subscribe command sources in the repository index

It does not provide enough checked-out source to compile.

## Official setup options

### Option A: External `ESP_MATTER_PATH`

Preferred for the first real controller proof-build because it matches Espressif's controller examples and includes tools/submodules.

Recommended setup, from WSL/Linux filesystem, not under `/mnt/c`:

```bash
cd ~/esp
git clone --recursive https://github.com/espressif/esp-matter.git
cd esp-matter
git checkout release/v1.5
git submodule update --init --recursive
./install.sh
source ~/esp/esp-idf-v5.4.2/export.sh
source ~/esp/esp-matter/export.sh
export ESP_MATTER_PATH=~/esp/esp-matter
```

If cloning is slow or unreliable, use Espressif's documented shallow flow:

```bash
cd ~/esp
source ~/esp/esp-idf-v5.4.2/export.sh
git clone --depth 1 --branch release/v1.5 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
cd connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow
cd ../..
./install.sh
source ./export.sh
export ESP_MATTER_PATH=~/esp/esp-matter
```

Notes:

- Espressif's v1.5 component page recommends ESP-IDF v5.4.1. The current project uses ESP-IDF v5.4.2, which is close but must be proof-built.
- ESP-Matter v1.5 references connectedhomeip commit `cf84d0360c` in the component registry readme. The local partial checkout readme mentions `b87051a95f`; treat the complete installed checkout as authoritative.

### Option B: Managed component

Official component registry entry:

```bash
idf.py add-dependency "espressif/esp_matter^1.5.0"
```

Local component manager version:

```text
idf-component-manager / compote 2.4.11
```

This satisfies Espressif's documentation requirement for component manager `1.4.*` or `>= 2.0`.

The managed component is plausible for final integration, but not the first proof step, because:

- it will add many dependencies;
- it may conflict with the currently locked `espressif/mdns` dependency version;
- it does not include the full example layout as a standalone proof target;
- binary size and partition impact must be measured first.

### Option C: Separate proof project

Recommended proof-build before touching the stable UI firmware:

```bash
cd /mnt/c/Users/Kovac/Documents/Arduino
cp -a "$ESP_MATTER_PATH/examples/controller" control_matter_commissioner_test
cd control_matter_commissioner_test
source ~/esp/esp-idf-v5.4.2/export.sh
source "$ESP_MATTER_PATH/export.sh"
idf.py set-target esp32s3
idf.py build
```

If the official example does not build with ESP-IDF v5.4.2, repeat with ESP-IDF v5.4.1 before changing `control`.

Only after this proof project builds should `control` link against `esp_matter_controller`.

## Current partition and memory audit

Current app build:

- `control.bin`: `0x108bc0`
- app partition: `0x177000`
- free in app partition: `0x6e440` / about 29%
- total image size reported by `idf.py size`: `1084236` bytes

Current memory summary:

- DIRAM used: `127319` bytes / 37.25%
- DIRAM remaining: `214441` bytes
- IRAM used: `16383` bytes / effectively full reported 16 KiB region
- e-paper framebuffer: 259200 bytes in PSRAM

Expected impact:

- ESP-Matter controller/commissioner will likely exceed the current 1500 KiB factory app partition.
- BLE commissioning may increase RAM pressure.
- Matter fabric, controller certificates, device list, and attribute storage require more NVS than the current 24 KiB default.

Do not add ESP-Matter to the stable firmware until a partition table is selected.

Recommended future partition direction:

- 16 MB flash allows a larger single factory app partition for early development.
- Increase NVS for Matter fabric/controller storage.
- Consider a future factory-reset/data partition.
- Do not switch to OTA until the Matter image size is known; OTA halves available app space unless using a larger custom layout.

## Matter controller API mapping

Current Control abstraction:

```cpp
esp_err_t matter_controller_init();
bool matter_controller_is_ready();

esp_err_t matter_controller_commission_with_manual_code(
    const std::string &normalized_code,
    uint64_t preferred_node_id
);

esp_err_t matter_controller_get_last_result(MatterCommissioningResult &out);
```

Official ESP-Matter concepts to map:

- Controller/commissioner component: `esp_matter_controller`.
- Commissioning shell commands are exposed under `matter esp controller ...`.
- Pairing methods in official docs include:
  - BLE Wi-Fi commissioning with setup passcode and discriminator.
  - manual pairing code Wi-Fi commissioning (`pairing code-wifi <node-id> <ssid> <passphrase> <manual-code>`).
  - QR payload commissioning.
  - on-network commissioning where applicable.
- Manual setup payload must be parsed by the official Matter setup payload parser, not by custom checksum logic.
- NOC issuer and attestation trust store need explicit configuration.

Expected mapping:

| Control API | Official stack work |
| --- | --- |
| `matter_controller_init()` | initialize CHIP platform, persistent storage, commissioner/controller client, attestation trust store, NOC issuer |
| `matter_controller_is_ready()` | true after commissioner/controller setup succeeds |
| `matter_controller_commission_with_manual_code()` | parse manual code with official setup payload parser, assign node ID, start commissioning through controller pairing API |
| `matter_controller_get_last_result()` | return result populated by Matter callbacks/events, never directly refresh display |

Known mismatch:

- Control currently stores only normalized 11 digits. The official stack may require a full onboarding payload object containing discriminator, passcode, and discovery capability. The next code step should replace simple format validation with the official parser and store a parsed payload object.

## Runtime safety rules for integration

- Commissioning starts only from explicit user action.
- Matter callbacks must enqueue small app events only.
- No display refresh from Matter, BLE, Wi-Fi, or worker callbacks.
- UI renders once per state transition.
- Full pairing code must never be logged.
- Do not claim IKEA/DIRIGERA identity until commissioning succeeds and vendor/product/basic information is read.

## Recommended next proof sequence

1. Install a complete official ESP-Matter checkout outside `/mnt/c`.
2. Build the official `examples/controller` for ESP32-S3.
3. Record binary size, sdkconfig, partition table, and NVS requirements.
4. If successful, create `control_matter_commissioner_test` next to `control` from the official example.
5. Only then enable a new `control` experimental build path:
   - add `ESP_MATTER_PATH` CMake integration;
   - require `esp_matter_controller`;
   - include official setup payload parser headers;
   - keep commissioning explicit and event-driven.
6. Revisit the partition table before linking ESP-Matter into `control`.

## Do not do yet

- Do not implement dashboards.
- Do not implement device control.
- Do not fake commissioning success.
- Do not modify `components/lilygo_epd47`.
- Do not add Thread commissioning on ESP32-S3.
