#include "matter/matter_pairing_code.h"

#include <cctype>
#include <vector>
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
#include <lib/core/CHIPError.h>
#include <setup_payload/SetupPayload.h>
#endif

namespace {
constexpr const char *TAG = "CONTROL_MATTER";

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
const char *commissioningFlowName(chip::CommissioningFlow flow) {
    switch (flow) {
        case chip::CommissioningFlow::kStandard: return "standard";
        case chip::CommissioningFlow::kUserActionRequired: return "user_action_required";
        case chip::CommissioningFlow::kCustom: return "custom";
    }
    return "unknown";
}

const char *discoveryCapabilitiesText(chip::SetupPayload &payload) {
    if (!payload.rendezvousInformation.HasValue()) return "manual_code_no_rendezvous";
    const uint8_t flags = payload.rendezvousInformation.Value().Raw();
    if (flags == 0) return "none";
    if (flags & static_cast<uint8_t>(chip::RendezvousInformationFlag::kOnNetwork)) return "on_network";
    if (flags & static_cast<uint8_t>(chip::RendezvousInformationFlag::kBLE)) return "ble";
    if (flags & static_cast<uint8_t>(chip::RendezvousInformationFlag::kSoftAP)) return "softap";
    return "other";
}

void logSetupPayloadDetails(chip::SetupPayload &payload) {
    const bool shortDiscriminator = payload.discriminator.IsShortDiscriminator();
    const uint16_t discriminator = shortDiscriminator
        ? payload.discriminator.GetShortValue()
        : payload.discriminator.GetLongValue();
    const uint8_t rendezvousFlags = payload.rendezvousInformation.HasValue()
        ? payload.rendezvousInformation.Value().Raw()
        : 0;
    ESP_LOGI(TAG, "setup_payload discriminator=%u type=%s",
             static_cast<unsigned>(discriminator), shortDiscriminator ? "short" : "long");
    ESP_LOGI(TAG, "setup_payload passcode_present=%s",
             chip::PayloadContents::IsValidSetupPIN(payload.setUpPINCode) ? "yes" : "no");
    ESP_LOGI(TAG, "setup_payload commissioning_flow=%s",
             commissioningFlowName(payload.commissioningFlow));
    ESP_LOGI(TAG, "setup_payload rendezvous_flags=0x%02x",
             static_cast<unsigned>(rendezvousFlags));
    ESP_LOGI(TAG, "setup_payload discovery_capabilities=%s",
             discoveryCapabilitiesText(payload));
}
#endif
}

bool isMatterCodeFormatSupported(const std::string &normalized) {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    std::vector<chip::SetupPayload> payloads;
    const CHIP_ERROR err = chip::SetupPayload::FromStringRepresentation(normalized.c_str(), payloads);
    return err == CHIP_NO_ERROR && !payloads.empty();
#else
    // Common Matter manual setup code length. The checksum/verhoeff validation
    // will be replaced by ESP-Matter's official parser in the commissioning milestone.
    return normalized.size() == 11;
#endif
}

bool normalizeMatterCode(const std::string &input, std::string &normalized, std::string &error) {
    normalized.clear();
    error.clear();
    for (unsigned char c : input) {
        if (c == ' ' || c == '-') continue;
        if (!std::isdigit(c)) {
            error = "Le code doit contenir uniquement des chiffres, espaces ou tirets.";
            ESP_LOGW(TAG, "code_format_invalid reason=non_digit");
            return false;
        }
        normalized.push_back(static_cast<char>(c));
    }
    if (normalized.empty()) {
        error = "Code vide.";
        ESP_LOGW(TAG, "code_format_invalid reason=empty");
        return false;
    }
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    ESP_LOGI(TAG, "setup_payload_parse_start code_len=%u", static_cast<unsigned>(normalized.size()));
    std::vector<chip::SetupPayload> payloads;
    const CHIP_ERROR parseErr = chip::SetupPayload::FromStringRepresentation(normalized.c_str(), payloads);
    if (parseErr != CHIP_NO_ERROR || payloads.empty()) {
        error = "Code Matter refusé par le parseur officiel.";
        ESP_LOGW(TAG, "setup_payload_parse_failed err=%s count=%u",
                 chip::ErrorStr(parseErr), static_cast<unsigned>(payloads.size()));
        return false;
    }
    ESP_LOGI(TAG, "setup_payload_parse_success");
    logSetupPayloadDetails(payloads.front());
    ESP_LOGI(TAG, "code_format_valid len=%u", static_cast<unsigned>(normalized.size()));
    return true;
#else
    if (!isMatterCodeFormatSupported(normalized)) {
        error = "Format attendu : code numérique Matter manuel à 11 chiffres.";
        ESP_LOGW(TAG, "code_format_invalid reason=unsupported_length len=%u",
                 static_cast<unsigned>(normalized.size()));
        return false;
    }
    ESP_LOGI(TAG, "code_format_valid len=%u", static_cast<unsigned>(normalized.size()));
    ESP_LOGW(TAG, "Format numérique accepté, checksum Matter non vérifié dans ce jalon");
    return true;
#endif
}

esp_err_t startMatterCommissioningWithCode(const MatterPairingInput &) {
    ESP_LOGW(TAG, "commissioning not implemented");
    return ESP_ERR_NOT_SUPPORTED;
}
