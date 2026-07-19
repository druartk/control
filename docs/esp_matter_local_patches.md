# Local ESP-Matter patches

External checkout:

- Path: `/home/k/esp/esp-matter`
- Branch: `release/v1.5`
- Commit: `8da2466dcab079e0721b68e3583558d9253dfcd5`

## PeerAddress namespace qualification

Patched file:

- `/home/k/esp/esp-matter/components/esp_matter_controller/commands/esp_matter_controller_pairing_command.cpp`

Original error from the `control` experimental Matter build:

```text
error: 'PeerAddress' was not declared in this scope
PeerAddress peerAddress = PeerAddress::UDP(nodeData.ipAddress[0], port, interfaceId);
```

The compiler suggested `chip::Transport::PeerAddress`. The official type is defined in:

```text
connectedhomeip/connectedhomeip/src/transport/raw/PeerAddress.h
```

Local change:

```diff
-    PeerAddress peerAddress = PeerAddress::UDP(nodeData.ipAddress[0], port, interfaceId);
+    chip::Transport::PeerAddress peerAddress = chip::Transport::PeerAddress::UDP(nodeData.ipAddress[0], port, interfaceId);
```

Reason this patch is external:

- The failing source belongs to the official external ESP-Matter checkout.
- `control` must not vendor or copy ESP-Matter sources.
- The `control` source tree only records the patch for reproducibility.

Saved diff:

- `docs/esp_matter_local_peeraddress_patch.diff`

Revert:

```bash
cd /home/k/esp/esp-matter
git checkout -- components/esp_matter_controller/commands/esp_matter_controller_pairing_command.cpp
```
