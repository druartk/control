# Matter SendNOC / AddNOC status 9 recovery

`SendNOC` failure with device NOCResponse status `9` maps to
`NodeOperationalCertStatusEnum::kFabricConflict` in the local CHIP checkout.

Use this clean procedure when the device rejects AddNOC:

1. In IKEA Home smart, remove/forget the previous Matter pairing or integration if visible.
2. On the ESP32 UI, press **Reset Matter local complet**.
3. Reboot the ESP32.
4. Generate a new Matter code from IKEA.
5. Pair once with node id `0x12344321`.
6. If SendNOC returns status `9` again, dump the local storage summary and capture logs.

## Initial commissioning vs multi-admin

If DIRIGERA is already paired to Google Home or another fabric, do not reuse an
old initial commissioning code. Open a new sharing / multi-admin commissioning
window from Google Home or IKEA Home smart, generate a fresh Matter code, and
select **Ajouter via multi-admin** on the ESP32.

Use **Premier appairage Matter** only when DIRIGERA is unpaired, factory reset,
or otherwise in its first commissioning window.

The ESP-Matter controller helper used by this app is
`esp_matter::controller::pairing_code(node_id, payload)`. In this checkout it
uses CHIP `PairDevice(..., DiscoveryType::kDiscoveryNetworkOnly)`, so DNS-SD
commissionable discovery must work for the normal path.

There is no official ESP-Matter helper exposed here for “manual code + direct
IP address”. The lower CHIP controller API can be used with
`RendezvousParameters::SetPeerAddress(...)` plus parsed setup PIN, but adding
that would be a custom pairing wrapper. Until that wrapper is intentionally
implemented, direct-IP commissioning is documented as unavailable and DNS-SD is
required.

The complete local Matter reset preserves Wi-Fi credentials and clears only local
Matter/controller storage:

- `control_matter`
- `CHIP_KVS`
- `chip-config`
- `chip-counters`
- `chip-opcert`, if present

It deliberately does not clear `chip-factory`.

If selective reset is still not enough, use the documented development fallback:

1. Run `idf.py erase-flash`.
2. Reflash firmware.
3. Reconfigure Wi-Fi.
4. Remove stale Matter pairing/integration from IKEA Home smart.
5. Generate a fresh Matter code.
6. Pair once.

Do not treat PAA-missing logs as fatal in dev attestation-bypass builds. In that
configuration, the expected final blocker for this failure mode is
`send_noc_fabric_conflict`, not `attestation_failed`.
