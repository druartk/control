# Graph Report - control  (2026-08-26)

## Corpus Check
- 103 files · ~244,200 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1593 nodes · 3114 edges · 95 communities (87 shown, 8 thin omitted)
- Extraction: 91% EXTRACTED · 9% INFERRED · 0% AMBIGUOUS · INFERRED: 270 edges (avg confidence: 0.8)
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
- esp_err_t
- TouchManager
- MatterDeviceItem
- Matter Configurator Page
- MatterPostCommissioningResult
- .renderUi
- DevPaaBypassVerifier
- matter_controller.cpp
- epd_driver.c
- ReadWaitContext
- ui_manager.cpp
- crc32.c
- test-usbipd-state.sh
- inflate.c
- CONTROL_MAIN_EXPERIMENTAL_MATTER
- control_time_wait_synced
- main.cpp
- touch_manager.cpp
- control_ui.cpp
- string
- ikea_discovery.cpp
- control
- TouchDiagnostics
- StoredSettings
- IkeaDiscovery
- weather_manager.cpp
- Graphify Knowledge-Graph Pipeline
- Rect_t
- .handleAction
- activate
- Matter SendNOC / AddNOC Status 9 Recovery
- WifiConnectionInfo
- MatterDiscoveredDevice
- tools/dev/control.sh
- MatterUiResult
- IkeaDiscoveryResult
- matter_pairing_code.cpp
- MatterUiCommand
- TouchEvent
- windows-esptool-flash.ps1
- MatterDiscoveryResult
- MatterSavedNode
- UiAction
- ed047tc1.c
- RoomGroup
- .processMatter
- font.c
- matter_saved_state_load_and_validate
- test-control-cli.sh
- commandSuccessCallback
- SettingsStore
- init
- RawSavedNodeRead
- rebuildFromConfig
- readDoneCallback
- PingProbeContext
- DeviceCacheHeader
- i2s_data_bus.c
- RendezvousParameters::SetPeerAddress
- IkeaDiscoveryEvent
- queueDevice
- control.local.env
- request_id Protocol Matching
- CONTROL_BUILD_DATE
- CONTROL_FW_VERSION
- MatterOnboarding
- esp_ping_handle_t
- ServiceQuery
- UiRect
- MatterPairingInput
- test-build-cache.sh
- ui_config.h
- touchStateName
- windows-configurator-acceptance.ps1

## God Nodes (most connected - your core abstractions)
1. `ControlApplication` - 124 edges
2. `ControlUi` - 86 edges
3. `UiViewModel` - 86 edges
4. `TouchManager` - 45 edges
5. `WifiManager` - 35 edges
6. `handleRequest()` - 31 edges
7. `DisplayManager` - 30 edges
8. `MatterDeviceItem` - 29 edges
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

## Communities (95 total, 8 thin omitted)

### Community 0 - "control_config_serial.cpp"
Cohesion: 0.06
Nodes (75): cJSON, activeConfig(), configById(), configJson(), ConfigRequest, cmd, json, length (+67 more)

### Community 1 - "app.js"
Cohesion: 0.08
Nodes (48): client, command(), confirmDiscard(), connect(), currentConfig(), deleteConfig(), deviceCard(), deviceNode() (+40 more)

### Community 2 - "ControlApplication"
Cohesion: 0.03
Nodes (58): ControlApplication, autoConnectAtUs_, batchCommandActive_, batchCommandEndpoints_, batchCommandOn_, batchCommandPosition_, connectionStartedUs_, controlUi_ (+50 more)

### Community 3 - "control.sh"
Cohesion: 0.08
Nodes (57): append_report(), build_cache_path_mismatch(), build_project(), canonical_project_path(), capture_diagnostics(), check_port_free(), check_project_root(), choose_linux_port() (+49 more)

### Community 4 - "UiViewModel"
Cohesion: 0.03
Nodes (60): AppState, MatterSavedState, string, vector, WeatherCurrent, WeatherForecastDay, UiViewModel, connection (+52 more)

