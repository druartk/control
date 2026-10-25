#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
TMP="$(mktemp -d)"
TEST_ROOT="$ROOT/.test-build-cache-$BASHPID"
mkdir -p "$TEST_ROOT"
trap 'rm -rf -- "$TMP" "$TEST_ROOT"' EXIT
MOCK_BIN="$TMP/bin"
mkdir -p "$MOCK_BIN"

cat > "$TMP/idf-export.sh" <<'EOF'
# test export
EOF
cat > "$TMP/matter-export.sh" <<'EOF'
# test export
EOF
cat > "$MOCK_BIN/idf.py" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
build=''
while (($#)); do
    if [[ "$1" == '-B' ]]; then
        build="$2"
        shift 2
    else
        shift
    fi
done
[[ -n "$build" ]]
mkdir -p "$build/config"
printf '%s\n' \
    '#define CONFIG_CONTROL_WEB_SERIAL_CONFIGURATOR 1' \
    '#define CONFIG_CONTROL_UI_DASHBOARD_V1 1' \
    '#define CONFIG_CONTROL_UI_MATTER_COMMANDS 1' \
    '#define CONFIG_CONTROL_UI_DEVICE_ONBOARDING 1' \
    '#define CONFIG_USE_MINIMAL_MDNS 1' \
    '#define CONFIG_CHIP_TASK_STACK_SIZE 24576' \
    '#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 24576' \
    '#define CONFIG_PARTITION_TABLE_FILENAME "partitions_matter.csv"' > "$build/config/sdkconfig.h"
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\ncontrol_SOURCE_DIR:STATIC=%s\nCMAKE_CACHEFILE_DIR:INTERNAL=%s\ncontrol_BINARY_DIR:STATIC=%s\n' \
    "$FAKE_ROOT" "$FAKE_ROOT" "$build" "$build" > "$build/CMakeCache.txt"
EOF
chmod +x "$MOCK_BIN/idf.py"

write_cache() {
    local build="$1" home="$2"
    mkdir -p "$build"
    printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\ncontrol_SOURCE_DIR:STATIC=%s\nCMAKE_CACHEFILE_DIR:INTERNAL=%s\ncontrol_BINARY_DIR:STATIC=%s\n' \
        "$home" "$home" "$build" "$build" > "$build/CMakeCache.txt"
}

run_build() {
    local build="$1"
    PATH="$MOCK_BIN:$PATH" FAKE_ROOT="$ROOT" \
        CONTROL_IDF_EXPORT="$TMP/idf-export.sh" CONTROL_MATTER_EXPORT="$TMP/matter-export.sh" \
        CONTROL_BUILD_DIR="$build" "$ROOT/tools/dev/control.sh" build
}

canonical_build="$TEST_ROOT/case-a"
write_cache "$canonical_build" "$ROOT"
output="$(run_build "$canonical_build")"
grep -Fq 'CONTROL_DEV: build_cache_path=accepted' <<<"$output"
printf '%s\n' 'CASE_PATH_A: PASS'

case_build="$TEST_ROOT/case-b"
write_cache "$case_build" "${ROOT/K/k}"
output="$(run_build "$case_build")"
grep -Fq 'CONTROL_DEV: build_cache_path_mismatch=detected' <<<"$output"
grep -Fq "CONTROL_DEV: cached_project_path=${ROOT/K/k}" <<<"$output"
grep -Fq "CONTROL_DEV: current_project_path=$ROOT" <<<"$output"
grep -Fq 'CONTROL_DEV: build_cache_recovery=PASS' <<<"$output"
printf '%s\n' 'CASE_PATH_B: PASS'

old_wsl_build="$TEST_ROOT/case-c"
write_cache "$old_wsl_build" '\\\\wsl.localhost\\Ubuntu\\mnt\\c\\Users\\K\\Documents\\Arduino\\control'
output="$(run_build "$old_wsl_build")"
grep -Fq 'CONTROL_DEV: build_cache_path_mismatch=detected' <<<"$output"
grep -Fq 'CONTROL_DEV: build_cache_recovery=PASS' <<<"$output"
printf '%s\n' 'CASE_PATH_C: PASS'

windows_root="$(wslpath -w "$ROOT" | tr -d '\r\n')"
[[ "$windows_root" == 'C:\Users\K\Documents\Arduino\control' ]]
printf '%s\n' 'CASE_PATH_D: PASS'
[[ "$windows_root" != *'\\wsl.localhost'* ]]
printf '%s\n' 'CASE_PATH_E: PASS'

output="$(cd /tmp && CONTROL_CLI_DRY_RUN=1 "$ROOT/control" build 2>&1)"
grep -Fq "CONTROL_CLI: dispatch=$ROOT/tools/dev/control.sh build" <<<"$output"
printf '%s\n' 'CASE_PATH_F: PASS'
