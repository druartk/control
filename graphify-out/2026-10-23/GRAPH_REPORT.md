# Graph Report - control  (2026-10-23)

## Corpus Check
- 154 files · ~950,989 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1735 nodes · 3429 edges · 131 communities (122 shown, 9 thin omitted)
- Extraction: 91% EXTRACTED · 9% INFERRED · 0% AMBIGUOUS · INFERRED: 292 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `e1550970`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- control_config_serial.cpp
- app.js
- ControlApplication
- control.sh
- UiViewModel
- WifiManager
- MatterCommissioningResult
- ui_config.cpp
- ControlUi
- DisplayManager
- Control-to-ESP-Matter Controller API Mapping
- commissioning_failure_callback
- TouchManager
- PendingRequest
- Matter Configurator Page
- queueRoom
- ed047tc1.c
- IkeaDiscovery
- matter_controller.cpp
- .handleAction
- ReadWaitContext
- ui_manager.cpp
- queueDevice
- test-usbipd-state.sh
- inflate.c
- CONTROL_MAIN_EXPERIMENTAL_MATTER
- control_config_web.cpp
- SettingsStore
- touch_manager.cpp
- control_ui.cpp
- handleRequest
- ikea_discovery.cpp
- control
- TouchDiagnostics
- font.c
- MatterPostCommissioningResult
- .initialize
- Graphify Knowledge-Graph Pipeline
- MatterDeviceItem
- IkeaDiscoveryResult
- activate
- Matter SendNOC / AddNOC Status 9 Recovery
- WifiConnectionInfo
- MatterDiscoveredDevice
- tools/dev/control.sh
- MatterUiResult
- formatMatterId
- matter_pairing_code.cpp
- MatterUiCommand
- TouchEvent
- windows-esptool-flash.ps1
- MatterDiscoveryResult
- MatterSavedNode
- UiAction
- epd_driver.c
- cJSON
- i2s_data_bus.c
- ConfigRequest
- esp_err_t
- test-control-cli.sh
- commandSuccessCallback
- DeviceCacheRecord
- rebuildFromConfig
- RawSavedNodeRead
- DevPaaBypassVerifier
- main.cpp
- readDoneCallback
- ConfigResponse
- DeviceCacheHeader
- IkeaDiscoveryEvent
- RendezvousParameters::SetPeerAddress
- .renderUi
- control.local.env
- request_id Protocol Matching
- CONTROL_BUILD_DATE
- CONTROL_FW_VERSION
- Rect_t
- processControlUiMatterResults
- test-build-cache.sh
- MatterPairingInput
- windows-configurator-acceptance.ps1
- ProtocolClient
- updateControlUiContext
- MatterOnboarding
- PingProbeContext
- ui_config.h
- esp_ping_handle_t
- ServiceQuery
- .processMatter
- RoomGroup
- .continueAfterMatterCodeConfirm
- appStateName
- matter_controller_load_device_cache
- test-wifi-config-live.py

## God Nodes (most connected - your core abstractions)
1. `ControlApplication` - 127 edges
2. `ControlUi` - 93 edges
3. `UiViewModel` - 86 edges
4. `TouchManager` - 45 edges
5. `handleRequest()` - 35 edges
6. `WifiManager` - 35 edges
7. `DisplayManager` - 33 edges
8. `MatterDeviceItem` - 33 edges
9. `die()` - 29 edges
10. `UiManager` - 26 edges

## Surprising Connections (you probably didn't know these)
- `LILYGO T5 4.7 Plus V2.4 Target Hardware` --semantically_similar_to--> `Complete External ESP-Matter v1.5 Checkout`  [INFERRED] [semantically similar]
  README.md → docs/matter_controller_integration_plan.md
- `Event-Driven Matter Runtime Safety` --semantically_similar_to--> `PRESS-Only UI Dispatch`  [INFERRED] [semantically similar]
  docs/matter_controller_integration_plan.md → README.md
- `ESP-Matter Controller Helper` --semantically_similar_to--> `esp_matter_controller Dependency`  [INFERRED] [semantically similar]
  docs/matter_sendnoc_failure.md → main/CMakeLists.txt