### Community 5 - "WifiManager"
Cohesion: 0.06
Nodes (51): esp_event_base_t, esp_event_handler_instance_t, esp_netif_t, authName(), esp_err_t, esp_ip4_addr_t, esp_ip6_addr_t, string (+43 more)

### Community 6 - "MatterCommissioningResult"
Cohesion: 0.13
Nodes (15): MatterCommissioningStatus, MatterCommissioningResult, attestation_bypassed, attestation_verified, dev_insecure_mode, endpointCount, error, errorText (+7 more)

### Community 7 - "ui_config.cpp"
Cohesion: 0.09
Nodes (42): clearNamespace(), CollectionMeta, active_config_id, config_count, crc32, magic, schema_version, control_ui_config_clear() (+34 more)

### Community 8 - "ControlUi"
Cohesion: 0.05
Nodes (42): ControlUi, activeBatchExpected_, activeBatchFailed_, activeBatchId_, activeBatchRoom_, activeBatchSuccess_, clockText_, config_ (+34 more)

### Community 9 - "DisplayManager"
Cohesion: 0.11
Nodes (20): esp_err_t, epd_init(), esp_err_t, string, DisplayManager, beginFrame, commitRegion, firstRender_ (+12 more)

### Community 10 - "Control-to-ESP-Matter Controller API Mapping"
Cohesion: 0.08
Nodes (33): control_config_enabled CMake Function, ESP_MATTER_PATH External Checkout, Experimental Matter Configuration, control_board ESP-IDF Component, lilygo_epd47 ESP-IDF Component, LILYGO EPD47 Upstream Driver Snapshot, External ESP-Matter Patch Reproducibility Policy, PeerAddress Namespace Qualification Patch (+25 more)

### Community 11 - "esp_err_t"
Cohesion: 0.18
Nodes (21): clearNvsNamespace(), esp_err_t, logEspStepResult(), logNvsStorageStatus(), matter_controller_clear_device_cache(), matter_controller_commission_with_manual_code(), matter_controller_early_init(), matter_controller_forget_saved_node() (+13 more)

### Community 12 - "TouchManager"
Cohesion: 0.07
Nodes (27): i2c_master_bus_handle_t, i2c_master_dev_handle_t, TouchManager, bus_, calibrationMode_, calibrationPoint_, contactActive_, controllerDetected_ (+19 more)

### Community 13 - "MatterDeviceItem"
Cohesion: 0.07
Nodes (28): boundedString(), vector, DeviceCacheRecord, device_type, display_name, endpoint_id, flags, last_seen (+20 more)

### Community 14 - "Matter Configurator Page"
Cohesion: 0.07
Nodes (27): ESP-IDF Component Registration, CONTROL_MAIN_REQUIRES, idf_component_register, app.js Module, Matter Configurator Page, Connect Button, Export JSON Button, Import JSON Button (+19 more)

### Community 15 - "MatterPostCommissioningResult"
Cohesion: 0.18
Nodes (11): MatterPostCommissioningStatus, string, MatterPostCommissioningResult, activeEndpoint, commandState, commandStateKnown, devices, error (+3 more)

### Community 16 - ".renderUi"
Cohesion: 0.12
Nodes (9): appStateName(), AppState, app_main(), keyboardModeName(), updateControlUiContext(), TouchEventType, touchEventName(), weather_poll_refresh_complete() (+1 more)

### Community 17 - "DevPaaBypassVerifier"
Cohesion: 0.10
Nodes (24): AttestationInfo, AttestationVerificationResult, ByteSpan, Callback, CHIP_ERROR, CommissioningStage, Credentials::DeviceAttestationVerifier, DeviceAttestationRevocationDelegate (+16 more)

### Community 18 - "matter_controller.cpp"
Cohesion: 0.16
Nodes (24): ChipDeviceEvent, commissioningFlowName(), copyBounded(), CommissioningFlow, MatterCommissioningStatus, SetupPayload, deviceToRecord(), discoveryCapabilitiesText() (+16 more)

