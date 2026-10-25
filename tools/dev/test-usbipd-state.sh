#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
MOCK_BIN="$TMP/bin"
mkdir -p "$MOCK_BIN" "$TMP/logs"
CALLS="$TMP/usbipd.calls"
PS_CALLS="$TMP/powershell.calls"
IDF_CALLS="$TMP/idf.calls"
: > "$CALLS"
: > "$PS_CALLS"
: > "$IDF_CALLS"

cat > "$MOCK_BIN/usbipd.exe" <<'MOCK_USBIPD'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$MOCK_CALLS"
case "${1:-}" in
    --version) printf '%s\n' 'usbipd-win mock 1.0' ;;
    attach)
        [[ "${2:-}" == '--help' ]] && printf '%s\n' '--auto-attach --unplugged'
        ;;
    list)
        printf 'BUSID  VID:PID  DEVICE  STATE\n'
        if [[ -n "${MOCK_SEQUENCE_FILE:-}" && -s "$MOCK_SEQUENCE_FILE" ]]; then
            sed -n '1p' "$MOCK_SEQUENCE_FILE"
            tail -n +2 "$MOCK_SEQUENCE_FILE" > "$MOCK_SEQUENCE_FILE.next"
            mv "$MOCK_SEQUENCE_FILE.next" "$MOCK_SEQUENCE_FILE"
        else
            printf '%s  %s  ESP32-S3 (%s)  %s\n' \
                "${MOCK_USB_BUSID:-11-2}" "${MOCK_USB_ID:-303a:0009}" "${MOCK_COM:-COM3}" "$MOCK_USB_STATE"
        fi
        ;;
    detach)
        [[ "${MOCK_DETACH_TO_SHARED:-0}" == '1' && -n "${MOCK_STATE_FILE:-}" ]] && printf '%s\n' 'Shared' > "$MOCK_STATE_FILE"
        :
        ;;
esac
MOCK_USBIPD

cat > "$MOCK_BIN/powershell.exe" <<'MOCK_POWERSHELL'
#!/usr/bin/env bash
command_line="$*"
printf '%s\n' "$command_line" >> "$MOCK_PS_CALLS"
if [[ "$command_line" == *'GetPortNames'* ]]; then
    printf '%s\n' "${MOCK_WINDOWS_PORT-COM3}"
elif [[ "$command_line" == *'start-usbipd-watcher.ps1'* ]]; then
    printf '%s\n' '4242'
elif [[ "$command_line" == *'windows-esptool-flash.ps1'* ]]; then
    if [[ "$command_line" == *'-VersionOnly'* && "${MOCK_ESPTOOL_MISSING:-0}" != '1' ]]; then
        printf '%s\n' 'CONTROL_DEV: windows_esptool_executable=C:\\Python\\python.exe'
        printf '%s\n' 'CONTROL_DEV: windows_esptool_prefix_arg[0]=-m'
        printf '%s\n' 'CONTROL_DEV: windows_esptool_prefix_arg[1]=esptool'
        exit 0
    elif [[ "${MOCK_ESPTOOL_MISSING:-0}" == '1' ]]; then
        printf '%s\n' 'CONTROL_DEV: ERROR: Windows esptool is not installed'
        printf '%s\n' 'CONTROL_DEV: install_hint=py -m pip install esptool'
        exit 1
    fi
    case "${MOCK_WINDOWS_FLASH_RESULT:-success}" in
        exit7)
            printf '%s\n' 'CONTROL_DEV: windows_esptool_launch=starting'
            printf '%s\n' 'CONTROL_DEV: windows_esptool_pid=9007'
            printf '%s\n' 'CONTROL_DEV: windows_esptool_exit_code=7'
            exit 7
            ;;
        timeout)
            printf '%s\n' 'CONTROL_DEV: windows_esptool_launch=starting'
            printf '%s\n' 'CONTROL_DEV: windows_esptool_pid=9008'
            printf '%s\n' 'CONTROL_DEV: ERROR: Windows esptool timed out' >&2
            exit 124
            ;;
        external_kill)
            printf '%s\n' 'CONTROL_DEV: windows_esptool_launch=starting'
            printf '%s\n' 'CONTROL_DEV: windows_esptool_pid=9009'
            printf '%s\n' 'CONTROL_DEV: windows_esptool_exit_code=137'
            exit 137
            ;;
    esac
    printf '%s\n' 'CONTROL_DEV: windows_esptool=py.exe -m esptool'
    printf '%s\n' 'esptool mock version'
    printf '%s\n' 'CONTROL_DEV: windows_esptool_exit_code=0'
    printf '%s\n' 'CONTROL_DEV: windows_flash=PASS'
    if [[ "${MOCK_REENUMERATE:-0}" == '1' ]]; then
        rm -f -- "$MOCK_PORT"
        (sleep 0.05; : > "$MOCK_PORT") &
    fi
