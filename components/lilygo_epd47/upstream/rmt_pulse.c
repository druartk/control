
/******************************************************************************/
/***        include files                                                   ***/
/******************************************************************************/

#include "rmt_pulse.h"

#include <driver/rmt.h>
#include <hal/rmt_ll.h>
#include <soc/rmt_struct.h>

/******************************************************************************/
/***        local variables                                                 ***/
/******************************************************************************/

/**
 * @brief the RMT channel configuration object
 */
static rmt_config_t row_rmt_config;

/******************************************************************************/
/***        exported functions                                              ***/
/******************************************************************************/

esp_err_t rmt_pulse_init(gpio_num_t pin)
{
    row_rmt_config.rmt_mode = RMT_MODE_TX;
    // currently hardcoded: use channel 1
    row_rmt_config.channel = RMT_CHANNEL_1;

    row_rmt_config.gpio_num = pin;
    row_rmt_config.mem_block_num = 2;

    // Divide 80MHz APB Clock by 8 -> .1us resolution delay
    row_rmt_config.clk_div = 8;

    row_rmt_config.tx_config.loop_en = false;
    row_rmt_config.tx_config.carrier_en = false;
    row_rmt_config.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
    row_rmt_config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    row_rmt_config.tx_config.idle_output_en = true;

    esp_err_t err = rmt_config(&row_rmt_config);
    if (err != ESP_OK) return err;
    return rmt_driver_install(row_rmt_config.channel, 0, 0);
}

void IRAM_ATTR pulse_ckv_wait()
{
    ESP_ERROR_CHECK(rmt_wait_tx_done(row_rmt_config.channel, portMAX_DELAY));
}

void pulse_ckv_prepare_frame_start()
{
    pulse_ckv_wait();
    rmt_item32_t items[2] = {0};
    items[0].level0 = 1;
    items[0].duration0 = 100;
    items[0].duration1 = 100;
    // This pulse is polled; the legacy driver's semaphore remains idle.
    ESP_ERROR_CHECK(rmt_set_tx_intr_en(row_rmt_config.channel, false));
    ESP_ERROR_CHECK(rmt_fill_tx_items(row_rmt_config.channel, items, 2, 0));
}

void IRAM_ATTR pulse_ckv_start_frame()
{
    rmt_ll_tx_reset_pointer(&RMT, row_rmt_config.channel);
    rmt_ll_clear_interrupt_status(&RMT, RMT_LL_EVENT_TX_DONE(row_rmt_config.channel));
    rmt_ll_tx_start(&RMT, row_rmt_config.channel);
}

void IRAM_ATTR pulse_ckv_finish_frame_start()
{
    const uint32_t done = RMT_LL_EVENT_TX_DONE(row_rmt_config.channel);
    while (!(rmt_ll_tx_get_interrupt_status_raw(&RMT, row_rmt_config.channel) & done)) ;
    rmt_ll_clear_interrupt_status(&RMT, done);
}

void IRAM_ATTR pulse_ckv_ticks(uint16_t high_time_ticks,
                               uint16_t low_time_ticks, bool wait)
{
    rmt_item32_t rmt_mem_ptr = {0};
    if (high_time_ticks > 0)
    {
        rmt_mem_ptr.level0 = 1;
        rmt_mem_ptr.duration0 = high_time_ticks;
        rmt_mem_ptr.level1 = 0;
        rmt_mem_ptr.duration1 = low_time_ticks;
    }
    else
    {
        rmt_mem_ptr.level0 = 1;
        rmt_mem_ptr.duration0 = low_time_ticks;
        rmt_mem_ptr.level1 = 0;
        rmt_mem_ptr.duration1 = 0;
    }
    // The factory API copies the symbol and starts CKV before returning.
    ESP_ERROR_CHECK(rmt_write_items(row_rmt_config.channel, &rmt_mem_ptr, 1, wait));
}

void IRAM_ATTR pulse_ckv_us(uint16_t high_time_us, uint16_t low_time_us, bool wait)
{
    pulse_ckv_ticks(10 * high_time_us, 10 * low_time_us, wait);
}

/******************************************************************************/
/***        END OF FILE                                                     ***/
/******************************************************************************/