### Community 19 - "epd_driver.c"
Cohesion: 0.15
Nodes (25): bit_shift_buffer_right(), DrawMode_t, calc_epd_input_1bpp(), epd_draw_circle(), epd_draw_frame_1bit(), epd_draw_hline(), epd_draw_line(), epd_draw_oval() (+17 more)

### Community 20 - "ReadWaitContext"
Cohesion: 0.08
Nodes (25): CommandWaitContext, chip_error, done, endpoint_id, got_result, array, MATTER_DISCOVERY_MAX_ENDPOINTS, SemaphoreHandle_t (+17 more)

### Community 21 - "ui_manager.cpp"
Cohesion: 0.24
Nodes (24): string, keyboardModeName(), UiManager, formattedMatterCode, maskedPassword, refreshKeyboard, refreshMatterCodeField, refreshPasswordField (+16 more)

### Community 22 - "crc32.c"
Cohesion: 0.24
Nodes (14): local, uLong, crc32(), crc32_big(), crc32_combine_(), crc32_combine64(), crc32_little(), crc32_z() (+6 more)

### Community 23 - "test-usbipd-state.sh"
Cohesion: 0.09
Nodes (24): assert_contains(), assert_forwarded_override(), assert_no_attach_command(), assert_no_real_attach(), CONTROL_BUILD_DIR, CONTROL_IDF_EXPORT, CONTROL_LOG_DIR, CONTROL_MATTER_EXPORT (+16 more)

### Community 24 - "inflate.c"
Cohesion: 0.07
Nodes (26): adler32(), adler32_combine_(), adler32_combine64(), adler32_z(), local, uLong, local, fixedtables() (+18 more)

### Community 25 - "CONTROL_MAIN_EXPERIMENTAL_MATTER"
Cohesion: 0.11
Nodes (20): DiscoveryType::kDiscoveryNetworkOnly, DNS-SD Commissionable Discovery, ESP-Matter Controller Helper, PairDevice, pairing_code, chip Dependency, CONTROL_MAIN_EXPERIMENTAL_MATTER, CONTROL_MAIN_RECOVERY (+12 more)

### Community 26 - "control_time_wait_synced"
Cohesion: 0.40
Nodes (9): control_time_has_valid_utc(), control_time_is_synced(), control_time_start_sntp_once(), control_time_wait_synced(), esp_err_t, formatUnixTime(), formatUtc(), logCurrentUtc() (+1 more)

### Community 27 - "main.cpp"
Cohesion: 0.14
Nodes (6): string, IkeaDiscoveryClassification, wifi_auth_mode_t, formatMatterNodeId(), ikeaClassificationName(), wifiAuthName()

### Community 28 - "touch_manager.cpp"
Cohesion: 0.27
Nodes (14): esp_err_t, irqModeName(), initialize, irqHandler, makeEvent, markUiDispatched, poll, readRegister (+6 more)

### Community 29 - "control_ui.cpp"
Cohesion: 0.24
Nodes (17): activeCount, candidateCount, lockRender, render, renderAssignRoom, renderClearConfirm, renderDeviceScan, renderHome (+9 more)

### Community 30 - "string"
Cohesion: 0.12
Nodes (23): esp_partition_type_t, controllerLargestInternalHeapReady(), controllerStateName(), esp_ip4_addr_t, esp_ip6_addr_t, string, ensureChipUniqueIdProvisioned(), internalHeapLooksLow() (+15 more)

### Community 31 - "ikea_discovery.cpp"
Cohesion: 0.25
Nodes (17): classificationName(), containsIkeaIdentity(), esp_ip4_addr_t, IkeaDiscoveryClassification, string, firstIpv4(), browseServices, classifyCandidate (+9 more)

### Community 32 - "control"
Cohesion: 0.17
Nodes (19): control script, check_project_files(), clean_command(), configure_environment(), detect_usb_busid(), die(), flash_command(), is_usbipd_list() (+11 more)

### Community 33 - "TouchDiagnostics"
Cohesion: 0.11
Nodes (19): string, TouchInternalState, TouchDiagnostics, address, controllerDetected, coordinatesReadable, eventsWorking, internalState (+11 more)

