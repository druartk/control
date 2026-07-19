# Matter persistence and recovery

## Storage model

The app stores only UI metadata in the `control_matter` NVS namespace:

- commissioned flag
- remote commissioned node id
- fabric index, when known
- attestation flags
- commissioning timestamp or boot counter

ESP-Matter stores the real controller fabric, operational credentials, and CASE/keystore state in CHIP storage. App metadata is only a pointer for UI flow. It is not proof that the controller can open a session.

The controller node id is `0x1B669`. The remote DIRIGERA node id is the node id passed to commissioning, currently `0x12344321`, or the commissioned node id reported by ESP-Matter. These are different concepts. A saved remote node id of `0` or `0x1B669` is invalid.

## Canonical saved states

Every Matter UI and controller path uses one validator:

```text
MatterSavedState::None
MatterSavedState::Valid
MatterSavedState::Invalid
```

Invalid examples:

- `remote_node_id_zero`
- `remote_node_id_is_controller`
- `partial_metadata`
- `read_error`

Expected logs:

```text
CONTROL_MATTER: saved_state=none
CONTROL_MATTER: saved_state=valid remote_node_id=...
CONTROL_MATTER: saved_state=invalid reason=remote_node_id_zero
CONTROL_MATTER: controller_init_skipped reason=invalid_saved_metadata
```

If metadata is invalid, the app does not call ESP-Matter controller init, discovery, reads, or On/Off commands.

## Commissioning persistence

Commissioning success is only shown after a valid remote node id is selected and metadata is saved. The source priority is:

1. ESP-Matter commissioning success callback node id
2. Preferred pairing node id, currently `0x12344321`
3. no fallback

Node id `0` and the controller node id are refused:

```text
CONTROL_MATTER: save_matter_node_metadata refused reason=invalid_remote_node_id
```

## Local Matter reset

`Oublier Matter` clears local Matter state and preserves Wi-Fi credentials.

Cleared namespaces:

- `control_matter`
- `CHIP_KVS`
- `chip-config`
- `chip-counters`

Not cleared:

- Wi-Fi NVS namespaces
- normal app settings
- `chip-factory`, because official CHIP ESP32 sources define it as factory/manufacturing data

Expected logs:

```text
CONTROL_MATTER: forget_start
CONTROL_MATTER: clear_app_metadata result=ESP_OK
CONTROL_MATTER: clear_matter_namespace namespace=CHIP_KVS result=...
CONTROL_MATTER: clear_matter_namespace namespace=chip-config result=...
CONTROL_MATTER: clear_matter_namespace namespace=chip-counters result=...
CONTROL_MATTER: wifi_preserved=yes
CONTROL_MATTER: forget_done
```

After reset, the UI shows `Matter oublié` and offers `Redémarrer`. Reboot before re-pairing so ESP-Matter restarts from the cleared storage state.

## Time and heap gates

Matter startup waits up to 10 seconds for SNTP and only reports valid UTC if system time is later than `2024-01-01T00:00:00Z`.

With saved Wi-Fi credentials, the boot order is now:

1. NVS and app settings
2. Wi-Fi station init and headless reconnect
3. bounded SNTP attempt
4. Matter platform and controller factory init
5. display framebuffer, e-paper driver, touch, and UI

This keeps the large contiguous internal allocation needed by `DeviceControllerFactory::Init(...)` ahead of display/touch/UI allocations and fragmentation.

Early controller init logs the largest internal heap block. If it is below 40 KB, the app logs a warning but still tries because this is the earliest boot phase:

```text
CONTROL_MATTER: low_heap_preflight_warning largest_internal=...
CONTROL_MATTER: attempting_controller_init_despite_warning=yes
```

Late controller init keeps the 40 KB protection. If a late path is reached with too little internal heap, the app fails early with:

```text
CONTROL_MATTER: controller_init_failed reason=low_internal_heap_for_device_controller_factory
```

This avoids repeated `DeviceControllerFactory` init attempts in a known bad memory state. If early init itself fails after the Matter platform is started, the UI shows `Matter indisponible` / `Mémoire interne insuffisante` and offers one-shot retry or local Matter reset.

## Recovery procedure

Preferred recovery:

1. Press `Oublier Matter`.
2. Press `Redémarrer`.
3. Reconnect Wi-Fi if needed.
4. Open `Matter / IKEA`.
5. Press `Appairer avec code`.
6. Pair again with a fresh IKEA Home smart Matter code.
7. Reboot once more.
8. Press `Utiliser Matter`.

If `Utiliser Matter` still fails, capture logs around:

```text
CONTROL_MATTER: saved_state
CONTROL_MATTER: time_valid_utc
CONTROL_HEAP: before_early_matter_init
CONTROL_HEAP: before_controller_client_init
CONTROL_MATTER: controller_state
MatterController: init
```