- `DNS-SD Commissionable Discovery` --semantically_similar_to--> `mdns Dependency`  [INFERRED] [semantically similar]
  docs/matter_sendnoc_failure.md → main/CMakeLists.txt
- `control_ui NVS Namespace` --semantically_similar_to--> `ESP-IDF Component Registration`  [INFERRED] [semantically similar]
  tools/configurator/README.md → main/CMakeLists.txt

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Graphify Structural and Semantic Extraction Pipeline** — codex_skills_graphify_skill_graphify_operation, codex_skills_graphify_skill_ast_extraction, codex_skills_graphify_skill_semantic_extraction, graphify_extraction_spec_contract [EXTRACTED 1.00]
- **Matter Integration Readiness Chain** — docs_matter_controller_integration_plan_proof_build, docs_matter_controller_integration_plan_partition_audit, docs_matter_attestation_trust_store, docs_matter_persistence_boot_order_heap_gates [INFERRED 0.85]
- **Control Hardware and Runtime Stack** — readme_lilygo_target_hardware, readme_gt911_touch_pipeline, readme_vendored_epaper_driver, readme_wifi_nvs_ikea_flow [EXTRACTED 1.00]
- **Matter Pairing Recovery Flow** — docs_matter_sendnoc_failure_sendnoc, docs_matter_sendnoc_failure_fabric_conflict, docs_matter_sendnoc_failure_clean_matter_local_reset, docs_matter_sendnoc_failure_pairing_code [EXTRACTED 1.00]
- **Configurator Serial Configuration Flow** — tools_configurator_readme_configurator_usb_web_serial, tools_configurator_index_configurator_matter, tools_configurator_readme_control_ui_nvs, tools_configurator_readme_matter_action_queue [INFERRED 0.85]
- **Development USB Flash and Diagnostics Flow** — tools_dev_readme_control_sh, tools_dev_readme_flash_diag, tools_dev_readme_usbipd, tools_dev_readme_windows_esptool, tools_dev_readme_chrome_web_serial [EXTRACTED 1.00]

## Communities (131 total, 9 thin omitted)

### Community 0 - "control_config_serial.cpp"
Cohesion: 0.17
Nodes (19): configuredTransport(), configureJsonAllocator(), consumeInput(), control_config_serial_init(), esp_err_t, drainResponses(), handleResult(), initFailure() (+11 more)

### Community 1 - "app.js"
Cohesion: 0.07
Nodes (80): clearBulkRooms(), command(), confirmDiscard(), connect(), currentConfig(), deleteConfig(), deleteRoom(), deviceCard() (+72 more)

### Community 2 - "ControlApplication"
Cohesion: 0.03
Nodes (61): ControlApplication, autoConnectAtUs_, batchCommandActive_, batchCommandEndpoints_, batchCommandOn_, batchCommandPosition_, batteryPercent_, connectionStartedUs_ (+53 more)

### Community 3 - "control.sh"
Cohesion: 0.08
Nodes (57): append_report(), build_cache_path_mismatch(), build_project(), canonical_project_path(), capture_diagnostics(), check_port_free(), check_project_root(), choose_linux_port() (+49 more)

### Community 4 - "UiViewModel"
Cohesion: 0.03
Nodes (60): AppState, MatterSavedState, string, vector, WeatherCurrent, WeatherForecastDay, UiViewModel, connection (+52 more)

### Community 5 - "WifiManager"
Cohesion: 0.05
Nodes (60): esp_event_base_t, esp_event_handler_instance_t, esp_netif_t, control_time_has_valid_utc(), control_time_is_synced(), control_time_start_sntp_once(), control_time_wait_synced(), esp_err_t (+52 more)

### Community 6 - "MatterCommissioningResult"
Cohesion: 0.13
Nodes (15): MatterCommissioningStatus, MatterCommissioningResult, attestation_bypassed, attestation_verified, dev_insecure_mode, endpointCount, error, errorText (+7 more)

### Community 7 - "ui_config.cpp"
Cohesion: 0.09
Nodes (42): clearNamespace(), CollectionMeta, active_config_id, config_count, crc32, magic, schema_version, control_ui_config_clear() (+34 more)

