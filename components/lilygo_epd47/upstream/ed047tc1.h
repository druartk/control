#ifndef _ED047TC1_H_
#define _ED047TC1_H_

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/***        include files                                                   ***/
/******************************************************************************/

#include <driver/gpio.h>
#include <esp_attr.h>   // IRAM_ATTR — no longer transitively included in ESP32 Arduino core 3.x
#include <esp_err.h>
#include "board/board_config.h"

#include <stdint.h>

/******************************************************************************/
/***        macro definitions                                               ***/
/******************************************************************************/

#define CFG_DATA CONTROL_BOARD_EPD_CFG_DATA
#define CFG_CLK  CONTROL_BOARD_EPD_CFG_CLK
#define CFG_STR  CONTROL_BOARD_EPD_CFG_STR
#define CKV      CONTROL_BOARD_EPD_CKV
#define STH      CONTROL_BOARD_EPD_STH
#define CKH      CONTROL_BOARD_EPD_CKH
#define D7       CONTROL_BOARD_EPD_D7
#define D6       CONTROL_BOARD_EPD_D6
#define D5       CONTROL_BOARD_EPD_D5
#define D4       CONTROL_BOARD_EPD_D4
#define D3       CONTROL_BOARD_EPD_D3
#define D2       CONTROL_BOARD_EPD_D2
#define D1       CONTROL_BOARD_EPD_D1
#define D0       CONTROL_BOARD_EPD_D0

/******************************************************************************/
/***        type definitions                                                ***/
/******************************************************************************/

/******************************************************************************/
/***        exported variables                                              ***/
/******************************************************************************/

/******************************************************************************/
/***        exported functions                                              ***/
/******************************************************************************/

esp_err_t epd_base_init(uint32_t epd_row_width);
void epd_poweron();
void epd_poweroff();

/**
 * @brief Start a draw cycle.
 */
void epd_start_frame();

/**
 * @brief End a draw cycle.
 */
void epd_end_frame();

/**
 * @brief output row data
 *
 * @note Waits until all previously submitted data has been written.
 *       Then, the following operations are initiated:
 *
 *           1. Previously submitted data is latched to the output register.
 *           2. The RMT peripheral is set up to pulse the vertical (gate) driver
 *              for `output_time_dus` / 10 microseconds.
 *           3. The I2S peripheral starts transmission of the current buffer to
 *              the source driver.
 *           4. The line buffers are switched.
 *
 *       This sequence of operations allows for pipelining data preparation and
 *       transfer, reducing total refresh times.
 */
void  epd_output_row(uint32_t output_time_dus);

/**
 * @brief Skip a row without writing to it.
 */
void  epd_skip();

/**
 * @brief Get the currently writable line buffer.
 */
uint8_t *  epd_get_current_buffer();

/**
 * @brief Switches front and back line buffer.
 *
 * @note If the switched-to line buffer is currently in use, this function
 *       blocks until transmission is done.
 */
void  epd_switch_buffer();

#ifdef __cplusplus
}
#endif

#endif
/******************************************************************************/
/***        END OF FILE                                                     ***/
/******************************************************************************/
