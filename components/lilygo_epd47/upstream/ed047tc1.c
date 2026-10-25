
/******************************************************************************/
/***        include files                                                   ***/
/******************************************************************************/

#include "ed047tc1.h"
#include "i2s_data_bus.h"
#include "rmt_pulse.h"
#include <freertos/FreeRTOS.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#include <soc/gpio_struct.h>  // GPIO.out_w1ts / GPIO.out_w1tc — required in IDF 5.x
#else
#include <hal/gpio_ll.h>
#endif
#include <xtensa/core-macros.h>

#include <string.h>

/******************************************************************************/
/***        macro definitions                                               ***/
/******************************************************************************/

/******************************************************************************/
/***        type definitions                                                ***/
/******************************************************************************/

typedef struct
{
    bool ep_latch_enable : 1;
    bool power_disable : 1;
    bool pos_power_enable : 1;
    bool neg_power_enable : 1;
    bool ep_stv : 1;
    bool ep_scan_direction : 1;
    bool ep_mode : 1;
    bool ep_output_enable : 1;
} epd_config_register_t;

/******************************************************************************/
/***        local function prototypes                                       ***/
/******************************************************************************/

/******************************************************************************/
/***        exported variables                                              ***/
/******************************************************************************/

/******************************************************************************/
/***        local variables                                                 ***/
/******************************************************************************/

static epd_config_register_t config_reg;
static portMUX_TYPE frame_start_mux = portMUX_INITIALIZER_UNLOCKED;

/******************************************************************************/
/***        exported functions                                              ***/
/******************************************************************************/

inline static void fast_gpio_set_hi(gpio_num_t gpio_num)
{
    if (gpio_num < 32) GPIO.out_w1ts = (1U << gpio_num);
    else GPIO.out1_w1ts.val = (1U << (gpio_num - 32));
}

inline static void fast_gpio_set_lo(gpio_num_t gpio_num)
{
    if (gpio_num < 32) GPIO.out_w1tc = (1U << gpio_num);
    else GPIO.out1_w1tc.val = (1U << (gpio_num - 32));
}

inline static void IRAM_ATTR push_cfg_bit(bool bit)
{
    fast_gpio_set_lo(CFG_CLK);
    if (bit)
    {
        fast_gpio_set_hi(CFG_DATA);
    }
    else
    {
        fast_gpio_set_lo(CFG_DATA);
    }
    fast_gpio_set_hi(CFG_CLK);
}

static void IRAM_ATTR push_cfg(epd_config_register_t *cfg)
{
    fast_gpio_set_lo(CFG_STR);

    // push config bits in reverse order
    push_cfg_bit(cfg->ep_output_enable);
    push_cfg_bit(cfg->ep_mode);
    push_cfg_bit(cfg->ep_scan_direction);
    push_cfg_bit(cfg->ep_stv);

    push_cfg_bit(cfg->neg_power_enable);
    push_cfg_bit(cfg->pos_power_enable);
    push_cfg_bit(cfg->power_disable);
    push_cfg_bit(cfg->ep_latch_enable);

    fast_gpio_set_hi(CFG_STR);
}


void IRAM_ATTR busy_delay(uint32_t cycles)
{
    volatile uint64_t counts = XTHAL_GET_CCOUNT() + cycles;
    while (XTHAL_GET_CCOUNT() < counts) ;
}


esp_err_t epd_base_init(uint32_t epd_row_width)
{
    config_reg.ep_latch_enable = false;
    config_reg.power_disable = true;
    config_reg.pos_power_enable = false;
    config_reg.neg_power_enable = false;
    config_reg.ep_stv = true;
    config_reg.ep_scan_direction = true;
    config_reg.ep_mode = false;
    config_reg.ep_output_enable = false;

    const gpio_num_t output_pins[] = {
        CFG_DATA, CFG_CLK, CFG_STR, CKV, STH, CKH,
        D0, D1, D2, D3, D4, D5, D6, D7,
    };
    uint64_t output_mask = 0;
    for (unsigned i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); ++i) {
        output_mask |= 1ULL << output_pins[i];
        // Startup can leave the data pads at the weakest drive setting.
        esp_err_t err = gpio_set_drive_capability(output_pins[i], GPIO_DRIVE_CAP_DEFAULT);
        if (err != ESP_OK) return err;
    }
    // Select digital pads before the LCD/RMT drivers attach their peripheral signals.
    const gpio_config_t display_pins = {
        .pin_bit_mask = output_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&display_pins);
    if (err != ESP_OK) return err;
    fast_gpio_set_lo(CFG_STR);

    push_cfg(&config_reg);

    // Setup I2S
    i2s_bus_config i2s_config;
    // add an offset off dummy bytes to allow for enough timing headroom
    i2s_config.epd_row_width = epd_row_width + 32;
    i2s_config.clock = CKH;
    i2s_config.start_pulse = STH;
    i2s_config.data_0 = D0;
    i2s_config.data_1 = D1;
    i2s_config.data_2 = D2;
    i2s_config.data_3 = D3;
    i2s_config.data_4 = D4;
    i2s_config.data_5 = D5;
    i2s_config.data_6 = D6;
    i2s_config.data_7 = D7;

    err = i2s_bus_init(&i2s_config);
    if (err != ESP_OK) return err;

    return rmt_pulse_init(CKV);
}