### Community 8 - "ControlUi"
Cohesion: 0.04
Nodes (45): ControlUi, acceptTouchAfterUs_, activeBatchExpected_, activeBatchFailed_, activeBatchId_, activeBatchRoom_, activeBatchSuccess_, batteryPercent_ (+37 more)

### Community 9 - "DisplayManager"
Cohesion: 0.10
Nodes (30): esp_err_t, string, DisplayManager, beginFrame, button, commitRegion, fillRectangle, firstRender_ (+22 more)

### Community 10 - "Control-to-ESP-Matter Controller API Mapping"
Cohesion: 0.08
Nodes (33): control_config_enabled CMake Function, ESP_MATTER_PATH External Checkout, Experimental Matter Configuration, control_board ESP-IDF Component, lilygo_epd47 ESP-IDF Component, LILYGO EPD47 Upstream Driver Snapshot, External ESP-Matter Patch Reproducibility Policy, PeerAddress Namespace Qualification Patch (+25 more)

### Community 11 - "commissioning_failure_callback"
Cohesion: 0.31
Nodes (9): CHIP_ERROR, CommissioningStage, commandErrorCallback(), commissioning_failure_callback(), isSendNocFabricConflict(), logChipStepResult(), pase_callback(), optional (+1 more)

### Community 12 - "TouchManager"
Cohesion: 0.07
Nodes (27): i2c_master_bus_handle_t, i2c_master_dev_handle_t, TouchManager, bus_, calibrationMode_, calibrationPoint_, contactActive_, controllerDetected_ (+19 more)

### Community 13 - "PendingRequest"
Cohesion: 0.11
Nodes (23): MatterUiAction, enqueueMatter(), newPending(), pendingFor(), PendingRequest, action, brightness, command_id (+15 more)

### Community 14 - "Matter Configurator Page"
Cohesion: 0.07
Nodes (27): ESP-IDF Component Registration, CONTROL_MAIN_REQUIRES, idf_component_register, app.js Module, Matter Configurator Page, Connect Button, Export JSON Button, Import JSON Button (+19 more)

### Community 15 - "queueRoom"
Cohesion: 0.33
Nodes (6): activeCount, chooseRoom, queueRoom, setContext, copyText(), UiRoom

### Community 16 - "ed047tc1.c"
Cohesion: 0.14
Nodes (28): busy_delay(), esp_err_t, gpio_num_t, epd_base_init(), epd_end_frame(), epd_get_current_buffer(), epd_poweroff(), epd_poweroff_all() (+20 more)

### Community 17 - "IkeaDiscovery"
Cohesion: 0.13
Nodes (15): atomic, TaskHandle_t, IkeaDiscovery, cancel, cancelRequested_, complete_, initializeMdns, mdnsInitialized_ (+7 more)

### Community 18 - "matter_controller.cpp"
Cohesion: 0.11
Nodes (39): ChipDeviceEvent, esp_partition_type_t, boolFromU8(), commissioningFlowName(), configureAttestationVerifier(), controllerLargestInternalHeapReady(), CommissioningFlow, esp_ip4_addr_t (+31 more)

### Community 20 - "ReadWaitContext"
Cohesion: 0.08
Nodes (25): CommandWaitContext, chip_error, done, endpoint_id, got_result, SemaphoreHandle_t, ReadWaitContext, attribute_id (+17 more)

### Community 21 - "ui_manager.cpp"
Cohesion: 0.24
Nodes (24): string, keyboardModeName(), UiManager, formattedMatterCode, maskedPassword, refreshKeyboard, refreshMatterCodeField, refreshPasswordField (+16 more)

### Community 22 - "queueDevice"
Cohesion: 0.50
Nodes (5): completeMatterCommand, markDeviceDirty, queueAllOff, queueDevice, UiDevice

### Community 23 - "test-usbipd-state.sh"
Cohesion: 0.09
Nodes (24): assert_contains(), assert_forwarded_override(), assert_no_attach_command(), assert_no_real_attach(), CONTROL_BUILD_DIR, CONTROL_IDF_EXPORT, CONTROL_LOG_DIR, CONTROL_MATTER_EXPORT (+16 more)

### Community 24 - "inflate.c"
Cohesion: 0.06
Nodes (40): adler32(), adler32_combine_(), adler32_combine64(), adler32_z(), local, uLong, local, uLong (+32 more)