elif [[ "$command_line" == *'ProcessId=4242'* ]]; then
    printf '%s\n%s\n' 'usbipd.exe' "usbipd.exe attach --wsl --auto-attach --busid ${MOCK_BUSID:-11-2} --unplugged"
fi
MOCK_POWERSHELL
chmod +x "$MOCK_BIN/usbipd.exe" "$MOCK_BIN/powershell.exe"

cat > "$TMP/idf-export.sh" <<'MOCK_IDF_EXPORT'
#!/usr/bin/env bash
MOCK_IDF_EXPORT
cat > "$TMP/matter-export.sh" <<'MOCK_MATTER_EXPORT'
#!/usr/bin/env bash
MOCK_MATTER_EXPORT
cat > "$MOCK_BIN/idf.py" <<'MOCK_IDF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$MOCK_IDF_CALLS"
if [[ "$*" == *' flash'* && "${MOCK_RESET:-0}" == '1' ]]; then
    rm -f -- "$MOCK_PORT"
    (sleep 0.5; : > "$MOCK_PORT") &
    sleep 0.7
fi
if [[ "$*" == *' monitor'* ]]; then
    printf '%s\n' \
        'Control mock build ESP-IDF' \
        'boot_phase=nvs' \
        'got_ip' \
        'controller_ready=yes' \
        'saved_state=valid' \
        'écran=OK' \
        'tactile=OK' \
        'CONTROL_CONFIG: serial_init_start' \
        'CONTROL_CONFIG: init_step=queue result=ok' \
        'CONTROL_CONFIG: init_step=result_queue result=ok' \
        'CONTROL_CONFIG: init_step=parser_buffers result=ok' \
        'CONTROL_CONFIG: init_step=transport result=ok' \
        'CONTROL_CONFIG: init_step=task result=ok' \
        'CONTROL_CONFIG: serial_ready'
fi
MOCK_IDF
chmod +x "$TMP/idf-export.sh" "$TMP/matter-export.sh" "$MOCK_BIN/idf.py"

mkdir -p "$TMP/build/config"
: > "$TMP/build/control.bin"
mkdir -p "$TMP/build/bootloader" "$TMP/build/partition_table"
: > "$TMP/build/bootloader/bootloader.bin"
: > "$TMP/build/partition_table/partition-table.bin"
cat > "$TMP/build/config/sdkconfig.h" <<'MOCK_SDKCONFIG'
#define CONFIG_CONTROL_WEB_SERIAL_CONFIGURATOR 1
#define CONFIG_CONTROL_UI_DASHBOARD_V1 1
#define CONFIG_CONTROL_UI_MATTER_COMMANDS 1
#define CONFIG_CONTROL_UI_DEVICE_ONBOARDING 1
#define CONFIG_USE_MINIMAL_MDNS 1
#define CONFIG_CHIP_TASK_STACK_SIZE 24576
#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 24576
#define CONFIG_PARTITION_TABLE_FILENAME "partitions_matter.csv"
MOCK_SDKCONFIG

export PATH="$MOCK_BIN:$PATH"
export MOCK_CALLS="$CALLS" MOCK_PS_CALLS="$PS_CALLS" MOCK_IDF_CALLS="$IDF_CALLS"
export MOCK_PORT="$TMP/ttyACM0"
export CONTROL_USBIPD_BUSID=11-2 CONTROL_USBIPD_AUTO_ATTACH=1
export CONTROL_LOG_DIR="$TMP/logs" CONTROL_BUILD_DIR="$TMP/build"
export CONTROL_IDF_EXPORT="$TMP/idf-export.sh" CONTROL_MATTER_EXPORT="$TMP/matter-export.sh"

assert_contains() {
    local output="$1" text="$2"
    grep -Fqx "$text" <<<"$output" || {
        printf 'FAIL: missing: %s\n%s\n' "$text" "$output" >&2
        exit 1
    }
}

