#pragma once

#include <string>
#include "esp_err.h"

struct MatterPairingInput {
    std::string raw_input;
    std::string normalized_digits;
    bool format_valid = false;
};

bool normalizeMatterCode(const std::string &input, std::string &normalized, std::string &error);
bool isMatterCodeFormatSupported(const std::string &normalized);
esp_err_t startMatterCommissioningWithCode(const MatterPairingInput &code);