### Community 25 - "CONTROL_MAIN_EXPERIMENTAL_MATTER"
Cohesion: 0.11
Nodes (20): DiscoveryType::kDiscoveryNetworkOnly, DNS-SD Commissionable Discovery, ESP-Matter Controller Helper, PairDevice, pairing_code, chip Dependency, CONTROL_MAIN_EXPERIMENTAL_MATTER, CONTROL_MAIN_RECOVERY (+12 more)

### Community 26 - "control_config_web.cpp"
Cohesion: 0.18
Nodes (15): httpd_req_t, Asset, end, start, type, uri, authorized(), control_config_web_access_code() (+7 more)

### Community 27 - "SettingsStore"
Cohesion: 0.15
Nodes (16): nvs_handle_t, string, getString(), string, SettingsStore, eraseWifi, initialize, load (+8 more)

### Community 28 - "touch_manager.cpp"
Cohesion: 0.17
Nodes (19): array, esp_err_t, TouchEventType, TouchInternalState, irqModeName(), diagnostics, initialize, irqHandler (+11 more)

### Community 29 - "control_ui.cpp"
Cohesion: 0.18
Nodes (22): candidateAt, candidateCount, lockRender, render, renderAssignRoom, renderBattery, renderClearConfirm, renderDeviceScan (+14 more)

### Community 30 - "handleRequest"
Cohesion: 0.27
Nodes (15): activeConfig(), configById(), configJson(), UiConfigRoom, UiNamedConfig, ensureConfigs(), findDevice(), formatNode() (+7 more)

### Community 31 - "ikea_discovery.cpp"
Cohesion: 0.25
Nodes (17): classificationName(), containsIkeaIdentity(), esp_ip4_addr_t, IkeaDiscoveryClassification, string, firstIpv4(), browseServices, classifyCandidate (+9 more)

### Community 32 - "control"
Cohesion: 0.17
Nodes (19): control script, check_project_files(), clean_command(), configure_environment(), detect_usb_busid(), die(), flash_command(), is_usbipd_list() (+11 more)

### Community 33 - "TouchDiagnostics"
Cohesion: 0.11
Nodes (18): string, TouchInternalState, TouchDiagnostics, address, controllerDetected, coordinatesReadable, eventsWorking, internalState (+10 more)

### Community 34 - "font.c"
Cohesion: 0.22
Nodes (14): DrawMode_t, draw_char(), font_properties_default(), get_char_bounds(), get_glyph(), get_text_bounds(), next_cp(), utf8_len() (+6 more)

### Community 35 - "MatterPostCommissioningResult"
Cohesion: 0.18
Nodes (11): MatterPostCommissioningStatus, string, MatterPostCommissioningResult, activeEndpoint, commandState, commandStateKnown, devices, error (+3 more)

### Community 36 - ".initialize"
Cohesion: 0.21
Nodes (16): esp_err_t, esp_err_t, WeatherCurrent, WeatherForecastDay, makeMockSnapshot(), nowSeconds(), parseProviderJson(), refreshTask() (+8 more)

### Community 37 - "Graphify Knowledge-Graph Pipeline"
Cohesion: 0.17
Nodes (16): Project Graphify Integration Rules, AST Structural Extraction, Existing-Graph Fast Path, Graphify Knowledge-Graph Pipeline, Graphify Query, Path, and Explain, Semantic Document Extraction, Graphify Folder Watcher, Graphify URL Ingest (+8 more)

### Community 38 - "MatterDeviceItem"
Cohesion: 0.08
Nodes (38): boundedString(), copyBounded(), vector, deviceToRecord(), fallbackDeviceName(), fallbackTypeLabel(), isControlLight(), isGeneratedDeviceName() (+30 more)

### Community 39 - "IkeaDiscoveryResult"
Cohesion: 0.17
Nodes (12): IkeaDiscoveryClassification, string, IkeaDiscoveryResult, classification, hostname, instanceName, ip, port (+4 more)

### Community 40 - "activate"
Cohesion: 0.22
Nodes (14): activate, beginOnboarding, handleTouch, markScreen, roomName, saveOnboarding, screenName, showHome (+6 more)