assert_matches() {
    local output="$1" pattern="$2"
    grep -Eq "$pattern" <<<"$output" || {
        printf 'FAIL: missing pattern: %s\n%s\n' "$pattern" "$output" >&2
        exit 1
    }
}

assert_no_real_attach() {
    ! grep -Fq 'attach --wsl --auto-attach' "$CALLS" || {
        printf 'FAIL: direct attach command was executed\n' >&2
        cat "$CALLS" >&2
        exit 1
    }
}

assert_no_attach_command() {
    ! grep -Eq '^attach([[:space:]]|$)' "$CALLS" || {
        printf 'FAIL: attached state executed an attach command\n' >&2
        cat "$CALLS" >&2
        exit 1
    }
}

assert_forwarded_override() {
    local expected="$1"
    grep -F -- "-ControlWindowsEsptool $expected" "$PS_CALLS" >/dev/null || {
        printf 'FAIL: CONTROL_WINDOWS_ESPTOOL was not forwarded unchanged\n' >&2
        cat "$PS_CALLS" >&2
        exit 1
    }
}

assert_start_process() {
    grep -Fq 'start-usbipd-watcher.ps1' "$PS_CALLS" || {
        printf 'FAIL: shared/flash state did not use the detached launcher\n' >&2
        cat "$PS_CALLS" >&2
        exit 1
    }
}

PORT="$TMP/ttyACM0"

export MOCK_USB_STATE=Attached
: > "$PORT"
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" usb-start 2>&1)"
assert_contains "$output" 'CONTROL_DEV: attach_state_branch=attached_fast_path'
assert_contains "$output" "CONTROL_DEV: serial_port=$PORT"
assert_contains "$output" 'CONTROL_DEV: usb_attach=PASS'
assert_no_attach_command
assert_no_real_attach
printf '%s\n' 'CASE_A: PASS'