void epd_poweron()
{
    config_reg.ep_scan_direction = true;
    config_reg.power_disable = false;
    push_cfg(&config_reg);
    busy_delay(100 * 240);
    config_reg.neg_power_enable = true;
    push_cfg(&config_reg);
    busy_delay(500 * 240);
    config_reg.pos_power_enable = true;
    push_cfg(&config_reg);
    busy_delay(100 * 240);
    config_reg.ep_stv = true;
    push_cfg(&config_reg);
    fast_gpio_set_hi(STH);
}

void epd_poweroff()
{
    config_reg.pos_power_enable = false;
    push_cfg(&config_reg);
    busy_delay(10 * 240);
    config_reg.neg_power_enable = false;
    push_cfg(&config_reg);
    busy_delay(100 * 240);
    config_reg.power_disable = true;
    push_cfg(&config_reg);

    config_reg.ep_stv = false;
    push_cfg(&config_reg);
}

void epd_poweroff_all()
{
    memset(&config_reg, 0, sizeof(config_reg));
    push_cfg(&config_reg);
}

void IRAM_ATTR epd_start_frame()
{
    while (i2s_is_busy()) ;
    pulse_ckv_wait();

    config_reg.ep_mode = true;
    push_cfg(&config_reg);

    pulse_ckv_us(1, 1, true);

    pulse_ckv_prepare_frame_start();
    // STV must be released during CKV, without a Wi-Fi interrupt or flash-cache stall.
    portENTER_CRITICAL(&frame_start_mux);
    config_reg.ep_stv = false;
    push_cfg(&config_reg);
    busy_delay(240);
    pulse_ckv_start_frame();
    config_reg.ep_stv = true;
    push_cfg(&config_reg);
    portEXIT_CRITICAL(&frame_start_mux);
    pulse_ckv_finish_frame_start();
    pulse_ckv_us(0, 10, true);

    config_reg.ep_output_enable = true;
    push_cfg(&config_reg);

    pulse_ckv_us(1, 1, true);
}

static inline void latch_row()
{
    config_reg.ep_latch_enable = true;
    push_cfg(&config_reg);

    config_reg.ep_latch_enable = false;
    push_cfg(&config_reg);
}

void  epd_skip()
{
#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2)
    pulse_ckv_ticks(2, 2, false);
#else
    // According to the spec, the OC4 maximum CKV frequency is 200kHz.
    pulse_ckv_ticks(45, 5, false);
#endif
}

void  epd_output_row(uint32_t output_time_dus)
{
    while (i2s_is_busy());
    // LCD DMA can finish before CKV; latching then corrupts the active row.
    pulse_ckv_wait();

    latch_row();

    pulse_ckv_ticks(output_time_dus, 50, false);

    i2s_start_line_output();
    i2s_switch_buffer();
}

void epd_end_frame()
{
    while (i2s_is_busy());
    pulse_ckv_wait();
    config_reg.ep_output_enable = false;
    push_cfg(&config_reg);
    config_reg.ep_mode = false;
    push_cfg(&config_reg);
    pulse_ckv_us(1, 1, true);
    pulse_ckv_us(1, 1, true);
}

void  epd_switch_buffer()
{
    i2s_switch_buffer();
}

uint8_t *  epd_get_current_buffer()
{
    return (uint8_t *)i2s_get_current_buffer();
}

/******************************************************************************/
/***        local functions                                                 ***/
/******************************************************************************/

/******************************************************************************/
/***        END OF FILE                                                     ***/
/******************************************************************************/