### Community 41 - "Matter SendNOC / AddNOC Status 9 Recovery"
Cohesion: 0.15
Nodes (13): attestation_failed, chip-factory, Clean Matter Local Reset, DIRIGERA, idf.py erase-flash, Fabric Conflict, IKEA Home Smart, Initial Commissioning (+5 more)

### Community 42 - "WifiConnectionInfo"
Cohesion: 0.15
Nodes (13): start, WifiConnectionInfo, bssid, dns, gateway, hasIpv4, hasIpv6LinkLocal, ipv4 (+5 more)

### Community 43 - "MatterDiscoveredDevice"
Cohesion: 0.14
Nodes (14): MatterDiscoveredDevice, display_name, endpoint_id, has_level_control, has_onoff, is_aggregator, is_light_candidate, name_source (+6 more)

### Community 44 - "tools/dev/control.sh"
Cohesion: 0.15
Nodes (13): clean-build, build_matter_stable and Temporary SDK Config, tools/dev/control.sh, Developer Automation Toolkit, Diagnostic Capture, flash, flash-diag, Interactive Monitor (+5 more)

### Community 45 - "MatterUiResult"
Cohesion: 0.20
Nodes (10): esp_err_t, MatterUiResult, chip_error, command, error, level, level_known, state (+2 more)

### Community 46 - "formatMatterId"
Cohesion: 0.31
Nodes (13): commissioning_success_callback(), executeUiCommand(), formatMatterId(), isValidRemoteNodeId(), matter_controller_read_level(), matter_controller_read_onoff(), matterCommandWorker(), readMatterAttribute() (+5 more)

### Community 47 - "matter_pairing_code.cpp"
Cohesion: 0.27
Nodes (10): commissioningFlowName(), CommissioningFlow, esp_err_t, SetupPayload, string, discoveryCapabilitiesText(), isMatterCodeFormatSupported(), logSetupPayloadDetails() (+2 more)

### Community 48 - "MatterUiCommand"
Cohesion: 0.17
Nodes (12): CommandWorkerArgs, discovery, from_ui, ui_command, MatterUiAction, MatterUiCommand, action, batch_id (+4 more)

### Community 49 - "TouchEvent"
Cohesion: 0.17
Nodes (11): TouchEventType, TouchEvent, count, irqLevel, rawX, rawY, status, timestampUs (+3 more)

### Community 50 - "windows-esptool-flash.ps1"
Cohesion: 0.27
Nodes (13): Assert-Test(), Escape-Diagnostic(), Invoke-EspTool(), Invoke-VersionCheck(), Join-WindowsArguments(), New-CommandModel(), Parse-CommandLine(), Quote-WindowsArgument() (+5 more)

### Community 51 - "MatterDiscoveryResult"
Cohesion: 0.18
Nodes (11): MatterDiscoveryPhase, MatterDiscoveryResult, devices, duration_ms, endpoint_count, error, light_count, node_id (+3 more)

### Community 52 - "MatterSavedNode"
Cohesion: 0.15
Nodes (13): MatterSavedNode, attestation_bypassed, attestation_verified, commissioned, commissioned_at_unix_or_boot_counter, commissioning_mode, controller_node_id, dev_insecure_mode (+5 more)

### Community 53 - "UiAction"
Cohesion: 0.27
Nodes (10): action(), actionName(), UiActionType, UiActionType, UiAction, character, index, type (+2 more)

### Community 54 - "epd_driver.c"
Cohesion: 0.15
Nodes (24): bit_shift_buffer_right(), DrawMode_t, calc_epd_input_1bpp(), calc_epd_input_4bpp(), epd_draw_circle(), epd_draw_frame_1bit(), epd_draw_hline(), epd_draw_line() (+16 more)

### Community 57 - "cJSON"
Cohesion: 0.25
Nodes (11): cJSON, control_config_serial_web_request(), MatterDiscoveryPhase, discoveryPhaseName(), field(), getRequestId(), parseConfig(), parseNode() (+3 more)

### Community 58 - "i2s_data_bus.c"
Cohesion: 0.15
Nodes (15): epd_output_row(), esp_err_t, dma_desc_addr(), fill_dma_desc(), gpio_setup_out(), i2s_bus_init(), i2s_get_current_buffer(), i2s_is_busy() (+7 more)