### Community 34 - "StoredSettings"
Cohesion: 0.33
Nodes (6): string, load, StoredSettings, initialSetupComplete, password, ssid

### Community 35 - "IkeaDiscovery"
Cohesion: 0.13
Nodes (15): atomic, TaskHandle_t, IkeaDiscovery, cancel, cancelRequested_, complete_, initializeMdns, mdnsInitialized_ (+7 more)

### Community 36 - "weather_manager.cpp"
Cohesion: 0.26
Nodes (15): esp_err_t, WeatherCurrent, WeatherForecastDay, makeMockSnapshot(), nowSeconds(), parseProviderJson(), refreshTask(), setText() (+7 more)

### Community 37 - "Graphify Knowledge-Graph Pipeline"
Cohesion: 0.17
Nodes (16): Project Graphify Integration Rules, AST Structural Extraction, Existing-Graph Fast Path, Graphify Knowledge-Graph Pipeline, Graphify Query, Path, and Explain, Semantic Document Extraction, Graphify Folder Watcher, Graphify URL Ingest (+8 more)

### Community 38 - "Rect_t"
Cohesion: 0.33
Nodes (9): epd_clear(), epd_clear_area(), epd_clear_area_cycles(), epd_copy_to_framebuffer(), epd_draw_grayscale_image(), epd_draw_image(), epd_full_screen(), commitFull (+1 more)

### Community 39 - ".handleAction"
Cohesion: 0.13
Nodes (6): AppState, esp_err_t, matter_controller_get_last_result(), matter_controller_get_post_commissioning_result(), matter_controller_set_pending_commissioning_mode(), control_heap_log()

### Community 40 - "activate"
Cohesion: 0.19
Nodes (14): activate, beginOnboarding, handleTouch, markScreen, roomName, screenName, showHome, showRoom (+6 more)

### Community 41 - "Matter SendNOC / AddNOC Status 9 Recovery"
Cohesion: 0.15
Nodes (13): attestation_failed, chip-factory, Clean Matter Local Reset, DIRIGERA, idf.py erase-flash, Fabric Conflict, IKEA Home Smart, Initial Commissioning (+5 more)

### Community 42 - "WifiConnectionInfo"
Cohesion: 0.15
Nodes (13): start, WifiConnectionInfo, bssid, dns, gateway, hasIpv4, hasIpv6LinkLocal, ipv4 (+5 more)

### Community 43 - "MatterDiscoveredDevice"
Cohesion: 0.15
Nodes (13): MatterDiscoveredDevice, display_name, endpoint_id, has_level_control, has_onoff, is_aggregator, is_light_candidate, node_id (+5 more)

### Community 44 - "tools/dev/control.sh"
Cohesion: 0.15
Nodes (13): clean-build, build_matter_stable and Temporary SDK Config, tools/dev/control.sh, Developer Automation Toolkit, Diagnostic Capture, flash, flash-diag, Interactive Monitor (+5 more)

### Community 45 - "MatterUiResult"
Cohesion: 0.33
Nodes (6): esp_err_t, MatterUiResult, command, error, success, completeMatterCommand

### Community 46 - "IkeaDiscoveryResult"
Cohesion: 0.17
Nodes (12): IkeaDiscoveryClassification, string, IkeaDiscoveryResult, classification, hostname, instanceName, ip, port (+4 more)

### Community 47 - "matter_pairing_code.cpp"
Cohesion: 0.31
Nodes (10): commissioningFlowName(), CommissioningFlow, esp_err_t, SetupPayload, string, discoveryCapabilitiesText(), isMatterCodeFormatSupported(), logSetupPayloadDetails() (+2 more)

### Community 48 - "MatterUiCommand"
Cohesion: 0.17
Nodes (12): CommandWorkerArgs, discovery, from_ui, ui_command, MatterUiAction, MatterUiCommand, action, batch_id (+4 more)

