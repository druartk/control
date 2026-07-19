# Matter device cache and endpoint classification

After successful Matter commissioning, the controller reuses the ESP-Matter
controller/fabric storage and the app metadata in `control_matter`.

Saved app metadata:

- `commissioned`
- `node_id`
- `fabric_idx`
- `ctrl_node`
- `pref_node`
- `comm_mode` (`1` first pairing, `2` multi-admin)
- `last_addr` diagnostic last mDNS pre-scan address, if seen
- `last_host` diagnostic last mDNS pre-scan host, if seen
- `att_verified`
- `att_bypassed`
- `dev_insecure`
- `commissioned_at`

Discovered endpoint cache is stored as a compact NVS blob:

- namespace: `control_matter_devices`
- key: `devices`
- format: versioned binary header plus fixed-size endpoint records

Cached fields per endpoint:

- endpoint id
- display name
- type label
- device type
- primary cluster class
- OnOff / LevelControl / ColorControl / Switch flags
- reachable flag if known
- last known OnOff state if read
- last seen timestamp based on UTC if available, otherwise boot time

Wi-Fi reset only clears Wi-Fi credentials. It does not clear Matter metadata or
the device cache.

`Reset Matter local complet` clears:

- `control_matter`
- `control_matter_devices`
- `CHIP_KVS`
- `chip-config`
- `chip-counters`
- `chip-opcert`

It preserves Wi-Fi and does not clear `chip-factory`.

## Endpoint names

For each endpoint, discovery tries official Matter attributes in this order:

1. Bridged Device Basic Information / NodeLabel
2. Basic Information / NodeLabel
3. Bridged Device Basic Information / ProductName
4. Basic Information / ProductName
5. Fallback: friendly type + endpoint id

Fallback examples:

- `Lumière 12`
- `Interrupteur 52`
- `Appareil On/Off 18`

Names are not invented. If no label/product name exists, the fallback is based
only on the discovered clusters/device types.

## Classification

Classification uses Descriptor / ServerList first, then DeviceTypeList:

- OnOff cluster `0x0006`: controllable On/Off endpoint.
- LevelControl cluster `0x0008`: dimmable capability.
- ColorControl cluster `0x0300`: color-light capability.
- Switch cluster `0x003B`: switch endpoint.
- OnOff plus known light device type: `Lumière`.
- OnOff without known light device type: `Appareil On/Off`.
- Switch without OnOff: `Interrupteur`.
- No controllable cluster: hidden from the default device page.

Endpoint 52 with ServerList `[59, 29, 3]` is classified as:

- Switch `0x003B`
- Descriptor `0x001D`
- Identify `0x0003`
- no OnOff
- UI type: `Interrupteur`

It is not treated as a light and On/Off commands are rejected for it.

## Normal boot

Expected after a reboot with saved Wi-Fi and saved Matter:

```text
CONTROL_WIFI: load_wifi_credentials found=yes
CONTROL_WIFI: auto_connect start ssid="..."
CONTROL_WIFI: got_ip ...
CONTROL_MATTER: saved_state=valid remote_node_id=...
CONTROL_MATTER: controller_state ... -> ControllerReady
CONTROL_MATTER: device_cache_load count=...
```

The UI opens Matter home with `Appairage enregistré`. It does not ask for a
Matter code unless the user chooses `Réappairer`.

## Refresh

`Rafraîchir appareils` performs recursive endpoint discovery from endpoint 0,
reads names, reads OnOff state where supported, updates the NVS cache, and then
renders once after the refresh completes.
