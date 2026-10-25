#!/usr/bin/env bash
set -Eeuo pipefail

canonical_project_path() {
    local value="$1" lower root_lower
    root_lower='/mnt/c/users/k/documents/arduino/control'
    lower="${value,,}"
    if [[ "$lower" == "$root_lower"* ]]; then
        printf '/mnt/c/Users/K/Documents/Arduino/control%s\n' "${value:${#root_lower}}"
    else
        printf '%s\n' "$value"
    fi
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_ROOT="$(canonical_project_path "$(cd -- "$SCRIPT_DIR/../.." && pwd -P)")"

load_local_environment() {
    local file="$SCRIPT_DIR/control.local.env"
    [[ -f "$file" ]] || return 0

    local names=(CONTROL_IDF_EXPORT CONTROL_MATTER_EXPORT CONTROL_BUILD_DIR CONTROL_LOG_DIR
        CONTROL_PORT CONTROL_USBIPD_AUTO_ATTACH CONTROL_USBIPD_BUSID CONTROL_USBIPD_VID
        CONTROL_USBIPD_PID CONTROL_USB_WAIT_SECONDS CONTROL_DIAG_SECONDS CONTROL_FLASH_TRANSPORT
        CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS CONTROL_WINDOWS_ESPTOOL)
    local name value
    declare -A saved=()
    for name in "${names[@]}"; do
        if [[ -v "$name" ]]; then
            saved["$name"]="${!name}"
        fi
    done

    set +u
    # shellcheck disable=SC1090
    source "$file"
    set -u

    for name in "${!saved[@]}"; do
        value="${saved[$name]}"
        printf -v "$name" '%s' "$value"
    done
}

load_local_environment

CONTROL_IDF_EXPORT="${CONTROL_IDF_EXPORT:-/home/k/esp/esp-idf-v5.4.2/export.sh}"
CONTROL_MATTER_EXPORT="${CONTROL_MATTER_EXPORT:-/home/k/esp/esp-matter/export.sh}"
CONTROL_BUILD_DIR_INPUT="${CONTROL_BUILD_DIR:-build_matter_stable}"
CONTROL_LOG_DIR_INPUT="${CONTROL_LOG_DIR:-$PROJECT_ROOT/logs}"
SDKCONFIG_PATH="/tmp/control_sdkconfig_matter_stable"
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.recovery;sdkconfig.defaults.matter_stable"
CONTROL_DIAG_SECONDS="${CONTROL_DIAG_SECONDS:-20}"
CONTROL_USBIPD_AUTO_ATTACH="${CONTROL_USBIPD_AUTO_ATTACH:-1}"
CONTROL_USBIPD_BUSID="${CONTROL_USBIPD_BUSID:-}"
CONTROL_USBIPD_VID="${CONTROL_USBIPD_VID:-303a}"
CONTROL_USBIPD_PID="${CONTROL_USBIPD_PID:-}"
CONTROL_USB_WAIT_SECONDS="${CONTROL_USB_WAIT_SECONDS:-20}"
CONTROL_FLASH_TRANSPORT="${CONTROL_FLASH_TRANSPORT:-windows_com}"
CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS="${CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS:-180}"

resolve_project_path() {
    local value="$1"
    if [[ "$value" = /* ]]; then
        canonical_project_path "$value"
    else
        printf '%s/%s\n' "$PROJECT_ROOT" "$value"
    fi
}

BUILD_DIR="$(resolve_project_path "$CONTROL_BUILD_DIR_INPUT")"
LOG_DIR="$(resolve_project_path "$CONTROL_LOG_DIR_INPUT")"
USBIPD_PID_FILE="$LOG_DIR/usbipd-auto-attach.pid"
USBIPD_BUSID_FILE="$LOG_DIR/usbipd-auto-attach.busid"
USBIPD_STDOUT_LOG_FILE="$LOG_DIR/usbipd-auto-attach.stdout.log"
USBIPD_STDERR_LOG_FILE="$LOG_DIR/usbipd-auto-attach.stderr.log"
PORT=""
USBIPD_COMMAND=()
USBIPD_VERSION=""
USBIPD_HELP=""
WATCHER_PID=""
USBIPD_SELECTED_STATE=""
USBIPD_SELECTED_LINE=""

die() {
    printf 'CONTROL_DEV: ERROR: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage:
  ./tools/dev/control.sh build
  ./tools/dev/control.sh flash
  ./tools/dev/control.sh monitor
  ./tools/dev/control.sh diag
  ./tools/dev/control.sh flash-diag
  ./tools/dev/control.sh usb-start
  ./tools/dev/control.sh usb-stop
  ./tools/dev/control.sh usb-status
  ./tools/dev/control.sh status
  ./tools/dev/control.sh clean-build --yes
EOF
}

check_project_root() {
    [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]] || die "project root not found: $PROJECT_ROOT"
    [[ -f "$PROJECT_ROOT/sdkconfig.defaults" ]] || die "missing $PROJECT_ROOT/sdkconfig.defaults"
    [[ -f "$PROJECT_ROOT/sdkconfig.defaults.recovery" ]] || die "missing $PROJECT_ROOT/sdkconfig.defaults.recovery"
    [[ -f "$PROJECT_ROOT/sdkconfig.defaults.matter_stable" ]] || die "missing $PROJECT_ROOT/sdkconfig.defaults.matter_stable"
}

load_environment() {
    [[ -f "$CONTROL_IDF_EXPORT" ]] || die "ESP-IDF export script not found: $CONTROL_IDF_EXPORT"
    [[ -f "$CONTROL_MATTER_EXPORT" ]] || die "ESP-Matter export script not found: $CONTROL_MATTER_EXPORT"

    set +u
    # shellcheck disable=SC1090
    source "$CONTROL_IDF_EXPORT"
    # shellcheck disable=SC1090
    source "$CONTROL_MATTER_EXPORT"
    set -u

    command -v idf.py >/dev/null 2>&1 || die "idf.py is not available after sourcing ESP-IDF"
}

usbipd_exec() {
    if [[ "${USBIPD_COMMAND[0]:-}" == "powershell.exe" ]]; then
        local command_line="usbipd.exe"
        local arg
        for arg in "$@"; do
            command_line+=" $arg"
        done
        powershell.exe -NoProfile -NonInteractive -Command "$command_line"
    else
        "${USBIPD_COMMAND[@]}" "$@"
    fi
}

usbipd_command_init() {
    (( ${#USBIPD_COMMAND[@]} == 0 )) || return 0
    if command -v usbipd.exe >/dev/null 2>&1; then
        if usbipd.exe --version >/dev/null 2>&1; then
            USBIPD_COMMAND=(usbipd.exe)
        elif command -v powershell.exe >/dev/null 2>&1; then
            printf 'CONTROL_DEV: usbipd_transport=powershell_fallback\n' >&2
            USBIPD_COMMAND=(powershell.exe)
        else
            USBIPD_COMMAND=(usbipd.exe)
        fi
    elif command -v usbipd >/dev/null 2>&1; then
        USBIPD_COMMAND=(usbipd)
    elif command -v powershell.exe >/dev/null 2>&1; then
        USBIPD_COMMAND=(powershell.exe)
    else
        die "usbipd.exe or powershell.exe is required for USB auto-attach"
    fi
}

usbipd_tool_init() {
    usbipd_command_init

    local command_status
    set +e
    USBIPD_VERSION="$(usbipd_exec --version 2>&1 | tr -d '\r' | tail -n 1)"
    command_status=$?
    set -e
    (( command_status == 0 && ${#USBIPD_VERSION} > 0 )) || die "unable to run usbipd --version"
    set +e
    USBIPD_HELP="$(usbipd_exec attach --help 2>&1 | tr -d '\r')"
    command_status=$?
    set -e
    (( command_status == 0 )) || die "unable to run usbipd attach --help"
    printf 'CONTROL_DEV: usbipd_version=%s\n' "$USBIPD_VERSION"
}

usbipd_require_auto_attach() {
    [[ -n "$USBIPD_HELP" ]] || usbipd_tool_init
    if ! grep -Eq -- '--auto-attach' <<<"$USBIPD_HELP"; then
        printf 'CONTROL_DEV: ERROR: installed usbipd does not support --auto-attach\n' >&2
        printf 'CONTROL_DEV: ERROR: upgrade usbipd-win, for example:\n' >&2
        printf '  winget upgrade usbipd\n' >&2
        return 1
    fi
}

usbipd_refresh_list() {
    USBIPD_LIST="$(usbipd_exec list 2>&1 | tr -d '\r' || true)"
    grep -Eq '(^|[[:space:]])BUSID[[:space:]]+VID:PID' <<<"$USBIPD_LIST" || die "usbipd list returned invalid output"
}

usbipd_print_list() {
    printf '%s\n' "$USBIPD_LIST"
}

usbipd_state() {
    local line="$1"
    if grep -Eiq 'not[[:space:]]+shared' <<<"$line"; then
        printf 'Not shared\n'
    elif grep -Eiq '[[:space:]]Shared([[:space:]([]|$)' <<<"$line"; then
        printf 'Shared\n'
    elif grep -Eiq 'attached' <<<"$line"; then
        printf 'Attached\n'
    else
        printf 'unknown\n'
    fi
}

usbipd_line_for_busid() {
    local busid="$1"
    awk -v busid="$busid" '$1 == busid { print; exit }' <<<"$USBIPD_LIST"
}

usbipd_description() {
    local line="$1" description
    description="$(awk '{ $1=""; $2=""; sub(/^[[:space:]]+/, ""); print }' <<<"$line")"
    sed -E 's/[[:space:]]+(Not[[:space:]]+shared|Shared|Attached|No such device).*//I' <<<"$description"
}

control_usb_identity_matches() {
    local identity="${1,,}" target="${CONTROL_USBIPD_VID#0x}:${CONTROL_USBIPD_PID#0x}"
    target="${target,,}"
    if [[ -n "$CONTROL_USBIPD_PID" ]]; then
        [[ "$identity" == "$target" ]]
    else
        [[ "$identity" == '303a:1001' || "$identity" == '303a:0009' ]]
    fi
}

control_usb_lines() {
    local preferred fallback
    if [[ -n "$CONTROL_USBIPD_PID" ]]; then
        awk -v target="${CONTROL_USBIPD_VID#0x}:${CONTROL_USBIPD_PID#0x}" 'tolower($2) == tolower(target) { print }' <<<"$USBIPD_LIST"
        return
    fi
    preferred="$(awk 'tolower($2) == "303a:1001" { print }' <<<"$USBIPD_LIST")"
    [[ -n "$preferred" ]] && { printf '%s\n' "$preferred"; return; }
    fallback="$(awk 'tolower($2) == "303a:0009" { print }' <<<"$USBIPD_LIST")"
    [[ -n "$fallback" ]] && printf '%s\n' "$fallback"
}

usbipd_bind_required() {
    local busid="$1"
    printf 'CONTROL_DEV: attach_state_branch=not_shared_error\n' >&2
    printf 'CONTROL_DEV: ERROR: device %s is not shared with usbipd\n' "$busid" >&2
    printf 'CONTROL_DEV: one-time command: usbipd bind --busid %s\n' "$busid" >&2
    return 1
}

select_usbipd_device() {
    local allow_not_shared="${1:-0}"
    local dynamic="${2:-0}"
    local quiet="${3:-0}"
    local required="${4:-1}"
    usbipd_refresh_list
    local line state target
    local -a matches=()

    if [[ "$dynamic" != '1' && -n "$CONTROL_USBIPD_BUSID" ]]; then
        [[ "$CONTROL_USBIPD_BUSID" =~ ^[[:alnum:]._:-]+$ ]] || die "invalid CONTROL_USBIPD_BUSID"
        line="$(usbipd_line_for_busid "$CONTROL_USBIPD_BUSID")"
        [[ -n "$line" ]] || { usbipd_print_list >&2; die "configured usbipd BUSID not found: $CONTROL_USBIPD_BUSID"; }
        state="$(usbipd_state "$line")"
        [[ "$state" != 'Not shared' || "$allow_not_shared" == '1' ]] || usbipd_bind_required "$CONTROL_USBIPD_BUSID"
        [[ "$state" == 'Shared' || "$state" == 'Attached' || ( "$allow_not_shared" == '1' && "$state" == 'Not shared' ) ]] || die "configured usbipd device $CONTROL_USBIPD_BUSID is not shared or attached"
        BUSID="$CONTROL_USBIPD_BUSID"
    elif [[ -n "$CONTROL_USBIPD_VID" || -n "$CONTROL_USBIPD_PID" ]]; then
        [[ -n "$CONTROL_USBIPD_VID" ]] || die "set CONTROL_USBIPD_VID when overriding USB identity"
        target="${CONTROL_USBIPD_VID#0x}:${CONTROL_USBIPD_PID#0x}"
        target="${target,,}"
        mapfile -t matches < <(control_usb_lines)
        if (( ${#matches[@]} == 0 )); then
            mapfile -t matches < <(awk '$1 ~ /^[0-9]+-[0-9]+$/ && tolower($0) ~ /(esp32|espressif|usb[ -]jtag|usb[ -]cdc|jtag\/serial)/ { print }' <<<"$USBIPD_LIST")
        fi
        if (( ${#matches[@]} == 0 )); then
            (( required )) && { usbipd_print_list >&2; die "no usbipd device matches VID:PID $target"; }
            return 1
        fi
        if (( ${#matches[@]} != 1 )); then
            (( required )) && { printf '%s\n' "${matches[@]}" >&2; die "multiple usbipd devices match VID:PID $target"; }
            return 1
        fi
        line="${matches[0]}"
        state="$(usbipd_state "$line")"
        [[ "$state" != 'Not shared' || "$allow_not_shared" == '1' ]] || usbipd_bind_required "$(awk '{print $1}' <<<"$line")"
        [[ "$state" == 'Shared' || "$state" == 'Attached' || ( "$allow_not_shared" == '1' && "$state" == 'Not shared' ) ]] || die "usbipd device is not shared or attached"
        BUSID="$(awk '{print $1}' <<<"$line")"
    else
        mapfile -t matches < <(control_usb_lines)
        if (( ${#matches[@]} == 0 )); then
            mapfile -t matches < <(awk '$1 ~ /^[0-9]+-[0-9]+$/ && tolower($0) ~ /(esp32|espressif|usb[ -]jtag|usb[ -]cdc|jtag\/serial)/ { print }' <<<"$USBIPD_LIST")
        fi
        if (( ${#matches[@]} == 0 )); then
            (( required )) && { usbipd_print_list >&2; die "no ESP32 USB device found; set CONTROL_USBIPD_BUSID or CONTROL_USBIPD_VID/PID"; }
            return 1
        fi
        if (( ${#matches[@]} != 1 )); then
            (( required )) && { printf '%s\n' "${matches[@]}" >&2; die 'multiple ESP32 USB devices found'; }
            return 1
        fi
        line="${matches[0]}"
        BUSID="$(awk '{print $1}' <<<"$line")"
        state="$(usbipd_state "$line")"
        [[ "$state" != 'Not shared' || "$allow_not_shared" == '1' ]] || usbipd_bind_required "$BUSID"
        [[ "$state" == 'Shared' || "$state" == 'Attached' || ( "$allow_not_shared" == '1' && "$state" == 'Not shared' ) ]] || die "ESP32 USB device $BUSID is not in a shared or attached state"
    fi

    if [[ "$quiet" != '1' ]]; then
        printf 'CONTROL_DEV: usbipd_busid=%s\n' "$BUSID"
        printf 'CONTROL_DEV: usbipd_device=%s\n' "$(usbipd_description "$line")"
        printf 'CONTROL_DEV: usbipd_state=%s\n' "$state"
    fi
    USBIPD_SELECTED_STATE="$state"
    USBIPD_SELECTED_LINE="$line"
}

require_powershell() {
    command -v powershell.exe >/dev/null 2>&1 || die "powershell.exe is required for detached usbipd auto-attach"
}

watcher_process_info() {
    local pid="$1"
    require_powershell
    timeout 10s powershell.exe -NoProfile -NonInteractive -Command \
        "\$p = Get-CimInstance Win32_Process -Filter 'ProcessId=$pid'; if (\$p) { Write-Output \$p.Name; Write-Output \$p.CommandLine }" 2>/dev/null | tr -d '\r'
}

watcher_matches() {
    local pid="$1" info
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    info="$(watcher_process_info "$pid" || true)"
    grep -Eiq '^usbipd\.exe[[:space:]]*$' <<<"$info" || return 1
    grep -Eiq '(^|[[:space:]])attach([[:space:]]|$)' <<<"$info" || return 1
    grep -Eiq -- '--auto-attach' <<<"$info" || return 1
    grep -Eiq -- "--busid[[:space:]]+$BUSID([[:space:]]|$)" <<<"$info"
}

find_external_watcher() {
    local result
    if command -v powershell.exe >/dev/null 2>&1; then
        result="$(timeout 10s powershell.exe -NoProfile -NonInteractive -Command \
            "Get-CimInstance Win32_Process | Where-Object { \$_.Name -ieq 'usbipd.exe' -and \$_.CommandLine -match '--auto-attach' -and \$_.CommandLine -match '--busid[[:space:]]+$BUSID([[:space:]]|$)' } | Select-Object -ExpandProperty ProcessId" \
            2>/dev/null | tr -d '\r' | head -n 1 || true)"
    else
        result="$(pgrep -f "usbipd.*--auto-attach.*--busid[[:space:]]+$BUSID" 2>/dev/null | head -n 1 || true)"
    fi
    [[ "$result" =~ ^[0-9]+$ ]] && printf '%s\n' "$result"
}

start_usbipd_watcher() {
    require_powershell
    command -v timeout >/dev/null 2>&1 || die "timeout command is required for the usbipd watcher launcher"
    mkdir -p "$LOG_DIR"
    local pid="" log_win err_win launcher_win raw_output launch_status
    local -a launcher_args=()
    if [[ -f "$USBIPD_PID_FILE" ]]; then
        pid="$(tr -d '[:space:]' < "$USBIPD_PID_FILE")"
        if [[ "$pid" =~ ^[0-9]+$ ]] && watcher_matches "$pid"; then
            printf 'CONTROL_DEV: usbipd_auto_attach=already_running\n'
            printf 'CONTROL_DEV: usbipd_pid=%s\n' "$pid"
            WATCHER_PID="$pid"
            return 0
        fi
        printf 'CONTROL_DEV: usbipd_auto_attach=stale_pid\n' >&2
        rm -f -- "$USBIPD_PID_FILE"
    fi

    pid="$(find_external_watcher || true)"
    if [[ -n "$pid" ]]; then
        printf 'CONTROL_DEV: usbipd_auto_attach=already_running\n'
        printf 'CONTROL_DEV: usbipd_pid=%s\n' "$pid"
        WATCHER_PID="$pid"
        return 0
    fi

    printf 'CONTROL_DEV: watcher_launch_step=before_path_conversion\n'
    log_win="$(wslpath -w "$USBIPD_STDOUT_LOG_FILE" 2>/dev/null | tr -d '\r\n' || true)"
    err_win="$(wslpath -w "$USBIPD_STDERR_LOG_FILE" 2>/dev/null | tr -d '\r\n' || true)"
    launcher_win="$(wslpath -w "$SCRIPT_DIR/start-usbipd-watcher.ps1" 2>/dev/null | tr -d '\r\n' || true)"
    [[ -n "$log_win" && -n "$err_win" && -n "$launcher_win" ]] || die "wslpath is required to start the Windows usbipd watcher"
    printf 'CONTROL_DEV: watcher_launch_step=after_path_conversion\n'
    launcher_args=(-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$launcher_win" -BusId "$BUSID" -StdoutLog "$log_win" -StderrLog "$err_win")
    if grep -Eq -- '--unplugged' <<<"$USBIPD_HELP"; then
        launcher_args+=( -UseUnplugged )
    fi
    printf 'CONTROL_DEV: watcher_launch_method=powershell_process_start\n'
    printf 'CONTROL_DEV: watcher_launch_busid=%s\n' "$BUSID"
    printf 'CONTROL_DEV: watcher_launch_step=before_powershell\n'
    set +e
    raw_output="$(timeout 10s powershell.exe "${launcher_args[@]}" 2>&1)"
    launch_status=$?
    set -e
    printf 'CONTROL_DEV: watcher_launch_step=after_powershell\n'
    printf 'CONTROL_DEV: watcher_launch_step=raw_output:'
    printf '%q' "$raw_output"
    printf '\n'
    if (( launch_status == 124 || launch_status == 137 )); then
        printf 'CONTROL_DEV: ERROR: detached watcher launcher timed out\n' >&2
        return 1
    fi
    printf 'CONTROL_DEV: watcher_launch_step=before_pid_verification\n'
    pid="$(printf '%s\n' "$raw_output" | tr -d '\r' | grep -E '^[[:space:]]*[0-9]+[[:space:]]*$' | tail -n 1 | tr -d '[:space:]' || true)"
    if (( launch_status != 0 )) || [[ ! "$pid" =~ ^[0-9]+$ ]]; then
        printf 'CONTROL_DEV: watcher_launch_step=after_pid_verification pid_valid=false\n'
        printf 'CONTROL_DEV: ERROR: detached usbipd launch failed\n' >&2
        printf 'CONTROL_DEV: powershell_output=%s\n' "$raw_output" >&2
        cat "$USBIPD_STDOUT_LOG_FILE" >&2 2>/dev/null || true
        cat "$USBIPD_STDERR_LOG_FILE" >&2 2>/dev/null || true
        return 1
    fi
    WATCHER_PID="$pid"
    if ! watcher_matches "$pid"; then
        printf 'CONTROL_DEV: watcher_launch_step=after_pid_verification pid_valid=false\n'
        printf 'CONTROL_DEV: ERROR: returned PID is not the expected usbipd watcher\n' >&2
        printf 'CONTROL_DEV: powershell_output=%s\n' "$raw_output" >&2
        return 1
    fi
    printf 'CONTROL_DEV: watcher_launch_step=after_pid_verification pid_valid=true\n'
    printf '%s\n' "$pid" > "$USBIPD_PID_FILE"
    printf '%s\n' "$BUSID" > "$USBIPD_BUSID_FILE"
    printf 'CONTROL_DEV: usbipd_auto_attach=started\n'
    printf 'CONTROL_DEV: usbipd_pid=%s\n' "$pid"
}

linux_serial_ports() {
    { compgen -G '/dev/ttyACM*' || true; compgen -G '/dev/ttyUSB*' || true; } | sort -u
}

sysfs_usb_id() {
    local port_name path vendor product i
    port_name="${1##*/}"
    path="$(readlink -f "/sys/class/tty/$port_name/device" 2>/dev/null || true)"
    for ((i = 0; i < 8 && -n "$path"; i++)); do
        if [[ -f "$path/idVendor" && -f "$path/idProduct" ]]; then
            vendor="$(tr -d '[:space:]' < "$path/idVendor")"
            product="$(tr -d '[:space:]' < "$path/idProduct")"
            printf '%s:%s\n' "${vendor,,}" "${product,,}"
            return 0
        fi
        path="$(dirname "$path")"
    done
    return 1
}

choose_linux_port() {
    local -a ports=() matches=()
    mapfile -t ports < <(linux_serial_ports)
    if [[ -n "${CONTROL_PORT:-}" ]]; then
        [[ -e "$CONTROL_PORT" ]] || return 1
        PORT="$CONTROL_PORT"
    elif (( ${#ports[@]} == 1 )); then
        PORT="${ports[0]}"
    elif (( ${#ports[@]} > 1 )); then
        local port id
        for port in "${ports[@]}"; do
            id="$(sysfs_usb_id "$port" || true)"
            control_usb_identity_matches "$id" && matches+=("$port")
        done
        (( ${#matches[@]} == 1 )) || return 1
        PORT="${matches[0]}"
    else
        return 1
    fi
    [[ "${1:-}" == '--quiet' ]] || printf 'CONTROL_DEV: serial_port=%s\n' "$PORT"
}

wait_for_linux_port() {
    [[ "$CONTROL_USB_WAIT_SECONDS" =~ ^[0-9]+$ && "$CONTROL_USB_WAIT_SECONDS" -gt 0 ]] || die "CONTROL_USB_WAIT_SECONDS must be a positive integer"
    [[ "${1:-}" == '--already-printed' ]] || printf 'CONTROL_DEV: waiting_for_usb...\n'
    local deadline=$((SECONDS + CONTROL_USB_WAIT_SECONDS))
    while (( SECONDS <= deadline )); do
        if choose_linux_port; then
            printf 'CONTROL_DEV: usb_attach=PASS\n'
            return 0
        fi
        if [[ -n "$WATCHER_PID" ]] && ! watcher_matches "$WATCHER_PID"; then
            printf 'CONTROL_DEV: ERROR: usbipd auto-attach exited early\n' >&2
            printf '%s\n' 'CONTROL_DEV: usbipd_stdout_log='"$USBIPD_STDOUT_LOG_FILE" >&2
            cat "$USBIPD_STDOUT_LOG_FILE" >&2 2>/dev/null || true
            printf '%s\n' 'CONTROL_DEV: usbipd_stderr_log='"$USBIPD_STDERR_LOG_FILE" >&2
            cat "$USBIPD_STDERR_LOG_FILE" >&2 2>/dev/null || true
            usbipd_refresh_list || true
            usbipd_print_list >&2 || true
            return 1
        fi
        sleep 0.25
    done

    printf 'CONTROL_DEV: ERROR: USB serial device did not appear\n' >&2
    usbipd_refresh_list || true
    usbipd_print_list >&2 || true
    tail -n 30 "$USBIPD_STDOUT_LOG_FILE" 2>/dev/null || true
    tail -n 30 "$USBIPD_STDERR_LOG_FILE" >&2 2>/dev/null || true
    command -v lsusb >/dev/null 2>&1 && lsusb >&2 || true
    ls -l /dev/ttyACM* /dev/ttyUSB* >&2 2>/dev/null || true
    dmesg | tail -n 30 >&2 2>/dev/null || true
    return 1
}

handle_selected_usb_state() {
    case "$USBIPD_SELECTED_STATE" in
        Attached)
            if choose_linux_port --quiet; then
                printf 'CONTROL_DEV: attach_state_branch=attached_fast_path\n'
                printf 'CONTROL_DEV: serial_port=%s\n' "$PORT"
                printf 'CONTROL_DEV: usb_attach=PASS\n'
                return 0
            fi
            printf 'CONTROL_DEV: attach_state_branch=attached_wait\n'
            printf 'CONTROL_DEV: waiting_for_usb...\n'
            wait_for_linux_port --already-printed
            ;;
        Shared)
            printf 'CONTROL_DEV: attach_state_branch=shared_auto_attach\n'
            start_usbipd_watcher
            wait_for_linux_port
            ;;
        Not\ shared)
            usbipd_bind_required "$BUSID"
            ;;
        *)
            die "unsupported usbipd state: $USBIPD_SELECTED_STATE"
            ;;
    esac
}

ensure_usb_attached() {
    if choose_linux_port; then
        printf 'CONTROL_DEV: usb_attach=PASS\n'
        return 0
    fi
    local -a existing_ports=()
    mapfile -t existing_ports < <(linux_serial_ports)
    if (( ${#existing_ports[@]} > 1 )) && [[ -z "${CONTROL_PORT:-}" ]] && { [[ -z "$CONTROL_USBIPD_VID" ]] || [[ -z "$CONTROL_USBIPD_PID" ]]; }; then
        printf 'CONTROL_DEV: multiple ports detected:\n' >&2
        printf '  %s\n' "${existing_ports[@]}" >&2
        die "set CONTROL_PORT; refusing to guess"
    fi
    [[ "$CONTROL_USBIPD_AUTO_ATTACH" == '1' ]] || die "no Linux serial device found and CONTROL_USBIPD_AUTO_ATTACH is disabled"
    usbipd_command_init
    select_usbipd_device 1
    [[ "$USBIPD_SELECTED_STATE" == 'Shared' ]] && usbipd_require_auto_attach
    handle_selected_usb_state
}

usb_start() {
    [[ "$CONTROL_USBIPD_AUTO_ATTACH" == '1' ]] || die "usb-start requires CONTROL_USBIPD_AUTO_ATTACH=1"
    usbipd_command_init
    select_usbipd_device 1
    [[ "$USBIPD_SELECTED_STATE" == 'Shared' ]] && usbipd_require_auto_attach
    handle_selected_usb_state
}

usb_stop() {
    require_powershell
    usbipd_tool_init
    local busid="${CONTROL_USBIPD_BUSID:-}" pid="" stop_status=0
    [[ -z "$busid" && -f "$USBIPD_BUSID_FILE" ]] && busid="$(tr -d '[:space:]' < "$USBIPD_BUSID_FILE")"
    if [[ -f "$USBIPD_PID_FILE" ]]; then
        pid="$(tr -d '[:space:]' < "$USBIPD_PID_FILE")"
        if [[ "$pid" =~ ^[0-9]+$ ]] && [[ -n "$busid" ]]; then
            BUSID="$busid"
            if watcher_matches "$pid"; then
                powershell.exe -NoProfile -NonInteractive -Command "Stop-Process -Id $pid -Force" 2>/dev/null || stop_status=$?
            fi
        fi
    fi
    if [[ -z "$busid" ]]; then
        select_usbipd_device
        busid="$BUSID"
    fi
    local run_detach=0
    if usbipd_exec detach --busid "$busid" >/dev/null 2>&1; then run_detach=1; fi
    rm -f -- "$USBIPD_PID_FILE" "$USBIPD_BUSID_FILE"
    (( stop_status == 0 )) || die "Stop-Process failed for verified watcher PID $pid"
    (( run_detach == 1 )) || die "usbipd detach failed for BUSID $busid"
    printf 'CONTROL_DEV: usbipd_auto_attach=stopped\n'
    printf 'CONTROL_DEV: usbipd_detach=PASS\n'
}

usb_release_ui() {
    usbipd_command_init
    stop_all_usbipd_watchers
    usbipd_refresh_list
    if ! select_usbipd_device 1 1 1 0; then
        printf 'CONTROL_DEV: usb_release_ui=no_device\n'
        return 0
    fi
    BUSID="$(awk '{print $1}' <<<"$USBIPD_SELECTED_LINE")"
    if [[ "$USBIPD_SELECTED_STATE" == 'Attached' ]]; then
        detach_usbipd_busid
    fi
    wait_for_windows_port
}

usb_status() {
    usbipd_require_auto_attach
    usbipd_refresh_list
    printf 'CONTROL_DEV: usbipd_status_begin\n'
    usbipd_print_list
    local line="" state="" status_busid="" status_pid="" watcher_state="not_running"
    [[ -n "$CONTROL_USBIPD_BUSID" ]] && status_busid="$CONTROL_USBIPD_BUSID"
    [[ -z "$status_busid" && -f "$USBIPD_BUSID_FILE" ]] && status_busid="$(tr -d '[:space:]' < "$USBIPD_BUSID_FILE")"
    if [[ -n "$status_busid" ]]; then
        line="$(usbipd_line_for_busid "$status_busid")"
    else
        local -a status_matches=()
        mapfile -t status_matches < <(awk 'tolower($0) ~ /(esp32|espressif|usb[ -]jtag|usb[ -]cdc|jtag\/serial)/ { print }' <<<"$USBIPD_LIST")
        if (( ${#status_matches[@]} == 1 )); then
            line="${status_matches[0]}"
            status_busid="$(awk '{print $1}' <<<"$line")"
        elif (( ${#status_matches[@]} > 1 )); then
            status_busid=ambiguous
        else
            status_busid=none
        fi
    fi
    printf 'CONTROL_DEV: usbipd_busid=%s\n' "$status_busid"
    if [[ -n "$line" ]]; then
        state="$(usbipd_state "$line")"
        printf 'CONTROL_DEV: usbipd_device=%s\n' "$(usbipd_description "$line")"
        printf 'CONTROL_DEV: usbipd_state=%s\n' "$state"
        printf 'CONTROL_DEV: usb_vidpid=%s\n' "$(awk '{print tolower($2)}' <<<"$line")"
    else
        printf 'CONTROL_DEV: usbipd_device=unknown\n'
        printf 'CONTROL_DEV: usbipd_state=unknown\n'
    fi
    if [[ -f "$USBIPD_PID_FILE" ]]; then
        status_pid="$(tr -d '[:space:]' < "$USBIPD_PID_FILE")"
        if [[ "$status_pid" =~ ^[0-9]+$ ]]; then
            BUSID="$status_busid"
            if watcher_matches "$status_pid"; then
                watcher_state=running
            else
                watcher_state=stale_pid
            fi
        else
            watcher_state=stale_pid
        fi
    fi
    printf 'CONTROL_DEV: watcher=%s\n' "$watcher_state"
    printf 'CONTROL_DEV: watcher_pid=%s\n' "$status_pid"
    printf 'CONTROL_DEV: linux_serial_devices=\n'
    linux_serial_ports || true
}

detect_port() {
    choose_linux_port || die "no unambiguous Linux serial port found; set CONTROL_PORT"
    printf 'CONTROL_DEV: port=%s\n' "$PORT"
}

check_port_free() {
    local owners=""
    if command -v fuser >/dev/null 2>&1; then
        owners="$(fuser "$PORT" 2>/dev/null || true)"
        if [[ -n "${owners//[[:space:]]/}" ]]; then
            printf 'CONTROL_DEV: port=%s is busy\n' "$PORT" >&2
            fuser -v "$PORT" 2>&1 >&2 || true
            die "close the owning process before continuing"
        fi
    elif command -v lsof >/dev/null 2>&1; then
        owners="$(lsof -t -- "$PORT" 2>/dev/null || true)"
        if [[ -n "${owners//[[:space:]]/}" ]]; then
            printf 'CONTROL_DEV: port=%s is busy\n' "$PORT" >&2
            lsof -- "$PORT" >&2 || true
            die "close the owning process before continuing"
        fi
    else
        printf 'CONTROL_DEV: warning=fuser_and_lsof_unavailable port_check_skipped\n' >&2
    fi
}

verify_generated_config() {
    local config="$BUILD_DIR/config/sdkconfig.h"
    [[ -f "$config" ]] || die "generated configuration not found: $config"

    local required=(
        'CONFIG_CONTROL_WEB_SERIAL_CONFIGURATOR 1'
        'CONFIG_CONTROL_UI_DASHBOARD_V1 1'
        'CONFIG_CONTROL_UI_MATTER_COMMANDS 1'
        'CONFIG_CONTROL_UI_DEVICE_ONBOARDING 1'
        'CONFIG_USE_MINIMAL_MDNS 1'
        'CONFIG_CHIP_TASK_STACK_SIZE 24576'
        'CONFIG_ESP_MAIN_TASK_STACK_SIZE 24576'
        'CONFIG_PARTITION_TABLE_FILENAME "partitions_matter.csv"'
    )
    local item name value
    for item in "${required[@]}"; do
        name="${item%% *}"
        value="${item#* }"
        grep -Fqx "#define $name $value" "$config" || die "generated configuration mismatch: $name expected $value"
    done
    if grep -Eq '^#define CONFIG_CONTROL_UI_MOCK_DEVICES([[:space:]]|$)' "$config"; then
        die "generated configuration mismatch: CONFIG_CONTROL_UI_MOCK_DEVICES must be disabled"
    fi
    printf 'CONTROL_DEV: generated_config=PASS\n'
}

cache_value() {
    local cache="$1" name="$2"
    sed -n "s#^${name}:[^=]*=##p" "$cache" | head -n 1
}

build_cache_path_mismatch() {
    local cache cached_home cached_source cached_cache_dir cached_binary_dir line lower
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || return 1

    cached_home="$(cache_value "$BUILD_DIR/CMakeCache.txt" CMAKE_HOME_DIRECTORY)"
    cached_source="$(cache_value "$BUILD_DIR/CMakeCache.txt" control_SOURCE_DIR)"
    cached_cache_dir="$(cache_value "$BUILD_DIR/CMakeCache.txt" CMAKE_CACHEFILE_DIR)"
    cached_binary_dir="$(cache_value "$BUILD_DIR/CMakeCache.txt" control_BINARY_DIR)"
    [[ -z "$cached_home" || "$cached_home" == "$PROJECT_ROOT" ]] || return 0
    [[ -z "$cached_source" || "$cached_source" == "$PROJECT_ROOT" ]] || return 0
    [[ -z "$cached_cache_dir" || "$cached_cache_dir" == "$BUILD_DIR" ]] || return 0
    [[ -z "$cached_binary_dir" || "$cached_binary_dir" == "$BUILD_DIR" ]] || return 0

    while IFS= read -r -d '' cache; do
        while IFS= read -r line; do
            lower="${line,,}"
            if [[ "$lower" == *"${PROJECT_ROOT,,}"* && "$line" != *"$PROJECT_ROOT"* ]]; then
                return 0
            fi
            if [[ "$lower" == *'\\wsl.localhost'* || "$lower" =~ [a-z]:\\ && "$lower" == *'/control'* ]]; then
                return 0
            fi
        done < "$cache"
    done < <(find "$BUILD_DIR" -type f -name CMakeCache.txt -print0 2>/dev/null)
    return 1
}

ensure_build_cache() {
    local cache="$BUILD_DIR/CMakeCache.txt" cached_project
    [[ -f "$cache" ]] || return 0
    if ! build_cache_path_mismatch; then
        printf 'CONTROL_DEV: build_cache_path_mismatch=not_detected\n'
        printf 'CONTROL_DEV: build_cache_path=accepted\n'
        return 0
    fi

    cached_project="$(cache_value "$cache" CMAKE_HOME_DIRECTORY)"
    [[ -n "$cached_project" ]] || cached_project="$(cache_value "$cache" control_SOURCE_DIR)"
    [[ -n "$cached_project" ]] || cached_project='unknown'
    printf 'CONTROL_DEV: build_cache_path_mismatch=detected\n'
    printf 'CONTROL_DEV: cached_project_path=%s\n' "$cached_project"
    printf 'CONTROL_DEV: current_project_path=%s\n' "$PROJECT_ROOT"
    [[ "$BUILD_DIR" == "$PROJECT_ROOT"/* && "$BUILD_DIR" != "$PROJECT_ROOT" ]] ||
        die "refusing to regenerate an unsafe build directory: $BUILD_DIR"
    printf 'CONTROL_DEV: build_cache_recovery=regenerating\n'
    rm -rf -- "$BUILD_DIR"
    printf 'CONTROL_DEV: build_cache_recovery=PASS\n'
}

build_project() {
    ensure_build_cache
    printf 'CONTROL_DEV: build_dir=%s\n' "$BUILD_DIR"
    (cd "$PROJECT_ROOT" && idf.py -B "$BUILD_DIR" "-DSDKCONFIG=$SDKCONFIG_PATH" "-DSDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" build)
    verify_generated_config
}

binary_path() {
    printf '%s/control.bin\n' "$BUILD_DIR"
}

print_binary_info() {
    local binary
    binary="$(binary_path)"
    [[ -f "$binary" ]] || die "firmware binary not found: $binary"
    printf 'CONTROL_DEV: binary=%s\n' "$binary"
    printf 'CONTROL_DEV: binary_timestamp=%s\n' "$(stat -c '%y' "$binary")"
    printf 'CONTROL_DEV: binary_size=%s bytes\n' "$(stat -c '%s' "$binary")"
}

ensure_usbipd_watcher_running() {
    [[ "$USBIPD_SELECTED_STATE" == 'Attached' || "$USBIPD_SELECTED_STATE" == 'Shared' ]] || die "watcher requires a shared or attached usbipd device"
    usbipd_require_auto_attach
    start_usbipd_watcher
    if [[ -z "$WATCHER_PID" ]] || ! watcher_matches "$WATCHER_PID"; then
        die "usbipd auto-attach watcher could not be verified"
    fi
    printf 'CONTROL_DEV: reset_resilience=PASS\n'
}

windows_com_from_selected_line() {
    grep -Eo 'COM[0-9]+' <<<"$USBIPD_SELECTED_LINE" | tail -n 1
}

windows_serial_ports() {
    timeout 10s powershell.exe -NoProfile -NonInteractive -Command \
        '[System.IO.Ports.SerialPort]::GetPortNames()' 2>/dev/null | tr -d '\r'
}

windows_identity_port() {
    timeout 10s powershell.exe -NoProfile -NonInteractive -Command \
        "Get-CimInstance Win32_PnPEntity | Where-Object { \$_.PNPDeviceID -match 'VID_303A&PID_(0009|1001)' -or \$_.Name -match 'ESP32|USB JTAG|JTAG/serial' } | ForEach-Object { if (\$_.Name -match '\\((COM[0-9]+)\\)') { \$matches[1] } }" \
        2>/dev/null | tr -d '\r' | head -n 1
}

wait_for_windows_port() {
    [[ "$CONTROL_USB_WAIT_SECONDS" =~ ^[0-9]+$ && "$CONTROL_USB_WAIT_SECONDS" -gt 0 ]] || die "CONTROL_USB_WAIT_SECONDS must be a positive integer"
    local deadline=$((SECONDS + CONTROL_USB_WAIT_SECONDS)) line port ports identity
    printf 'CONTROL_DEV: waiting_for_windows_serial...\n'
    while (( SECONDS <= deadline )); do
        if usbipd_refresh_list 2>/dev/null && select_usbipd_device 1 1 1 0; then
            [[ "$USBIPD_SELECTED_STATE" == 'Shared' ]] || { sleep 0.25; continue; }
            line="$USBIPD_SELECTED_LINE"
            port="$(windows_com_from_selected_line || true)"
            ports="$(windows_serial_ports || true)"
            identity="$(windows_identity_port || true)"
            if [[ ! "$port" =~ ^COM[0-9]+$ || ! "$(grep -Fx "$port" <<<"$ports" || true)" ]]; then port="$identity"; fi
            if [[ "$port" =~ ^COM[0-9]+$ ]] && grep -Fxq "$port" <<<"$ports" && windows_serial_access "$port"; then
                printf 'CONTROL_DEV: usb_owner=windows\n'
                printf 'CONTROL_DEV: usb_vidpid=%s\n' "$(awk '{print tolower($2)}' <<<"$line")"
                printf 'CONTROL_DEV: windows_port=%s\n' "$port"
                printf 'CONTROL_DEV: usb_release_ui=PASS\n'
                return 0
            fi
        fi
        sleep 0.25
    done
    die 'Windows did not expose the Control-board COM port after USB release'
}

wait_for_windows_bootloader() {
    local listed_port identity_port ports
    printf 'CONTROL_DEV: ACTION_REQUIRED: Put the ESP32-S3 into bootloader mode now.\n'
    printf 'CONTROL_DEV: Waiting for ESP32-S3 bootloader...\n'
    while :; do
        if usbipd_refresh_list 2>/dev/null && select_usbipd_device 1 1 1 0; then
            listed_port="$(windows_com_from_selected_line || true)"
            ports="$(windows_serial_ports || true)"
            identity_port="$(windows_identity_port || true)"
            if [[ "$listed_port" =~ ^COM[0-9]+$ ]] && grep -Fxq "$listed_port" <<<"$ports"; then
                WINDOWS_PORT="$listed_port"
            elif [[ "$identity_port" =~ ^COM[0-9]+$ ]] && grep -Fxq "$identity_port" <<<"$ports"; then
                WINDOWS_PORT="$identity_port"
            else
                WINDOWS_PORT=''
            fi
            if [[ "$WINDOWS_PORT" =~ ^COM[0-9]+$ ]]; then
                printf 'CONTROL_DEV: bootloader_detected=PASS\n'
                printf 'CONTROL_DEV: usbipd_busid=%s\n' "$BUSID"
                printf 'CONTROL_DEV: windows_port=%s\n' "$WINDOWS_PORT"
                return 0
            fi
        fi
        sleep 0.25
    done
}

stop_all_usbipd_watchers() {
    require_powershell
    local pids pid recorded_pid recorded_busid stop_status=0
    pids="$(timeout 10s powershell.exe -NoProfile -NonInteractive -Command \
        "Get-CimInstance Win32_Process | Where-Object { \$_.Name -ieq 'usbipd.exe' -and \$_.CommandLine -match '--auto-attach' } | Select-Object -ExpandProperty ProcessId" \
        2>/dev/null | tr -d '\r' || true)"
    if [[ -f "$USBIPD_PID_FILE" && -f "$USBIPD_BUSID_FILE" ]]; then
        recorded_pid="$(tr -d '[:space:]' < "$USBIPD_PID_FILE")"
        recorded_busid="$(tr -d '[:space:]' < "$USBIPD_BUSID_FILE")"
        if [[ "$recorded_pid" =~ ^[0-9]+$ && "$recorded_busid" =~ ^[[:alnum:]._:-]+$ ]]; then
            BUSID="$recorded_busid"
            if watcher_matches "$recorded_pid" && ! grep -Fxq "$recorded_pid" <<<"$pids"; then
                pids="${pids:+$pids$'\n'}$recorded_pid"
            fi
        fi
    fi
    while IFS= read -r pid; do
        [[ "$pid" =~ ^[0-9]+$ ]] || continue
        powershell.exe -NoProfile -NonInteractive -Command "Stop-Process -Id $pid -Force" >/dev/null 2>&1 || stop_status=$?
        (( stop_status == 0 )) || die "Stop-Process failed for usbipd watcher PID $pid"
        printf 'CONTROL_DEV: usbipd_auto_attach=stopped\n'
    done <<<"$pids"
    rm -f -- "$USBIPD_PID_FILE" "$USBIPD_BUSID_FILE"
}

detach_usbipd_busid() {
    usbipd_exec detach --busid "$BUSID" >/dev/null 2>&1 || die "usbipd detach failed for BUSID $BUSID"
    printf 'CONTROL_DEV: usbipd_detach=PASS\n'
}

windows_serial_access() {
    local port="$1"
    powershell.exe -NoProfile -NonInteractive -Command \
        "\$s = New-Object System.IO.Ports.SerialPort '$port',115200,None,8,one; try { \$s.Open(); exit 0 } catch { exit 1 } finally { if (\$s.IsOpen) { \$s.Close() }; \$s.Dispose() }" \
        >/dev/null 2>&1
}

report_windows_flash_failure() {
    printf 'CONTROL_DEV: windows_flash=FAIL\n' >&2
    usbipd_refresh_list || true
    usbipd_print_list >&2 || true
    printf 'CONTROL_DEV: dmesg_usb_lines\n' >&2
    dmesg 2>/dev/null | grep -Ei 'usb|ttyACM|ttyUSB|cdc|serial' | tail -n 30 >&2 || true
}

prepare_windows_esptool() {
    local status
    local -a args=(-VersionOnly -Bootloader "$2" -PartitionTable "$3" -Application "$4")
    [[ -n "$5" ]] && args+=(-ControlWindowsEsptool "$5")
    printf 'CONTROL_DEV: windows_esptool=detecting\n'
    set +e
    powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$1" "${args[@]}"
    status=$?
    set -e
    (( status == 0 )) || die 'Windows esptool was not found or failed version validation'
}

flash_windows_native() {
    local bootloader="$BUILD_DIR/bootloader/bootloader.bin"
    local partition="$BUILD_DIR/partition_table/partition-table.bin"
    local application="$(binary_path)" launcher_win
    local bootloader_win partition_win application_win windows_esptool_override flash_status
    local attempt=1 attempt_log
    [[ -f "$bootloader" && -f "$partition" && -f "$application" ]] || die "required flash binaries are missing"
    [[ "$CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS" =~ ^[0-9]+$ && "$CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS" -gt 0 ]] || die "CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS must be a positive integer"

    usbipd_command_init
    command -v wslpath >/dev/null 2>&1 || die 'wslpath is required for Windows flashing'
    bootloader_win="$(wslpath -w "$bootloader" | tr -d '\r\n')"
    partition_win="$(wslpath -w "$partition" | tr -d '\r\n')"
    application_win="$(wslpath -w "$application" | tr -d '\r\n')"
    launcher_win="$(wslpath -w "$SCRIPT_DIR/windows-esptool-flash.ps1" | tr -d '\r\n')"

    printf 'CONTROL_DEV: flash_transport=windows_com\n'
    printf 'CONTROL_DEV: target=windows\n'
    printf 'CONTROL_DEV: windows_bootloader=%s\n' "$bootloader_win"
    printf 'CONTROL_DEV: windows_partition_table=%s\n' "$partition_win"
    printf 'CONTROL_DEV: windows_application=%s\n' "$application_win"

    windows_esptool_override="${CONTROL_WINDOWS_ESPTOOL:-}"
    prepare_windows_esptool "$launcher_win" "$bootloader_win" "$partition_win" "$application_win" "$windows_esptool_override"
    usbipd_tool_init
    stop_all_usbipd_watchers
    select_usbipd_device 1 1
    if [[ "$USBIPD_SELECTED_STATE" == 'Attached' ]]; then
        detach_usbipd_busid
    fi

    while (( attempt <= 3 )); do
        wait_for_windows_bootloader
        attempt_log="$(mktemp)"
        set +e
        powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$launcher_win" \
            -Port "$WINDOWS_PORT" -Bootloader "$bootloader_win" -PartitionTable "$partition_win" \
            -Application "$application_win" -ControlWindowsEsptool "$windows_esptool_override" \
            -TimeoutSeconds "$CONTROL_WINDOWS_FLASH_TIMEOUT_SECONDS" 2>&1 | tee "$attempt_log"
        flash_status="${PIPESTATUS[0]}"
        set -e
        if (( flash_status == 0 )); then
            rm -f -- "$attempt_log"
            break
        fi
        if (( attempt < 3 )) && grep -Fq 'Failed to connect to ESP32-S3: No serial data received' "$attempt_log"; then
            rm -f -- "$attempt_log"
            attempt=$((attempt + 1))
            continue
        fi
        rm -f -- "$attempt_log"
        report_windows_flash_failure
        return "$flash_status"
    done

    printf 'CONTROL_DEV: windows_flash=PASS\n'
    printf 'CONTROL_DEV: windows_flash_hash=PASS\n'
}

flash_project() {
    local binary
    binary="$(binary_path)"
    ensure_build_cache
    if [[ ! -d "$BUILD_DIR" || ! -f "$binary" ]]; then
        printf 'CONTROL_DEV: build_missing=1; building\n'
        build_project
    else
        verify_generated_config
    fi
    print_binary_info
    [[ "$CONTROL_FLASH_TRANSPORT" == 'windows_com' ]] || die 'firmware flashing target is windows; WSL flashing is disabled'
    flash_windows_native
}

monitor_project() {
    printf 'CONTROL_DEV: target=wsl\n'
    ensure_usb_attached
    detect_port
    check_port_free
    (cd "$PROJECT_ROOT" && idf.py -B "$BUILD_DIR" "-DSDKCONFIG=$SDKCONFIG_PATH" "-DSDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" -p "$PORT" monitor)
}

latest_log() {
    [[ -d "$LOG_DIR" ]] || return 0
    find "$LOG_DIR" -maxdepth 1 -type f -name 'control-*.log' -printf '%T@ %p\n' 2>/dev/null |
        sort -n | tail -n 1 | cut -d' ' -f2-
}

strip_ansi() {
    local input="$1" output="$2"
    sed -E $'s/\x1B\[[0-9;?]*[ -\/]*[@-~]//g' "$input" > "$output"
}

report_line() {
    local line="$1"
    printf '%s\n' "$line"
    printf '%s\n' "$line" >> "$REPORT_LOG"
}

report_check() {
    local label="$1" pattern="$2"
    if grep -Eiq -- "$pattern" "$REPORT_LOG"; then
        report_line "PASS: $label"
        return 0
    fi
    report_line "FAIL: $label"
    REPORT_FAILED=1
}

report_absent() {
    local label="$1" pattern="$2"
    if grep -Eiq -- "$pattern" "$REPORT_LOG"; then
        report_line "FAIL: $label"
        REPORT_FAILED=1
        return 1
    fi
    report_line "PASS: $label"
}

report_reboot_loop() {
    local boot_count
    boot_count="$(grep -Ec 'boot_phase=nvs' "$REPORT_LOG" || true)"
    if [[ "$boot_count" -gt 1 ]]; then
        report_line 'FAIL: absence of reboot loop'
        REPORT_FAILED=1
    else
        report_line 'PASS: absence of reboot loop'
    fi
}

append_report() {
    local log="$1"
    REPORT_LOG="$log"
    REPORT_FAILED=0

    printf '\n'
    report_line 'CONTROL_DEV: diagnostic_report'
    report_check 'boot detected' 'boot_phase=nvs|Control .*build .*ESP-IDF'
    report_check 'current firmware build timestamp detected' 'Control .*build .*ESP-IDF'
    report_check 'Wi-Fi got IP' 'got_ip|headless_connect_result=connected'
    report_check 'Matter controller ready' 'controller_ready=yes|ControllerReady'
    report_check 'fabric valid' 'saved_state=valid|fabric_storage_check result=valid'
    report_check 'display initialized' 'écran=OK|display.*(initialized|ready|ok)'
    report_check 'touch initialized' 'tactile=OK|touch.*(initialized|ready|ok)'
    report_check 'CONTROL_CONFIG serial_init_start' 'CONTROL_CONFIG: serial_init_start'
    report_check 'CONTROL_CONFIG queue initialized' 'CONTROL_CONFIG: init_step=queue result=ok'
    report_check 'CONTROL_CONFIG result queue initialized' 'CONTROL_CONFIG: init_step=result_queue result=ok'
    report_check 'CONTROL_CONFIG parser buffers initialized' 'CONTROL_CONFIG: init_step=parser_buffers result=ok'
    report_check 'CONTROL_CONFIG transport initialized' 'CONTROL_CONFIG: init_step=transport result=ok'
    report_check 'CONTROL_CONFIG task initialized' 'CONTROL_CONFIG: init_step=task result=ok'
    report_check 'CONTROL_CONFIG serial_ready' 'CONTROL_CONFIG: serial_ready'
    report_absent 'absence of serial_init_failed' 'CONTROL_CONFIG: serial_init_failed'
    report_absent 'absence of panic' 'panic|Guru Meditation'
    report_absent 'absence of abort' 'abort'
    report_reboot_loop

    local firmware_line ip_line node_line heap_line
    firmware_line="$(grep -E 'Control .*build .*ESP-IDF' "$REPORT_LOG" | head -n 1 || true)"
    ip_line="$(grep -E 'got_ip|headless_connect_result=connected' "$REPORT_LOG" | tail -n 1 || true)"
    node_line="$(grep -E 'remote_node_id=|remote_node_id:' "$REPORT_LOG" | tail -n 1 || true)"
    heap_line="$(grep -E 'after_(touch|ui)_init|after_ui_init' "$REPORT_LOG" | grep -E 'internal=|largest_internal=' | tail -n 1 || true)"
    report_line "firmware_build: ${firmware_line:-not found}"
    report_line "ip: ${ip_line:-not found}"
    report_line "remote_node_id: ${node_line:-not found}"
    report_line "ui_heap: ${heap_line:-not found}"

    report_line 'errors_and_warnings:'
    local errors_file tail_file
    errors_file="$REPORT_LOG.errors.$$"
    if grep -E '(^|[[:space:]])[EW] \(|ERROR|WARN|error|warning' "$REPORT_LOG" > "$errors_file" 2>/dev/null; then
        cat "$errors_file" >> "$REPORT_LOG"
    else
        report_line '(none)'
    fi
    rm -f -- "$errors_file"
    report_line 'last_30_log_lines:'
    tail_file="$REPORT_LOG.tail.$$"
    tail -n 30 "$REPORT_LOG" > "$tail_file"
    cat "$tail_file" >> "$REPORT_LOG"
    rm -f -- "$tail_file"

    if (( REPORT_FAILED == 0 )); then
        report_line 'CONTROL_DEV: verdict=PASS'
    else
        report_line 'CONTROL_DEV: verdict=FAIL'
    fi
    printf 'CONTROL_DEV: log=%s\n' "$REPORT_LOG"
    printf 'CONTROL_DEV: verdict=%s\n' "$([[ $REPORT_FAILED -eq 0 ]] && printf PASS || printf FAIL)"
    return "$REPORT_FAILED"
}

capture_diagnostics() {
    [[ "$CONTROL_DIAG_SECONDS" =~ ^[0-9]+$ && "$CONTROL_DIAG_SECONDS" -gt 0 ]] || die "CONTROL_DIAG_SECONDS must be a positive integer"
    command -v timeout >/dev/null 2>&1 || die "timeout command is required for diag"
    command -v tee >/dev/null 2>&1 || die "tee command is required for diag"
    mkdir -p "$LOG_DIR"
    printf 'CONTROL_DEV: target=wsl\n'
    ensure_usb_attached
    detect_port
    check_port_free

    local stamp raw_log clean_log monitor_status
    stamp="$(date '+%Y%m%d-%H%M%S')"
    raw_log="$LOG_DIR/.control-$stamp.raw.log"
    clean_log="$LOG_DIR/control-$stamp.log"
    printf 'CONTROL_DEV: capture_seconds=%s\n' "$CONTROL_DIAG_SECONDS"
    printf 'CONTROL_DEV: raw_log=%s\n' "$raw_log"

    set +e
    (cd "$PROJECT_ROOT" && timeout --signal=INT --kill-after=5s "$CONTROL_DIAG_SECONDS" \
        idf.py -B "$BUILD_DIR" "-DSDKCONFIG=$SDKCONFIG_PATH" "-DSDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" -p "$PORT" monitor) 2>&1 |
        tee "$raw_log"
    monitor_status="${PIPESTATUS[0]}"
    set -e

    [[ -f "$raw_log" ]] || die "monitor produced no capture: $raw_log"
    strip_ansi "$raw_log" "$clean_log"
    rm -f -- "$raw_log"
    printf 'CONTROL_DEV: monitor_exit=%s\n' "$monitor_status"
    append_report "$clean_log"
}

status_project() {
    printf 'CONTROL_DEV: branch=%s\n' "$(git -C "$PROJECT_ROOT" branch --show-current)"
    printf 'CONTROL_DEV: project_root=%s\n' "$PROJECT_ROOT"
    printf 'CONTROL_DEV: build_dir=%s\n' "$BUILD_DIR"
    printf 'CONTROL_DEV: sdkconfig=%s\n' "$SDKCONFIG_PATH"
    if [[ -n "${CONTROL_PORT:-}" ]]; then
        printf 'CONTROL_DEV: port=%s\n' "$CONTROL_PORT"
    else
        local status_ports=()
        mapfile -t status_ports < <(linux_serial_ports)
        if (( ${#status_ports[@]} == 1 )); then
            printf 'CONTROL_DEV: port=%s\n' "${status_ports[0]}"
        elif (( ${#status_ports[@]} == 0 )); then
            printf 'CONTROL_DEV: port=none\n'
        else
            printf 'CONTROL_DEV: port=ambiguous (%s)\n' "${status_ports[*]}"
        fi
    fi
    local binary log verdict
    binary="$(binary_path)"
    printf 'CONTROL_DEV: binary=%s\n' "$binary"
    if [[ -f "$binary" ]]; then
        printf 'CONTROL_DEV: binary_timestamp=%s\n' "$(stat -c '%y' "$binary")"
        printf 'CONTROL_DEV: binary_size=%s bytes\n' "$(stat -c '%s' "$binary")"
    else
        printf 'CONTROL_DEV: binary=missing\n'
    fi
    log="$(latest_log || true)"
    printf 'CONTROL_DEV: latest_log=%s\n' "${log:-none}"
    if [[ -n "$log" ]]; then
        verdict="$(grep -E 'CONTROL_DEV: verdict=' "$log" | tail -n 1 || true)"
        printf 'CONTROL_DEV: latest_verdict=%s\n' "${verdict:-unknown}"
    else
        printf 'CONTROL_DEV: latest_verdict=unknown\n'
    fi
    git -C "$PROJECT_ROOT" status --short
}

clean_build() {
    [[ "${1:-}" == '--yes' && "$#" -eq 1 ]] || die 'clean-build requires exactly: clean-build --yes'
    [[ "$BUILD_DIR" == "$PROJECT_ROOT/build_matter_stable" ]] || die 'clean-build refuses a non-default build directory'
    [[ "$SDKCONFIG_PATH" == '/tmp/control_sdkconfig_matter_stable' ]] || die 'clean-build refuses a non-default sdkconfig path'
    rm -rf -- "$BUILD_DIR" "$SDKCONFIG_PATH"
    printf 'CONTROL_DEV: cleaned=%s %s\n' "$BUILD_DIR" "$SDKCONFIG_PATH"
    build_project
}

check_project_root
COMMAND="${1:-help}"
shift || true

    case "$COMMAND" in
    build)
        [[ "$#" -eq 0 ]] || die 'build takes no arguments'
        load_environment
        ensure_build_cache
        (cd "$PROJECT_ROOT" && idf.py -B "$BUILD_DIR" "-DSDKCONFIG=$SDKCONFIG_PATH" "-DSDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" reconfigure)
        verify_generated_config
        (cd "$PROJECT_ROOT" && idf.py -B "$BUILD_DIR" "-DSDKCONFIG=$SDKCONFIG_PATH" "-DSDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" build)
        verify_generated_config
        ;;
    flash)
        [[ "$#" -eq 0 ]] || die 'flash takes no arguments'
        load_environment
        flash_project
        ;;
    monitor)
        [[ "$#" -eq 0 ]] || die 'monitor takes no arguments'
        load_environment
        monitor_project
        ;;
    diag)
        [[ "$#" -eq 0 ]] || die 'diag takes no arguments'
        load_environment
        capture_diagnostics
        ;;
    flash-diag)
        [[ "$#" -eq 0 ]] || die 'flash-diag takes no arguments'
        load_environment
        build_project
        flash_project
        sleep 2
        capture_diagnostics
        ;;
    usb-start)
        [[ "$#" -eq 0 ]] || die 'usb-start takes no arguments'
        usb_start
        ;;
    usb-stop)
        [[ "$#" -eq 0 ]] || die 'usb-stop takes no arguments'
        usb_stop
        ;;
    usb-release-ui)
        [[ "$#" -eq 0 ]] || die 'usb-release-ui takes no arguments'
        usb_release_ui
        ;;
    usb-status)
        [[ "$#" -eq 0 ]] || die 'usb-status takes no arguments'
        usb_status
        ;;
    status)
        [[ "$#" -eq 0 ]] || die 'status takes no arguments'
        status_project
        ;;
    clean-build)
        load_environment
        clean_build "$@"
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        usage >&2
        die "unknown command: $COMMAND"
        ;;
esac
