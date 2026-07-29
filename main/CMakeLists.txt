set(CONTROL_MAIN_EXPERIMENTAL_MATTER OFF)
set(CONTROL_MAIN_RECOVERY OFF)
set(CONTROL_MAIN_CONFIG_FILES)
if(DEFINED SDKCONFIG)
    list(APPEND CONTROL_MAIN_CONFIG_FILES "${SDKCONFIG}")
else()
    list(APPEND CONTROL_MAIN_CONFIG_FILES "${CMAKE_SOURCE_DIR}/sdkconfig")
endif()
if(DEFINED SDKCONFIG_DEFAULTS)
    foreach(DEFAULT_FILE ${SDKCONFIG_DEFAULTS})
        if(IS_ABSOLUTE "${DEFAULT_FILE}")
            list(APPEND CONTROL_MAIN_CONFIG_FILES "${DEFAULT_FILE}")
        else()
            list(APPEND CONTROL_MAIN_CONFIG_FILES "${CMAKE_SOURCE_DIR}/${DEFAULT_FILE}")
        endif()
    endforeach()
else()
    list(APPEND CONTROL_MAIN_CONFIG_FILES "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
endif()
foreach(CONFIG_FILE ${CONTROL_MAIN_CONFIG_FILES})
    if(EXISTS "${CONFIG_FILE}")
        file(READ "${CONFIG_FILE}" CONTROL_MAIN_CONFIG_CONTENT)
        if(CONTROL_MAIN_CONFIG_CONTENT MATCHES "(^|\n)CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL=y(\n|$)")
            set(CONTROL_MAIN_EXPERIMENTAL_MATTER ON)
        endif()
        if(CONTROL_MAIN_CONFIG_CONTENT MATCHES "(^|\n)# CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL is not set(\n|$)")
            set(CONTROL_MAIN_EXPERIMENTAL_MATTER OFF)
        endif()
        if(CONTROL_MAIN_CONFIG_CONTENT MATCHES "(^|\n)CONFIG_CONTROL_RECOVERY_BUILD=y(\n|$)")
            set(CONTROL_MAIN_RECOVERY ON)
        endif()
        if(CONTROL_MAIN_CONFIG_CONTENT MATCHES "(^|\n)# CONFIG_CONTROL_RECOVERY_BUILD is not set(\n|$)")
            set(CONTROL_MAIN_RECOVERY OFF)
        endif()
        if(CONTROL_MAIN_CONFIG_CONTENT MATCHES "(^|\n)CONFIG_CONTROL_RECOVERY_BUILD=n(\n|$)")
            set(CONTROL_MAIN_RECOVERY OFF)
        endif()
    endif()
endforeach()

set(CONTROL_MAIN_REQUIRES
    control_board
    lilygo_epd47
    esp_event
    esp_app_format
    esp_netif
    esp_timer
    esp_http_client
    json
    esp_wifi
    lwip
    esp_driver_i2c
    nvs_flash
    app_update)

set(CONTROL_MAIN_PRIV_INCLUDE_DIRS ".")
set(CONTROL_MAIN_LDFRAGMENTS "")

if(CONTROL_MAIN_EXPERIMENTAL_MATTER)
    list(APPEND CONTROL_MAIN_REQUIRES
        esp_matter
        esp_matter_console
        esp_matter_controller
        chip)
    if(DEFINED ENV{ESP_MATTER_PATH})
        list(APPEND CONTROL_MAIN_PRIV_INCLUDE_DIRS
            "$ENV{ESP_MATTER_PATH}/examples/common/utils")
    endif()
    set(CONTROL_MAIN_LDFRAGMENTS "linker.lf")
endif()

if(NOT CONTROL_MAIN_RECOVERY)
    list(APPEND CONTROL_MAIN_REQUIRES mdns)
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "display/display_manager.cpp"
        "touch/touch_manager.cpp"
        "ui/control_ui_model.cpp"
        "ui/control_ui.cpp"
        "ui/ui_manager.cpp"
        "wifi/wifi_manager.cpp"
        "system/control_heap.cpp"
        "system/control_time.cpp"
        "weather/weather_manager.cpp"
        "ikea/ikea_discovery.cpp"
        "matter/matter_controller.cpp"
        "matter/matter_pairing_code.cpp"
        "storage/settings_store.cpp"
        "matter/matter_onboarding.cpp"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS ${CONTROL_MAIN_PRIV_INCLUDE_DIRS}
    REQUIRES ${CONTROL_MAIN_REQUIRES}
    LDFRAGMENTS ${CONTROL_MAIN_LDFRAGMENTS})

if(NOT CONTROL_MAIN_RECOVERY)
    target_sources(${COMPONENT_LIB} PRIVATE "network/mdns_manager.cpp")
endif()

if(CONTROL_MAIN_EXPERIMENTAL_MATTER)
    if(NOT DEFINED ENV{ESP_MATTER_PATH})
        message(FATAL_ERROR
            "CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL requires a clean ESP-Matter "
            "controller integration. Install official espressif/esp-matter and set "
            "ESP_MATTER_PATH, for example: export ESP_MATTER_PATH=~/esp/esp-matter. "
            "Keep CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL disabled for the "
            "current safe UI/stub build.")
    endif()
    target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONTROL_FW_VERSION="0.1.0"
    CONTROL_BUILD_DATE="${CMAKE_BUILD_TYPE}")