### Community 59 - "ConfigRequest"
Cohesion: 0.33
Nodes (6): ConfigRequest, cmd, json, length, request_id, web_request

### Community 60 - "esp_err_t"
Cohesion: 0.17
Nodes (23): clearNvsNamespace(), controllerStateName(), esp_err_t, MatterSavedState, ensureCommandWorker(), matter_controller_clear_device_cache(), matter_controller_forget_saved_node(), matter_controller_get_discovery_result() (+15 more)

### Community 63 - "commandSuccessCallback"
Cohesion: 0.33
Nodes (6): ConcreteCommandPath, ConcreteDataAttributePath, commandSuccessCallback(), readAttributeCallback(), StatusIB, TLVReader

### Community 64 - "DeviceCacheRecord"
Cohesion: 0.25
Nodes (8): DeviceCacheRecord, device_type, display_name, endpoint_id, flags, last_seen, primary_cluster_class, type_label

### Community 65 - "rebuildFromConfig"
Cohesion: 0.25
Nodes (10): bindMatterDevices, bindPersistedDevices, init, rebuildFromConfig, resetRealModel, UiConfig, vector, control_ui_mock_reset() (+2 more)

### Community 66 - "RawSavedNodeRead"
Cohesion: 0.25
Nodes (8): RawSavedNodeRead, any_key_present, commissioned_key_present, error, fabric_index_key_present, namespace_exists, node, node_id_key_present

### Community 67 - "DevPaaBypassVerifier"
Cohesion: 0.13
Nodes (15): AttestationInfo, AttestationVerificationResult, ByteSpan, Callback, Credentials::DeviceAttestationVerifier, DeviceAttestationRevocationDelegate, DeviceAttestationVerifier, DeviceInfoForAttestation (+7 more)

### Community 68 - "main.cpp"
Cohesion: 0.16
Nodes (4): string, wifi_auth_mode_t, wifiAuthName(), vector

### Community 69 - "readDoneCallback"
Cohesion: 0.50
Nodes (4): AttributePathParams, EventPathParams, readDoneCallback(), ScopedMemoryBufferWithSize

### Community 70 - "ConfigResponse"
Cohesion: 0.33
Nodes (6): ConfigResponse, json, length, request_id, type, web_request

### Community 71 - "DeviceCacheHeader"
Cohesion: 0.50
Nodes (4): DeviceCacheHeader, count, magic, version

### Community 72 - "IkeaDiscoveryEvent"
Cohesion: 0.40
Nodes (5): IkeaDiscoveryEventType, poll, IkeaDiscoveryEvent, result, type

### Community 73 - "RendezvousParameters::SetPeerAddress"
Cohesion: 0.67
Nodes (3): CHIP Controller API, Direct-IP Commissioning Unavailable, RendezvousParameters::SetPeerAddress

### Community 74 - ".renderUi"
Cohesion: 0.13
Nodes (7): app_main(), IkeaDiscoveryClassification, ikeaClassificationName(), keyboardModeName(), touchEventName(), weather_poll_refresh_complete(), weather_refresh_in_progress()

### Community 88 - "Rect_t"
Cohesion: 0.29
Nodes (11): esp_err_t, epd_clear(), epd_clear_area(), epd_clear_area_cycles(), epd_copy_to_framebuffer(), epd_draw_grayscale_image(), epd_draw_image(), epd_full_screen() (+3 more)

### Community 91 - "processControlUiMatterResults"
Cohesion: 0.40
Nodes (5): control_config_serial_handle_matter_result(), control_config_serial_take_active_config_id(), control_config_serial_take_config_update(), processControlUiMatterResults(), matter_controller_ui_poll_result()

### Community 97 - "MatterPairingInput"
Cohesion: 0.33
Nodes (5): string, MatterPairingInput, format_valid, normalized_digits, raw_input

### Community 98 - "windows-configurator-acceptance.ps1"
Cohesion: 0.70
Nodes (4): Get-AllDevices(), Read-Lines(), Request(), Wait-Scan()

### Community 110 - "updateControlUiContext"
Cohesion: 0.50
Nodes (4): updateControlUiContext(), control_battery_percent(), esp_err_t, initialize()