rm -f -- "$PORT" "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
export MOCK_USB_STATE=Attached
: > "$PS_CALLS"
: > "$CALLS"
: > "$IDF_CALLS"
printf '%s\n' '4242' > "$TMP/logs/usbipd-auto-attach.pid"
printf '%s\n' '11-2' > "$TMP/logs/usbipd-auto-attach.busid"
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash 2>&1)"
assert_contains "$output" 'CONTROL_DEV: flash_transport=windows_com'
assert_contains "$output" 'CONTROL_DEV: windows_port=COM3'
assert_contains "$output" 'CONTROL_DEV: usbipd_auto_attach=stopped'
assert_contains "$output" 'CONTROL_DEV: usbipd_detach=PASS'
grep -Fq 'detach --busid 11-2' "$CALLS" || { printf 'FAIL: BUSID was not detached\n' >&2; exit 1; }
! grep -Fq ' flash' "$IDF_CALLS" || { printf 'FAIL: WSL flash was invoked\n' >&2; exit 1; }
assert_no_real_attach
printf '%s\n' 'CASE_E: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
EXPECTED_OVERRIDE='C:\Users\K\AppData\Local\Programs\Python\Python314\python.exe -m esptool'
export CONTROL_WINDOWS_ESPTOOL="$EXPECTED_OVERRIDE" MOCK_USB_STATE=Attached
: > "$PS_CALLS"
: > "$CALLS"
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash 2>&1)"
assert_contains "$output" 'CONTROL_DEV: flash_transport=windows_com'
assert_forwarded_override "$EXPECTED_OVERRIDE"
unset CONTROL_WINDOWS_ESPTOOL
printf '%s\n' 'CASE_H_FORWARD: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
:
export MOCK_USB_STATE=Shared
: > "$PS_CALLS"
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" usb-start 2>&1)"
assert_contains "$output" 'CONTROL_DEV: attach_state_branch=shared_auto_attach'
assert_contains "$output" 'CONTROL_DEV: usbipd_auto_attach=started'
assert_contains "$output" 'CONTROL_DEV: usbipd_pid=4242'
grep -Fq 'start-usbipd-watcher.ps1' "$PS_CALLS" || {
    printf 'FAIL: shared state did not use the detached launcher\n' >&2
    cat "$PS_CALLS" >&2
    exit 1
}
printf '%s\n' 'CASE_C: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
export MOCK_REENUMERATE=1 MOCK_USB_STATE=Shared
: > "$PS_CALLS"
: > "$CALLS"
: > "$IDF_CALLS"
output="$(CONTROL_DIAG_SECONDS=1 CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash-diag 2>&1)"
assert_contains "$output" 'CONTROL_DEV: flash_transport=windows_com'
assert_contains "$output" 'CONTROL_DEV: windows_port=COM3'
assert_contains "$output" 'CONTROL_DEV: usb_attach=PASS'
assert_contains "$output" 'CONTROL_DEV: verdict=PASS'
! grep -Fq ' flash' "$IDF_CALLS" || { printf 'FAIL: reset test used WSL flash\n' >&2; exit 1; }
grep -Fq ' monitor' "$IDF_CALLS" || { printf 'FAIL: reset test did not continue to diagnostics\n' >&2; exit 1; }
printf '%s\n' 'CASE_D: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
export MOCK_REENUMERATE=0 MOCK_ESPTOOL_MISSING=1 MOCK_USB_STATE=Attached
set +e
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash 2>&1)"
status=$?
set -e
(( status != 0 )) || { printf 'FAIL: missing Windows esptool unexpectedly passed\n' >&2; exit 1; }
assert_contains "$output" 'CONTROL_DEV: ERROR: Windows esptool is not installed'
assert_contains "$output" 'CONTROL_DEV: install_hint=py -m pip install esptool'
! grep -Fq ' flash' "$IDF_CALLS" || { printf 'FAIL: missing Windows esptool fell back to WSL flash\n' >&2; exit 1; }
printf '%s\n' 'CASE_F: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
: > "$PS_CALLS"
: > "$CALLS"
: > "$IDF_CALLS"
export MOCK_ESPTOOL_MISSING=0 MOCK_WINDOWS_FLASH_RESULT=exit7 MOCK_REENUMERATE=0 MOCK_USB_STATE=Attached
set +e
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash-diag 2>&1)"
status=$?
set -e
(( status != 0 )) || { printf 'FAIL: non-zero Windows flash unexpectedly continued\n%s\n' "$output" >&2; exit 1; }
assert_contains "$output" 'CONTROL_DEV: windows_flash=FAIL'
! grep -Fq 'CONTROL_DEV: usbipd_auto_attach=started' <<<"$output" || { printf 'FAIL: failed flash started usbipd auto-attach\n' >&2; exit 1; }
! grep -Fq 'CONTROL_DEV: usb_attach=PASS' <<<"$output" || { printf 'FAIL: failed flash reported USB attach success\n' >&2; exit 1; }
! grep -Fq ' monitor' "$IDF_CALLS" || { printf 'FAIL: failed flash entered diagnostics\n' >&2; exit 1; }
printf '%s\n' 'CASE_P: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
export MOCK_WINDOWS_FLASH_RESULT=external_kill
: > "$PS_CALLS"
: > "$CALLS"
: > "$IDF_CALLS"
set +e
output="$(CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash-diag 2>&1)"
status=$?
set -e
(( status != 0 )) || { printf 'FAIL: externally terminated Windows flash unexpectedly passed\n%s\n' "$output" >&2; exit 1; }
assert_contains "$output" 'CONTROL_DEV: windows_esptool_exit_code=137'
assert_contains "$output" 'CONTROL_DEV: windows_flash=FAIL'
! grep -Fq 'CONTROL_DEV: post_flash_usb_attach=starting' <<<"$output" || { printf 'FAIL: externally terminated flash entered post-flash attach\n' >&2; exit 1; }
! grep -Fq ' monitor' "$IDF_CALLS" || { printf 'FAIL: externally terminated flash entered diagnostics\n' >&2; exit 1; }
printf '%s\n' 'CASE_Q: PASS'

rm -f -- "$TMP/logs/usbipd-auto-attach.pid" "$TMP/logs/usbipd-auto-attach.busid"
: > "$PORT"
export MOCK_ESPTOOL_MISSING=0 MOCK_WINDOWS_FLASH_RESULT=success MOCK_REENUMERATE=1 MOCK_USB_STATE=Attached
: > "$PS_CALLS"
: > "$CALLS"
: > "$IDF_CALLS"
output="$(CONTROL_DIAG_SECONDS=1 CONTROL_PORT="$PORT" "$ROOT/tools/dev/control.sh" flash-diag 2>&1)"
assert_contains "$output" 'CONTROL_DEV: flash_transport=windows_com'
assert_contains "$output" 'CONTROL_DEV: windows_esptool_exit_code=0'
assert_contains "$output" 'CONTROL_DEV: windows_flash=PASS'
assert_contains "$output" 'CONTROL_DEV: verdict=PASS'
grep -Fq ' monitor' "$IDF_CALLS" || { printf 'FAIL: successful Windows flash did not continue to diagnostics\n' >&2; exit 1; }
! grep -Fq ' flash' "$IDF_CALLS" || { printf 'FAIL: successful Windows flash invoked WSL flash\n' >&2; exit 1; }
printf '%s\n' 'CASE_R: PASS'

