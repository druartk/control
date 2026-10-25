#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
MOCK_BIN="$TMP/bin"
mkdir -p "$MOCK_BIN"

cat > "$MOCK_BIN/usbipd.exe" <<'MOCK_USBIPD'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" == list ]] || exit 0
printf '%s\n' "${MOCK_USB_LIST:-BUSID  VID:PID  DEVICE  STATE}"
MOCK_USBIPD

cat > "$MOCK_BIN/powershell.exe" <<'MOCK_POWERSHELL'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$*" == *'-VersionOnly'* ]]; then
    if [[ "$*" == *'-ControlWindowsEsptool'* ]]; then
        printf '%s\n' 'CONTROL_DEV: windows_esptool_executable=C:\Custom\python.exe'
    else
        printf '%s\n' 'CONTROL_DEV: windows_esptool_executable=C:\Users\K\AppData\Local\Programs\Python\Python314\python.exe'
    fi
    printf '%s\n' 'CONTROL_DEV: windows_esptool_prefix_arg[0]=-m'
    printf '%s\n' 'CONTROL_DEV: windows_esptool_prefix_arg[1]=esptool'
    exit 0
fi
exit 0
MOCK_POWERSHELL
chmod +x "$MOCK_BIN/usbipd.exe" "$MOCK_BIN/powershell.exe"

assert_contains() {
    local text="$1" expected="$2"
    grep -Fq -- "$expected" <<<"$text" || { printf 'FAIL: missing %s\n%s\n' "$expected" "$text" >&2; exit 1; }
}

output="$(PATH="$MOCK_BIN:$PATH" "$ROOT/control" help)"
assert_contains "$output" './control flash-diag'
printf '%s\n' 'CASE_CLI_A: PASS'

output="$(CONTROL_CLI_DRY_RUN=1 "$ROOT/control" flash 2>&1)"
assert_contains "$output" 'CONTROL_CLI: dispatch='
printf '%s\n' 'CASE_CLI_B: PASS'

output="$(CONTROL_CLI_DRY_RUN=1 "$ROOT/control" flash-diag 2>&1)"
assert_contains "$output" 'CONTROL_CLI: dispatch='
printf '%s\n' 'CASE_CLI_C: PASS'

grep -Fq 'ACTION_REQUIRED: Put the ESP32-S3 into bootloader mode now.' "$ROOT/tools/dev/control.sh" || { printf 'FAIL: manual bootloader action is not explicit\n' >&2; exit 1; }
grep -Fq 'Failed to connect to ESP32-S3: No serial data received' "$ROOT/tools/dev/control.sh" || { printf 'FAIL: retry trigger is missing\n' >&2; exit 1; }
printf '%s\n' 'CASE_CLI_D: PASS'

output="$(CONTROL_CLI_DRY_RUN=1 "$ROOT/control" ui)"
assert_contains "$output" 'CONTROL_CLI: target=windows'
assert_contains "$output" 'CONTROL_CLI: ui_command=python3 -m http.server 8080 --bind 127.0.0.1'
grep -Fq 'python -m http.server 8080' "$ROOT/tools/configurator/README.md" || { printf 'FAIL: configurator README does not document the resolved server\n' >&2; exit 1; }
printf '%s\n' 'CASE_CLI_H: PASS'

set +e
"$ROOT/control" unknown-command >/dev/null 2>&1
status=$?
set -e
(( status != 0 )) || { printf 'FAIL: unknown command unexpectedly passed\n' >&2; exit 1; }
printf '%s\n' 'CASE_CLI_I: PASS'