### Community 49 - "TouchEvent"
Cohesion: 0.20
Nodes (10): TouchEventType, TouchEvent, count, irqLevel, rawX, rawY, status, type (+2 more)

### Community 50 - "windows-esptool-flash.ps1"
Cohesion: 0.27
Nodes (13): Assert-Test(), Escape-Diagnostic(), Invoke-EspTool(), Invoke-VersionCheck(), Join-WindowsArguments(), New-CommandModel(), Parse-CommandLine(), Quote-WindowsArgument() (+5 more)

### Community 51 - "MatterDiscoveryResult"
Cohesion: 0.18
Nodes (11): array, MATTER_DISCOVERY_MAX_ENDPOINTS, MatterDiscoveryResult, devices, endpoint_count, error, light_count, node_id (+3 more)

### Community 52 - "MatterSavedNode"
Cohesion: 0.15
Nodes (13): MatterSavedNode, attestation_bypassed, attestation_verified, commissioned, commissioned_at_unix_or_boot_counter, commissioning_mode, controller_node_id, dev_insecure_mode (+5 more)

### Community 53 - "UiAction"
Cohesion: 0.27
Nodes (10): action(), actionName(), UiActionType, UiActionType, UiAction, character, index, type (+2 more)

### Community 54 - "ed047tc1.c"
Cohesion: 0.11
Nodes (34): busy_delay(), esp_err_t, gpio_num_t, epd_base_init(), epd_end_frame(), epd_get_current_buffer(), epd_output_row(), epd_poweroff() (+26 more)

### Community 57 - "RoomGroup"
Cohesion: 0.25
Nodes (7): vector, RoomGroup, controllable_count, endpoint_count, endpoint_ids, lights_on_count, name

### Community 58 - ".processMatter"
Cohesion: 0.22
Nodes (4): string, TaskHandle_t, MatterPostCommissioningStatus, matter_post_commissioning_status_name()

### Community 59 - "font.c"
Cohesion: 0.20
Nodes (15): DrawMode_t, draw_char(), font_properties_default(), get_char_bounds(), get_glyph(), get_text_bounds(), next_cp(), utf8_len() (+7 more)

### Community 60 - "matter_saved_state_load_and_validate"
Cohesion: 0.15
Nodes (22): bindControlUiMatter(), startMatterCommissioningWorker(), commissioning_success_callback(), MatterSavedState, dumpLocalStorageSummaryLocked(), ensureCommandWorker(), ensureSavedNodeLoaded(), formatMatterId() (+14 more)

### Community 63 - "commandSuccessCallback"
Cohesion: 0.33
Nodes (6): ConcreteCommandPath, ConcreteDataAttributePath, commandSuccessCallback(), readAttributeCallback(), StatusIB, TLVReader

### Community 64 - "SettingsStore"
Cohesion: 0.23
Nodes (10): nvs_handle_t, string, getString(), SettingsStore, eraseWifi, initialize, ready_, saveWifi (+2 more)

### Community 65 - "init"
Cohesion: 0.40
Nodes (5): init, resetRealModel, control_ui_mock_reset(), control_ui_mock_rooms(), UiRoom

### Community 66 - "RawSavedNodeRead"
Cohesion: 0.17
Nodes (12): boolFromU8(), nvs_handle_t, nvsGetString(), RawSavedNodeRead, any_key_present, commissioned_key_present, error, fabric_index_key_present (+4 more)

### Community 68 - "rebuildFromConfig"
Cohesion: 0.53
Nodes (6): bindMatterDevices, bindPersistedDevices, rebuildFromConfig, saveOnboarding, UiConfig, vector

### Community 69 - "readDoneCallback"
Cohesion: 0.50
Nodes (4): AttributePathParams, EventPathParams, readDoneCallback(), ScopedMemoryBufferWithSize

### Community 70 - "PingProbeContext"
Cohesion: 0.50
Nodes (4): SemaphoreHandle_t, PingProbeContext, done, reachable

### Community 71 - "DeviceCacheHeader"
Cohesion: 0.50
Nodes (4): DeviceCacheHeader, count, magic, version

