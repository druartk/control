#pragma once

#include <string>

struct StoredSettings {
    std::string ssid;
    std::string password;
    bool initialSetupComplete = false;
    bool hasWifiCredentials() const { return !ssid.empty(); }
};

class SettingsStore {
public:
    bool initialize();
    StoredSettings load() const;
    bool saveWifi(const std::string &ssid, const std::string &password);
    bool setInitialSetupComplete(bool complete);
    bool eraseWifi();
    const char *statusText() const { return status_.c_str(); }

private:
    bool ready_ = false;
    std::string status_ = "Non initialisé";
};