unset CONTROL_USBIPD_BUSID
printf '%s\n' '4-8  303a:1001  ESP32-S3 (COM4)  Shared' > "$TMP/runtime.sequence"
export MOCK_SEQUENCE_FILE="$TMP/runtime.sequence" MOCK_WINDOWS_PORT=COM4 MOCK_USB_STATE=Shared
output="$(CONTROL_LOG_DIR="$TMP/logs" "$ROOT/control" status 2>&1)"
assert_contains "$output" 'CONTROL_CLI: usbipd_busid=4-8'
assert_contains "$output" 'CONTROL_CLI: usb_vidpid=303a:1001'
assert_contains "$output" 'CONTROL_CLI: windows_port=COM4'
printf '%s\n' 'CASE_ID_RUNTIME: PASS'

printf '%s\n' '7-1  303a:0009  ESP32-S3 (COM3)  Shared' > "$TMP/bootloader.sequence"
export MOCK_SEQUENCE_FILE="$TMP/bootloader.sequence" MOCK_WINDOWS_PORT=COM3
output="$(CONTROL_LOG_DIR="$TMP/logs" "$ROOT/control" status 2>&1)"
assert_contains "$output" 'CONTROL_CLI: usbipd_busid=7-1'
assert_contains "$output" 'CONTROL_CLI: usb_vidpid=303a:0009'
assert_contains "$output" 'CONTROL_CLI: windows_port=COM3'
printf '%s\n' 'CASE_ID_BOOTLOADER: PASS'

printf '%s\n' \
    '4-8  303a:1001  ESP32-S3 (COM4)  Attached' \
    '4-8  303a:1001  ESP32-S3 (COM4)  Attached' \
    '7-1  303a:0009  ESP32-S3 (COM3)  Shared' > "$TMP/transition.sequence"
printf '%s\n' '4242' > "$TMP/logs/usbipd-auto-attach.pid"
printf '%s\n' '4-8' > "$TMP/logs/usbipd-auto-attach.busid"
export MOCK_SEQUENCE_FILE="$TMP/transition.sequence" MOCK_WINDOWS_PORT=COM3 MOCK_USB_STATE=Shared
export MOCK_BUSID=4-8
: > "$PS_CALLS"
output="$(CONTROL_LOG_DIR="$TMP/logs" CONTROL_USB_WAIT_SECONDS=2 "$ROOT/tools/dev/control.sh" usb-release-ui 2>&1)"
assert_contains "$output" 'CONTROL_DEV: usbipd_auto_attach=stopped'
assert_contains "$output" 'CONTROL_DEV: usb_vidpid=303a:0009'
assert_contains "$output" 'CONTROL_DEV: windows_port=COM3'
! grep -Fq 'start-usbipd-watcher.ps1' "$PS_CALLS" || { printf 'FAIL: UI started a watcher\n' >&2; exit 1; }
printf '%s\n' 'CASE_ID_TRANSITION: PASS'

printf '%s\n' \
    '4-8  303a:1001  ESP32-S3 (COM4)  Attached' \
    '4-8  303a:1001  ESP32-S3 (COM4)  Attached' \
    '4-8  303a:1001  ESP32-S3 (COM4)  Shared' > "$TMP/no-com.sequence"
export MOCK_SEQUENCE_FILE="$TMP/no-com.sequence" MOCK_WINDOWS_PORT=''
set +e
output="$(CONTROL_LOG_DIR="$TMP/logs" CONTROL_USB_WAIT_SECONDS=1 "$ROOT/tools/dev/control.sh" usb-release-ui 2>&1)"
status=$?
set -e
(( status != 0 )) || { printf 'FAIL: UI started without a Windows COM port\n' >&2; exit 1; }
! grep -Fq 'CONTROL_DEV: usb_release_ui=PASS' <<<"$output" || { printf 'FAIL: UI passed without a Windows COM port\n' >&2; exit 1; }
printf '%s\n' 'CASE_ID_UI_GATE: PASS'