### Community 111 - "MatterOnboarding"
Cohesion: 0.33
Nodes (8): string, MatterOnboarding, commissionWithManualCode, commissionWithQrPayload, initializeControllerPlaceholder, notImplementedMessage, requestEnvironmentSearch, requestInstructions

### Community 113 - "PingProbeContext"
Cohesion: 0.50
Nodes (4): SemaphoreHandle_t, PingProbeContext, done, reachable

### Community 116 - "ui_config.h"
Cohesion: 0.40
Nodes (4): bindings(), UiConfig, UiConfigBinding, ui()

### Community 122 - "esp_ping_handle_t"
Cohesion: 0.67
Nodes (3): esp_ping_handle_t, onPingEnd(), onPingSuccess()

### Community 123 - "ServiceQuery"
Cohesion: 0.67
Nodes (3): ServiceQuery, proto, type

### Community 125 - ".processMatter"
Cohesion: 0.24
Nodes (6): bindControlUiMatter(), TaskHandle_t, formatMatterNodeId(), MatterPostCommissioningStatus, matter_controller_log_ui_endpoints(), matter_post_commissioning_status_name()

### Community 126 - "RoomGroup"
Cohesion: 0.22
Nodes (7): string, RoomGroup, controllable_count, endpoint_count, endpoint_ids, lights_on_count, name

### Community 127 - ".continueAfterMatterCodeConfirm"
Cohesion: 0.36
Nodes (5): startMatterCommissioningWorker(), matter_controller_get_last_result(), matter_controller_get_preferred_remote_node_id(), matter_controller_is_ready(), control_heap_log()

### Community 128 - "appStateName"
Cohesion: 0.33
Nodes (3): appStateName(), AppState, AppState

### Community 129 - "matter_controller_load_device_cache"
Cohesion: 0.50
Nodes (5): dumpLocalStorageSummaryLocked(), ensureSavedNodeLoaded(), matter_controller_dump_local_storage_summary(), matter_controller_get_post_commissioning_result(), matter_controller_load_device_cache()

## Knowledge Gaps
- **527 isolated node(s):** `web_request`, `length`, `request_id`, `cmd`, `json` (+522 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **9 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ControlApplication` connect `ControlApplication` to `appStateName`, `WifiManager`, `MatterCommissioningResult`, `ControlUi`, `DisplayManager`, `TouchManager`, `IkeaDiscovery`, `.handleAction`, `ui_manager.cpp`, `SettingsStore`, `MatterPostCommissioningResult`, `.initialize`, `IkeaDiscoveryResult`, `matter_pairing_code.cpp`, `MatterSavedNode`, `esp_err_t`, `main.cpp`, `.renderUi`, `MatterPairingInput`, `MatterOnboarding`, `.processMatter`, `RoomGroup`, `.continueAfterMatterCodeConfirm`?**
  _High betweenness centrality (0.199) - this node is a cross-community bridge._
- **Why does `ControlUi` connect `ControlUi` to `rebuildFromConfig`, `ControlApplication`, `main.cpp`, `activate`, `DisplayManager`, `queueRoom`, `MatterDiscoveryResult`, `queueDevice`, `control_ui.cpp`?**
  _High betweenness centrality (0.078) - this node is a cross-community bridge._
- **Why does `UiViewModel` connect `UiViewModel` to `MatterPostCommissioningResult`, `main.cpp`, `WifiManager`, `IkeaDiscoveryResult`, `.renderUi`, `WifiConnectionInfo`, `MatterSavedNode`, `ui_manager.cpp`, `UiAction`, `RoomGroup`?**
  _High betweenness centrality (0.068) - this node is a cross-community bridge._
- **What connects `web_request`, `length`, `request_id` to the rest of the system?**
  _527 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `app.js` be split into smaller, more focused modules?**
  _Cohesion score 0.07137954701441318 - nodes in this community are weakly interconnected._
- **Should `ControlApplication` be split into smaller, more focused modules?**
  _Cohesion score 0.031746031746031744 - nodes in this community are weakly interconnected._
- **Should `control.sh` be split into smaller, more focused modules?**
  _Cohesion score 0.08364389233954451 - nodes in this community are weakly interconnected._