### Community 72 - "i2s_data_bus.c"
Cohesion: 0.17
Nodes (11): esp_err_t, dma_desc_addr(), fill_dma_desc(), gpio_setup_out(), i2s_bus_init(), i2s_start_line_output(), notify_trans_done(), esp_lcd_panel_io_event_data_t (+3 more)

### Community 73 - "RendezvousParameters::SetPeerAddress"
Cohesion: 0.67
Nodes (3): CHIP Controller API, Direct-IP Commissioning Unavailable, RendezvousParameters::SetPeerAddress

### Community 74 - "IkeaDiscoveryEvent"
Cohesion: 0.40
Nodes (5): IkeaDiscoveryEventType, poll, IkeaDiscoveryEvent, result, type

### Community 77 - "queueDevice"
Cohesion: 0.20
Nodes (10): candidateAt, chooseRoom, queueDevice, queueRoom, renderRoomDetail, setContext, copyText(), UiDevice (+2 more)

### Community 88 - "MatterOnboarding"
Cohesion: 0.33
Nodes (8): string, MatterOnboarding, commissionWithManualCode, commissionWithQrPayload, initializeControllerPlaceholder, notImplementedMessage, requestEnvironmentSearch, requestInstructions

### Community 89 - "esp_ping_handle_t"
Cohesion: 0.67
Nodes (3): esp_ping_handle_t, onPingEnd(), onPingSuccess()

### Community 90 - "ServiceQuery"
Cohesion: 0.67
Nodes (3): ServiceQuery, proto, type

### Community 92 - "UiRect"
Cohesion: 0.40
Nodes (5): UiRect, height, width, x, y

### Community 93 - "MatterPairingInput"
Cohesion: 0.33
Nodes (5): string, MatterPairingInput, format_valid, normalized_digits, raw_input

### Community 95 - "ui_config.h"
Cohesion: 0.40
Nodes (4): bindings(), UiConfig, UiConfigBinding, ui()

## Knowledge Gaps
- **502 isolated node(s):** `length`, `request_id`, `cmd`, `json`, `request_id` (+497 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **8 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ControlApplication` connect `ControlApplication` to `WifiManager`, `MatterCommissioningResult`, `ControlUi`, `DisplayManager`, `TouchManager`, `MatterPostCommissioningResult`, `.renderUi`, `ui_manager.cpp`, `main.cpp`, `IkeaDiscovery`, `weather_manager.cpp`, `.handleAction`, `IkeaDiscoveryResult`, `MatterSavedNode`, `RoomGroup`, `.processMatter`, `SettingsStore`, `MatterOnboarding`, `MatterPairingInput`?**
  _High betweenness centrality (0.199) - this node is a cross-community bridge._
- **Why does `UiViewModel` connect `UiViewModel` to `WifiManager`, `WifiConnectionInfo`, `IkeaDiscoveryResult`, `MatterPostCommissioningResult`, `.renderUi`, `MatterSavedNode`, `ui_manager.cpp`, `UiAction`, `RoomGroup`?**
  _High betweenness centrality (0.075) - this node is a cross-community bridge._
- **Why does `MatterPostCommissioningResult` connect `MatterPostCommissioningResult` to `control_config_serial.cpp`, `ControlApplication`, `UiViewModel`, `.handleAction`, `MatterDeviceItem`, `MatterUiResult`, `MatterDiscoveryResult`, `MatterSavedNode`, `RoomGroup`, `matter_saved_state_load_and_validate`?**
  _High betweenness centrality (0.046) - this node is a cross-community bridge._
- **What connects `length`, `request_id`, `cmd` to the rest of the system?**
  _502 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `control_config_serial.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.05661005661005661 - nodes in this community are weakly interconnected._
- **Should `app.js` be split into smaller, more focused modules?**
  _Cohesion score 0.08050655811849841 - nodes in this community are weakly interconnected._
- **Should `ControlApplication` be split into smaller, more focused modules?**
  _Cohesion score 0.03333333333333333 - nodes in this community are weakly interconnected._