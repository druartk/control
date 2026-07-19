#pragma once

#include <string>

class MatterOnboarding {
public:
    bool initializeControllerPlaceholder();
    void requestEnvironmentSearch();
    void requestInstructions();
    bool commissioningImplemented() const { return false; }
    const char *notImplementedMessage() const;

    // Next milestone interfaces: no discovery or fake nodes are returned today.
    bool commissionWithManualCode(const std::string &pairingCode);
    bool commissionWithQrPayload(const std::string &qrPayload);
};
