cmake_minimum_required(VERSION 3.16)

function(control_config_enabled OUT SYMBOL)
    set(ENABLED OFF)
    set(CONFIG_FILES "${CMAKE_SOURCE_DIR}/sdkconfig")
    if(DEFINED SDKCONFIG_DEFAULTS)
        foreach(DEFAULT_FILE ${SDKCONFIG_DEFAULTS})
            if(IS_ABSOLUTE "${DEFAULT_FILE}")
                list(APPEND CONFIG_FILES "${DEFAULT_FILE}")
            else()
                list(APPEND CONFIG_FILES "${CMAKE_SOURCE_DIR}/${DEFAULT_FILE}")
            endif()
        endforeach()
    else()
        list(APPEND CONFIG_FILES "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    endif()
    foreach(CONFIG_FILE ${CONFIG_FILES})
        if(EXISTS "${CONFIG_FILE}")
            file(READ "${CONFIG_FILE}" CONFIG_CONTENT)
            if(CONFIG_CONTENT MATCHES "(^|\n)CONFIG_${SYMBOL}=y(\n|$)")
                set(ENABLED ON)
            endif()
            if(CONFIG_CONTENT MATCHES "(^|\n)# CONFIG_${SYMBOL} is not set(\n|$)")
                set(ENABLED OFF)
            endif()
        endif()
    endforeach()
    set(${OUT} ${ENABLED} PARENT_SCOPE)
endfunction()

control_config_enabled(CONTROL_EXPERIMENTAL_MATTER CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL)

if(CONTROL_EXPERIMENTAL_MATTER)
    if(NOT DEFINED ENV{ESP_MATTER_PATH})
        message(FATAL_ERROR
            "ESP_MATTER_PATH must point to a complete official esp-matter checkout.\n"
            "Run:\n"
            "  export ESP_MATTER_PATH=/home/k/esp/esp-matter\n"
            "  source /home/k/esp/esp-matter/export.sh")
    endif()

    set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
    set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)
    foreach(REQUIRED_PATH
            "${ESP_MATTER_PATH}/components"
            "${ESP_MATTER_PATH}/components/esp_matter_controller"
            "${MATTER_SDK_PATH}"
            "${MATTER_SDK_PATH}/config/esp32/components")
        if(NOT EXISTS "${REQUIRED_PATH}")
            message(FATAL_ERROR
                "ESP_MATTER_PATH must point to a complete official esp-matter checkout.\n"
                "Missing: ${REQUIRED_PATH}\n"
                "Run:\n"
                "  export ESP_MATTER_PATH=/home/k/esp/esp-matter\n"
                "  source /home/k/esp/esp-matter/export.sh")
        endif()
    endforeach()

    set(EXTRA_COMPONENT_DIRS
        "${MATTER_SDK_PATH}/config/esp32/components"
        "${ESP_MATTER_PATH}/components")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(control)

if(CONTROL_EXPERIMENTAL_MATTER)
    idf_build_set_property(CXX_COMPILE_OPTIONS
        "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
    idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
    idf_build_set_property(COMPILE_OPTIONS
        "-D_NO_GENERIC_THREAD_NETWORK_COMMISSIONING_DRIVER_;-Wno-format-nonliteral;-Wno-format-security;-Wformat=0" APPEND)
endif()
