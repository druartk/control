# Vendored from LILYGO's official ESP32-S3 branch at immutable commit:
# 5b1adc7100cce95529ace84278fcdc3aa0439c85
# Only the native ESP-IDF display/font sources are built (not Arduino touch.cpp).
set(EPD_SRC "${CMAKE_CURRENT_LIST_DIR}/upstream")
idf_component_register(
    SRCS
        "${EPD_SRC}/ed047tc1.c"
        "${EPD_SRC}/epd_driver.c"
        "${EPD_SRC}/font.c"
        "${EPD_SRC}/i2s_data_bus.c"
        "${EPD_SRC}/rmt_pulse.c"
        "${EPD_SRC}/zlib/adler32.c"
        "${EPD_SRC}/zlib/crc32.c"
        "${EPD_SRC}/zlib/inffast.c"
        "${EPD_SRC}/zlib/inflate.c"
        "${EPD_SRC}/zlib/inftrees.c"
        "${EPD_SRC}/zlib/uncompr.c"
        "${EPD_SRC}/zlib/zutil.c"
    INCLUDE_DIRS "${EPD_SRC}" "${EPD_SRC}/zlib"
    REQUIRES control_board esp_lcd driver)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function -Wno-unused-variable -Wno-attributes
    -Wno-implicit-fallthrough -Wno-cpp)
