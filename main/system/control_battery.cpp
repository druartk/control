#include "system/control_battery.h"

#include <algorithm>
#include "board/board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "CONTROL_BATTERY";
adc_oneshot_unit_handle_t adc = nullptr;
adc_cali_handle_t calibration = nullptr;
adc_channel_t channel;

esp_err_t initialize() {
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(CONTROL_BOARD_BATTERY_ADC, &unit, &channel);
    if (err != ESP_OK) return err;
    adc_oneshot_unit_init_cfg_t config{};
    config.unit_id = unit;
    err = adc_oneshot_new_unit(&config, &adc);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t input{};
    input.atten = ADC_ATTEN_DB_12;
    input.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_oneshot_config_channel(adc, channel, &input);
    if (err == ESP_OK) {
        adc_cali_curve_fitting_config_t cali{};
        cali.unit_id = unit;
        cali.chan = channel;
        cali.atten = input.atten;
        cali.bitwidth = input.bitwidth;
        err = adc_cali_create_scheme_curve_fitting(&cali, &calibration);
    }
    if (err != ESP_OK) {
        adc_oneshot_del_unit(adc);
        adc = nullptr;
    }
    return err;
}
}

int control_battery_percent() {
    if (!adc) {
        const esp_err_t err = initialize();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "init_failed err=%s", esp_err_to_name(err));
            return -1;
        }
    }
    int sum = 0, samples = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0;
        const esp_err_t err = adc_oneshot_read(adc, channel, &raw);
        if (err == ESP_OK) { sum += raw; ++samples; }
        else if (err != ESP_ERR_TIMEOUT) return -1;
    }
    int millivolts = 0;
    if (samples < 8 || adc_cali_raw_to_voltage(calibration, sum / samples, &millivolts) != ESP_OK) {
        ESP_LOGW(TAG, "reading_unavailable samples=%d", samples);
        return -1;
    }
    millivolts *= 2; // V2.4 uses a 100 kOhm / 100 kOhm divider on GPIO14.
    if (millivolts < 2500 || millivolts > 4500) return -1;
    // shortcut: voltage estimates Li-ion charge in 5% steps; use a fuel gauge for accurate state of charge.
    const int percent = std::clamp(((millivolts - 3300) * 20 + 450) / 900, 0, 20) * 5;
    ESP_LOGI(TAG, "voltage_mv=%d estimated_percent=%d samples=%d", millivolts, percent, samples);
    return percent;
}
