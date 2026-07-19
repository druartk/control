#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"

/*
 * Physical target: LILYGO T5 4.7 Plus V2.4, PCB 2024-12-03.
 *
 * Pin sources, all from LILYGO's official LilyGo-EPD47 repository at
 * commit 5b1adc7100cce95529ace84278fcdc3aa0439c85:
 *   - schematic/Screen-4.7-S3-V2.4 24-12-03.pdf (exact PCB revision)
 *   - src/ed047tc1.h (ESP32-S3 display bus)
 *   - src/utilities.h and examples/touch/touch.ino (GT911 and auxiliaries)
 *   - boards/esp32s3/lilygo_t5_epaper_s3.json (flash/PSRAM/USB assumptions)
 *
 * This header intentionally describes only the confirmed V2.4 hardware. It
 * must not grow speculative aliases copied from the older ESP32 board.
 */

#if !CONFIG_IDF_TARGET_ESP32S3
#error "Control supports only the ESP32-S3 T5 4.7 Plus V2.4 target"
#endif
#if !CONFIG_ESPTOOLPY_FLASHSIZE_16MB || !CONFIG_ESPTOOLPY_FLASHMODE_QIO
#error "T5 4.7 Plus V2.4 requires the N16 module configuration: 16 MB QIO flash"
#endif
#if !CONFIG_SPIRAM_MODE_OCT || !CONFIG_SPIRAM_SPEED_80M
#error "T5 4.7 Plus V2.4 requires the R8 configuration: 8 MB OPI PSRAM at 80 MHz"
#endif
#if !CONFIG_ESP_CONSOLE_USB_CDC
#error "Control expects the V2.4 native USB connector as the primary CDC console"
#endif

#define CONTROL_BOARD_NAME              "LILYGO T5 4.7 Plus"
#define CONTROL_BOARD_REVISION          "V2.4"
#define CONTROL_BOARD_PCB_DATE          "2024-12-03"
#define CONTROL_BOARD_IDENTITY          CONTROL_BOARD_NAME " " CONTROL_BOARD_REVISION
#define CONTROL_BOARD_MODULE            "ESP32-S3-WROOM-1-N16R8"

/* ED047 display geometry: official S3 driver and V2.4 schematic. */
#define CONTROL_BOARD_DISPLAY_WIDTH     960
#define CONTROL_BOARD_DISPLAY_HEIGHT    540

/*
 * ED047 8-bit parallel bus: official src/ed047tc1.h, cross-checked against
 * the V2.4 schematic nets EP_D0..EP_D7, EP_CKV, EP_STH and EP_CKH.
 */
#define CONTROL_BOARD_EPD_D0            GPIO_NUM_8
#define CONTROL_BOARD_EPD_D1            GPIO_NUM_1
#define CONTROL_BOARD_EPD_D2            GPIO_NUM_2
#define CONTROL_BOARD_EPD_D3            GPIO_NUM_3
#define CONTROL_BOARD_EPD_D4            GPIO_NUM_4
#define CONTROL_BOARD_EPD_D5            GPIO_NUM_5
#define CONTROL_BOARD_EPD_D6            GPIO_NUM_6
#define CONTROL_BOARD_EPD_D7            GPIO_NUM_7
#define CONTROL_BOARD_EPD_CKV           GPIO_NUM_38
#define CONTROL_BOARD_EPD_STH           GPIO_NUM_40
#define CONTROL_BOARD_EPD_CKH           GPIO_NUM_41

/*
 * 74HCT4094 display control bus: V2.4 nets STR_IO0, CP and DATA.
 * GPIO0 is shared with BOOT. The official shift-register sequence is the only
 * permitted way to control display power; there is no direct power GPIO.
 * QP0..QP7 are EP_LE, SMPS_CTRL, POS_CTRL, NEG_CTRL, PWR_EN, EP_MODE,
 * EP_OE and EP_STV respectively. On V2.4, POS_CTRL/NEG_CTRL series links are
 * marked NC in the schematic; the official sequence is retained unchanged.
 */
#define CONTROL_BOARD_EPD_CFG_STR        GPIO_NUM_0
#define CONTROL_BOARD_EPD_CFG_CLK        GPIO_NUM_12
#define CONTROL_BOARD_EPD_CFG_DATA       GPIO_NUM_13
#define CONTROL_BOARD_EPD_POWER_GPIO     GPIO_NUM_NC
#define CONTROL_BOARD_EPD_POWER_VIA_4094 1

/*
 * GT911: exact V2.4 connector/FPC nets plus official touch example.
 * T_RST is present on the touch FPC but is not routed to an ESP32-S3 GPIO;
 * therefore firmware must never attempt to drive a reset or touch-power pin.
 * The official example briefly drives IRQ high to wake/probe the controller,
 * then restores it to input mode.
 */
#define CONTROL_BOARD_TOUCH_SDA          GPIO_NUM_18
#define CONTROL_BOARD_TOUCH_SCL          GPIO_NUM_17
#define CONTROL_BOARD_TOUCH_IRQ          GPIO_NUM_47
#define CONTROL_BOARD_TOUCH_RESET        GPIO_NUM_NC
#define CONTROL_BOARD_TOUCH_POWER        GPIO_NUM_NC
#define CONTROL_BOARD_TOUCH_WAKE_LEVEL   1

/*
 * Touch orientation from LILYGO examples/touch/touch.ino:
 *   setMaxCoordinates(960, 540), setSwapXY(true), setMirrorXY(false, true).
 * The GT911 reports the panel in its native portrait axes (X=0..539,
 * Y=0..959). Transform in this order: clamp native, swap XY, invert Y, clamp
 * to the 960x540 landscape display. Display maxima are inclusive minus one.
 */
#define CONTROL_BOARD_TOUCH_NATIVE_MAX_X 539
#define CONTROL_BOARD_TOUCH_NATIVE_MAX_Y 959
#define CONTROL_BOARD_TOUCH_SWAP_XY      1
#define CONTROL_BOARD_TOUCH_INVERT_X     0
#define CONTROL_BOARD_TOUCH_INVERT_Y     1

/* Confirmed auxiliary signals; not enabled by this milestone. */
#define CONTROL_BOARD_BATTERY_ADC        GPIO_NUM_14
#define CONTROL_BOARD_BUTTON             GPIO_NUM_21
#define CONTROL_BOARD_BOOT               GPIO_NUM_0
#define CONTROL_BOARD_SD_MISO            GPIO_NUM_16
#define CONTROL_BOARD_SD_MOSI            GPIO_NUM_15
#define CONTROL_BOARD_SD_SCLK            GPIO_NUM_11
#define CONTROL_BOARD_SD_CS              GPIO_NUM_42

/* Native ESP32-S3 USB D-/D+ nets from the V2.4 Type-C schematic section. */
#define CONTROL_BOARD_USB_DM             GPIO_NUM_19
#define CONTROL_BOARD_USB_DP             GPIO_NUM_20
#define CONTROL_BOARD_USB_NATIVE_CDC     1
#define CONTROL_BOARD_RESET_USES_CHIP_PU 1

/* Module and official board JSON expectations. */
#define CONTROL_BOARD_FLASH_BYTES        (16U * 1024U * 1024U)
#define CONTROL_BOARD_FLASH_MODE_QIO     1
#define CONTROL_BOARD_FLASH_FREQ_MHZ     80
#define CONTROL_BOARD_PSRAM_BYTES        (8U * 1024U * 1024U)
#define CONTROL_BOARD_PSRAM_MODE_OPI      1
#define CONTROL_BOARD_PSRAM_FREQ_MHZ     80
