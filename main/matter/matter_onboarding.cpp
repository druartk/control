#include "matter/matter_onboarding.h"

#include "esp_log.h"

namespace { constexpr const char *TAG = "CONTROL_MATTER"; }

bool MatterOnboarding::initializeControllerPlaceholder() {
    ESP_LOGI(TAG, "Interface Matter prête; pile contrôleur non intégrée pour ce jalon");
    return true;
}

void MatterOnboarding::requestEnvironmentSearch() {
    ESP_LOGW(TAG, "Recherche Matter demandée: commissioning non implémenté, aucune découverte simulée");
}

void MatterOnboarding::requestInstructions() {
    ESP_LOGI(TAG, "Affichage des instructions de préparation DIRIGERA demandé");
}

const char *MatterOnboarding::notImplementedMessage() const {
    return "Le commissioning Matter n'est pas encore implémenté.\n"
           "Dans l'application IKEA Home smart, DIRIGERA devra exposer\n"
           "le code d'association de son Matter Bridge.";
}

bool MatterOnboarding::commissionWithManualCode(const std::string &) {
    ESP_LOGW(TAG, "Code manuel refusé: commissioning non implémenté");
    return false;
}

bool MatterOnboarding::commissionWithQrPayload(const std::string &) {
    ESP_LOGW(TAG, "Payload QR refusé: commissioning non implémenté");
    return false;
}
