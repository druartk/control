#include "ui/ui_manager.h"
#include "board/board_config.h"

#include <algorithm>
#include <array>
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {
constexpr const char *TAG = "CONTROL_UI";
constexpr UiRect WIFI_BUTTON{55, 405, 520, 90};
constexpr UiRect PASSWORD_FIELD{28, 125, 650, 68};
constexpr UiRect PASSWORD_VISIBILITY_BUTTON{700, 125, 230, 68};
constexpr UiRect PASSWORD_KEYBOARD_REGION{24, 210, 912, 310};
constexpr UiRect BOTTOM_LEFT{30, 455, 205, 65};
constexpr UiRect BOTTOM_MIDDLE{255, 455, 270, 65};
constexpr UiRect BOTTOM_RIGHT{545, 455, 385, 65};
constexpr int NETWORKS_PER_PAGE = 5;
constexpr int NETWORK_TOP = 92;
constexpr int NETWORK_ROW_HEIGHT = 65;
constexpr UiRect MATTER_CODE_FIELD{35, 130, 590, 72};
constexpr UiRect MATTER_CODE_VALIDATE{665, 220, 260, 68};
constexpr UiRect MATTER_CODE_CLEAR{665, 130, 260, 68};
constexpr UiRect MATTER_CODE_CANCEL{665, 310, 260, 68};
constexpr int MATTER_KEY_X = 35;
constexpr int MATTER_KEY_Y = 220;
constexpr int MATTER_KEY_STEP_X = 195;
constexpr int MATTER_KEY_STEP_Y = 65;
constexpr int MATTER_KEY_W = 170;
constexpr int MATTER_KEY_H = 58;
constexpr int MATTER_DEVICES_PER_PAGE = 4;
constexpr int MATTER_DEVICE_TOP = 118;
constexpr int MATTER_DEVICE_ROW_HEIGHT = 66;

const std::array<const char *, 4> LOWER{{"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm.-_"}};
const std::array<const char *, 4> UPPER{{"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-_"}};
const std::array<const char *, 4> SYMBOLS{{"!@#$%^&*()", "[]{}<>+=?", "~`|\\/:;\"", "'_,.-"}};

const char *keyboardModeName(int mode) {
    switch (mode) {
        case 1: return "upper";
        case 2: return "symbols";
        default: return "lower";
    }
}

UiAction action(UiActionType type, int index = -1, char character = 0) {
    return UiAction{type, index, character};
}

const char *actionName(UiActionType type) {
    switch (type) {
        case UiActionType::NONE: return "NONE";
        case UiActionType::CONFIGURE_WIFI: return "WIFI_SETUP";
        case UiActionType::BACK: return "BACK";
        case UiActionType::REFRESH: return "REFRESH";
        case UiActionType::PAGE_PREVIOUS: return "PAGE_PREVIOUS";
        case UiActionType::PAGE_NEXT: return "PAGE_NEXT";
        case UiActionType::SELECT_NETWORK: return "SELECT_NETWORK";
        case UiActionType::KEY_CHARACTER: return "KEY_CHARACTER";
        case UiActionType::KEY_BACKSPACE: return "KEY_BACKSPACE";
        case UiActionType::KEY_CLEAR: return "KEY_CLEAR";
        case UiActionType::KEY_SHIFT: return "KEY_SHIFT";
        case UiActionType::KEY_SYMBOLS: return "KEY_SYMBOLS";
        case UiActionType::TOGGLE_PASSWORD: return "TOGGLE_PASSWORD";
        case UiActionType::CANCEL: return "CANCEL";
        case UiActionType::CONNECT: return "CONNECT";
        case UiActionType::RETRY: return "RETRY";
        case UiActionType::ERASE_CREDENTIALS: return "ERASE_CREDENTIALS";
        case UiActionType::CONTINUE_MATTER: return "CONTINUE_MATTER";
        case UiActionType::SEARCH_MATTER: return "SEARCH_MATTER";
        case UiActionType::SHOW_INSTRUCTIONS: return "SHOW_INSTRUCTIONS";
        case UiActionType::BACK_TO_WIFI: return "BACK_TO_WIFI";
        case UiActionType::SEARCH_IKEA: return "SEARCH_IKEA";
        case UiActionType::RETRY_IKEA: return "RETRY_IKEA";
        case UiActionType::MATTER_CODE_VALIDATE: return "MATTER_CODE_VALIDATE";
        case UiActionType::MATTER_CODE_EDIT: return "MATTER_CODE_EDIT";
        case UiActionType::MATTER_PAIR_INITIAL: return "MATTER_PAIR_INITIAL";
        case UiActionType::MATTER_PAIR_MULTI_ADMIN: return "MATTER_PAIR_MULTI_ADMIN";
        case UiActionType::MATTER_CONTINUE_ANYWAY: return "MATTER_CONTINUE_ANYWAY";
        case UiActionType::MATTER_USE_SAVED: return "MATTER_USE_SAVED";
        case UiActionType::MATTER_PAIR_CODE: return "MATTER_PAIR_CODE";
        case UiActionType::MATTER_NETWORK_DIAGNOSTIC: return "MATTER_NETWORK_DIAGNOSTIC";
        case UiActionType::MATTER_DEVICE_SELECT: return "MATTER_DEVICE_SELECT";
        case UiActionType::MATTER_ON: return "MATTER_ON";
        case UiActionType::MATTER_OFF: return "MATTER_OFF";
        case UiActionType::MATTER_FORGET: return "MATTER_FORGET";
        case UiActionType::MATTER_FORGET_CONFIRM: return "MATTER_FORGET_CONFIRM";
        case UiActionType::MATTER_RESTART: return "MATTER_RESTART";
    }
    return "UNKNOWN";
}

}

bool UiManager::render(const UiViewModel &m) {
    if (!display_.available()) return false;
    display_.beginFrame();
    switch (m.state) {
        case AppState::BOOT:
            display_.title("Control");
            display_.text(40, 155, "Initialisation du contrôleur...");
            display_.text(40, 225, "Diagnostic matériel en cours");
            break;
        case AppState::HARDWARE_TEST: renderDiagnostics(m); break;
        case AppState::WIFI_SCANNING:
            display_.title("Recherche Wi-Fi");
            display_.text(55, 175, "Analyse des réseaux en cours...");
            display_.text(55, 245, "Aucune animation: attente du résultat.");
            display_.button(BOTTOM_LEFT, "Retour");
            break;
        case AppState::WIFI_NETWORK_SELECTION: renderNetworks(m); break;
        case AppState::WIFI_PASSWORD_ENTRY: renderPassword(m); break;
        case AppState::WIFI_CONNECTING:
            display_.title("Connexion Wi-Fi");
            display_.text(45, 155, "Réseau : " + m.selectedSsid);
            display_.text(45, 230, "Connexion en cours  •••");
            display_.text(45, 300, "Délai maximal : 20 secondes");
            display_.button(BOTTOM_LEFT, "Annuler");
            break;
        case AppState::WIFI_CONNECTED:
            display_.title("Wi-Fi connecté");
                display_.multiline(45, 120,
                "SSID : " + m.connection.ssid + "\nIPv4 : " + m.connection.ipv4 +
                "\nGW : " + m.connection.gateway + "\nDNS : " + m.connection.dns +
                "\nRSSI : " + std::to_string(m.connection.rssi) + " dBm\nMAC : " + m.connection.mac, 52);
            display_.button(BOTTOM_RIGHT, "Matter / IKEA", true);
            break;
        case AppState::WIFI_CONNECTION_FAILED:
            display_.title("Échec de connexion Wi-Fi");
            display_.text(40, 135, m.failureReason);
            display_.button({30, 250, 280, 80}, "Réessayer");
            display_.button({335, 250, 390, 80}, "Autre réseau");
            display_.button({30, 360, 430, 80}, "Effacer les identifiants");
            break;
        case AppState::IKEA_DISCOVERY:
            display_.title("Recherche IKEA DIRIGERA");
            display_.text(45, 165, "Recherche sur le réseau local...");
            display_.text(45, 225, "IP ESP32 : " + m.connection.ipv4 + "  GW : " + m.connection.gateway);
            display_.text(45, 285, "mDNS/DNS-SD, hostnames locaux, aucun résultat simulé.");
            display_.button(BOTTOM_LEFT, "Annuler");
            display_.button(BOTTOM_RIGHT, "Retour Wi-Fi");
            break;
        case AppState::IKEA_FOUND:
            if (m.ikea.classification == IkeaDiscoveryClassification::KNOWN_IP_REACHABLE_UNCONFIRMED) {
                display_.title("Passerelle joignable");
                display_.multiline(35, 108,
                    "IP : " + m.ikea.ip + "\n"
                    "Répond au ping. Identité IKEA non confirmée.\n"
                    "Aucun service Matter annoncé.\n\n"
                    "IKEA Home smart :\n"
                    "Intégrations > Matter Bridge\n"
                    "Créer / afficher le code Matter.", 42);
                display_.button({35, 405, 390, 70}, "Entrer code Matter", true);
            } else {
                display_.title(m.ikea.classification == IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT
                    ? "Centrale IKEA trouvée"
                    : (m.ikea.classification == IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED
                        ? "Équipement Matter détecté" : "Services locaux détectés"));
                display_.multiline(35, 125,
                    (m.ikea.classification == IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT
                        ? std::string("DIRIGERA/IKEA confirmé.\n")
                        : (m.ikea.classification == IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED
                            ? std::string("Identité IKEA non confirmée.\n")
                            : std::string("Aucun service IKEA/Matter confirmé.\n"))) +
                    "Hôte : " + m.ikea.hostname + "\nIP : " + m.ikea.ip +
                    "\nService : " + m.ikea.serviceType + "\nPort : " + std::to_string(m.ikea.port) +
                    "\nClasse : " + m.ikea.summary, 48);
                display_.button({35, 405, 390, 70}, "Continuer Matter", true);
            }
            display_.button({455, 405, 220, 70}, "Réessayer");
            display_.button({705, 405, 220, 70}, "Retour");
            break;
        case AppState::IKEA_NOT_FOUND:
            display_.title("Centrale IKEA non trouvée");
            display_.multiline(30, 110,
                "Vérifier : même Wi-Fi/LAN ; DIRIGERA allumée ;\n"
                "pas de réseau invité ; isolation client désactivée ;\n"
                "Matter Bridge activé dans IKEA Home smart si besoin.\n\n"
                "ESP32 : " + m.connection.ipv4 + "\nGW : " + m.connection.gateway +
                "  Mask : " + m.connection.netmask, 48);
            display_.button({35, 405, 390, 70}, "Réessayer");
            display_.button({455, 405, 470, 70}, "Retour Wi-Fi");
            break;
        case AppState::IKEA_DISCOVERY_FAILED:
            display_.title("Découverte IKEA indisponible");
            display_.multiline(35, 130, m.ikea.summary, 58);
            display_.button({35, 405, 390, 70}, "Réessayer");
            display_.button({455, 405, 470, 70}, "Retour Wi-Fi");
            break;
        case AppState::MATTER_INTRO:
            display_.title("Matter / IKEA");
            if (m.matterSavedState == MatterSavedState::Valid) {
                display_.multiline(35, 95,
                    std::string("Appairage enregistré\nNoeud distant : ") +
                    std::to_string(static_cast<unsigned long long>(m.matterSavedNode.node_id)) +
                    "\nFabric : " + std::to_string(m.matterSavedNode.fabric_index) +
                    "\nCache appareils : " + (m.matterPost.devices.empty()
                        ? std::string("Aucun élément connu")
                        : std::to_string(m.matterPost.devices.size()) + " élément(s)") +
                    (m.matterSavedNode.dev_insecure_mode || m.matterSavedNode.attestation_bypassed
                        ? "\nMode dev: attestation non vérifiée" : "\nAttestation vérifiée"), 42);
                display_.button({35, 255, 280, 58}, "Utiliser Matter", true);
                display_.button({340, 255, 280, 58}, "Rafraîchir appareils");
                display_.button({645, 255, 280, 58}, "Réappairer");
                display_.button({35, 335, 280, 58}, "Oublier Matter");
                display_.button({340, 335, 280, 58}, "Diagnostic");
                display_.button({645, 335, 280, 58}, "Retour");
            } else {
                display_.multiline(35, 110,
                    "Aucun appairage Matter valide\n\n"
                    "Appairez DIRIGERA avec un code Matter\n"
                    "ou lancez le diagnostic réseau.", 52);
                display_.button({35, 245, 390, 64}, "Appairer avec code", true);
                display_.button({455, 245, 390, 64}, "Diagnostic réseau");
                display_.button({455, 330, 220, 64}, "Retour");
            }
            break;
        case AppState::MATTER_NO_SAVED_NODE:
            display_.title("Matter / IKEA");
            display_.multiline(35, 130,
                "Aucun appareil Matter enregistré.\n\n"
                "Appairez DIRIGERA avec le code Matter\n"
                "fourni par IKEA Home smart.", 58);
            display_.button({35, 405, 390, 70}, "Appairer avec code", true);
            display_.button({455, 405, 220, 70}, "Retour");
            break;
        case AppState::MATTER_DEVICE_DISCOVERY:
            display_.title("Appareils Matter");
            display_.multiline(35, 140,
                "Connexion au noeud Matter enregistré...\n"
                "Lecture des endpoints et clusters.\n\n"
                "Aucun rafraîchissement périodique.", 58);
            break;
        case AppState::MATTER_DEVICE_LIST: {
            display_.title("Appareils Matter");
            if (m.matterSavedNode.dev_insecure_mode || m.matterSavedNode.attestation_bypassed) {
                display_.text(35, 82, "Mode dev: attestation non vérifiée");
            }
            const auto &devices = m.matterPost.devices;
            const size_t start = m.matterDevicePage * MATTER_DEVICES_PER_PAGE;
            for (size_t row = 0; row < MATTER_DEVICES_PER_PAGE && start + row < devices.size(); ++row) {
                const auto &device = devices[start + row];
                const int y = MATTER_DEVICE_TOP + static_cast<int>(row) * MATTER_DEVICE_ROW_HEIGHT;
                const bool selected = static_cast<int>(start + row) == m.selectedMatterDevice;
                display_.button({30, y, 520, 56}, device.name.empty()
                    ? ("Endpoint " + std::to_string(device.endpoint_id)) : device.name, selected);
                std::string detail = device.type_label.empty() ? "Matter" : device.type_label;
                if (device.has_onoff) {
                    detail += device.onoff_state_known
                        ? (device.onoff_state ? " · Allumée" : " · Éteinte")
                        : " · État ?";
                }
                display_.text(585, y + 38, detail);
            }
            if (devices.empty()) display_.text(45, 185, "Aucun élément connu. Appuyer sur Rafraîchir.");
            display_.button({35, 385, 150, 56}, "On");
            display_.button({205, 385, 150, 56}, "Off");
            display_.button({375, 385, 190, 56}, "Rafraîchir");
            display_.button({35, 455, 150, 56}, "Préc.");
            display_.button({205, 455, 150, 56}, "Suiv.");
            display_.button({375, 455, 190, 56}, "Retour");
            break;
        }
        case AppState::MATTER_COMMAND_IN_PROGRESS:
            display_.title("Commande Matter");
            display_.multiline(35, 150,
                "Commande en cours...\n"
                "Endpoint : " + std::to_string(m.matterPost.activeEndpoint), 64);
            break;
        case AppState::MATTER_COMMAND_SUCCESS:
            display_.title("Commande envoyée");
            display_.multiline(35, 140,
                (m.matterPost.commandStateKnown
                    ? ("État relu : " + std::string(m.matterPost.commandState ? "ON" : "OFF"))
                    : "État non relu"), 70);
            display_.button({35, 405, 390, 70}, "Retour liste", true);
            break;
        case AppState::MATTER_COMMAND_FAILED:
            display_.title("Commande échouée");
            display_.multiline(35, 140, m.matterPost.errorText, 64);
            display_.button({35, 405, 260, 70}, "Réessayer", true);
            display_.button({325, 405, 250, 70}, "Retour");
            break;
        case AppState::MATTER_CONNECTION_FAILED:
            display_.title("Connexion Matter impossible");
            display_.multiline(35, 120,
                "Hub non joignable ou fabric invalide", 64);
            display_.button({35, 405, 260, 70}, "Réessayer", true);
            display_.button({325, 405, 250, 70}, "Appairer");
            display_.button({610, 405, 315, 70}, "Retour");
            break;
        case AppState::MATTER_DISCOVERY_FAILED:
            display_.title("Lecture appareils échouée");
            display_.multiline(35, 135,
                m.matterPost.errorText.empty() ? "Lecture appareils échouée" : m.matterPost.errorText, 62);
            display_.button({35, 405, 260, 70}, "Réessayer", true);
            display_.button({325, 405, 250, 70}, "Retour");
            break;
        case AppState::MATTER_INIT_FAILED:
            display_.title("Matter indisponible");
            display_.multiline(35, 130,
                m.matterPost.errorText.empty()
                    ? "Mémoire interne insuffisante"
                    : m.matterPost.errorText, 62);
            display_.button({35, 405, 260, 70}, "Réessayer", true);
            display_.button({325, 405, 250, 70}, "Reset local");
            display_.button({610, 405, 315, 70}, "Retour");
            break;
        case AppState::MATTER_SAVED_INVALID:
            display_.title("Matter enregistré invalide");
            display_.multiline(35, 105,
                (m.matterPost.errorText.empty()
                    ? "Réappairage nécessaire\n\n"
                      "Le contrôleur Matter ne peut pas\n"
                      "réouvrir sa fabric locale."
                    : m.matterPost.errorText + "\n\nRéappairage nécessaire"), 45);
            display_.button({35, 405, 260, 70}, "Réappairer", true);
            display_.button({325, 405, 250, 70}, "Reset local");
            display_.button({610, 405, 315, 70}, "Retour");
            break;
        case AppState::MATTER_FORGET_CONFIRM:
            display_.title("Reset Matter local complet");
            display_.multiline(35, 140,
                "Effacer le stockage Matter local complet ?\n\n"
                "Wi-Fi conservé\n"
                "chip-factory conservé", 60);
            display_.button({35, 405, 390, 70}, "Reset Matter local complet", true);
            display_.button({455, 405, 220, 70}, "Retour");
            break;
        case AppState::MATTER_FORGOT:
            display_.title("Matter oublié");
            display_.multiline(35, 135,
                "Stockage Matter local réinitialisé.\n"
                "Wi-Fi conservé.\n\n"
                "Redémarrage recommandé avant réappairage.", 58);
            display_.button({35, 405, 390, 70}, "Redémarrer", true);
            display_.button({455, 405, 220, 70}, "Retour");
            break;
        case AppState::MATTER_CODE_ENTRY:
            renderMatterCodeEntry(m);
            break;
        case AppState::MATTER_CODE_CONFIRM:
            display_.title("Code Matter prêt");
            display_.multiline(35, 120,
                "Code saisi : " + formattedMatterCode(m.matterCodeRaw) + "\n"
                "Format numérique accepté.\n"
                "Choisir le type d'appairage ensuite.\n\n"
                "Si DIRIGERA est déjà dans Google Home,\n"
                "utiliser un nouveau code multi-admin.", 50);
            display_.button({35, 405, 260, 70}, "Continuer", true);
            display_.button({325, 405, 250, 70}, "Modifier");
            display_.button({610, 405, 315, 70}, "Retour");
            break;
        case AppState::MATTER_PAIRING_MODE:
            display_.title("Type d'appairage");
            display_.multiline(35, 100,
                "Premier appairage : hub non appairé/reset.\n\n"
                "Multi-admin : hub déjà appairé à Google Home\n"
                "ou IKEA. Générer un nouveau code de partage.", 48);
            display_.button({35, 330, 390, 70}, "Premier appairage", true);
            display_.button({455, 330, 390, 70}, "Ajouter multi-admin");
            display_.button({610, 430, 315, 70}, "Retour");
            break;
        case AppState::MATTER_MDNS_PRESCAN_EMPTY:
            display_.title("Aucun hub Matter détecté");
            display_.multiline(35, 118,
                "Aucun service _matterc/_matterd détecté.\n\n"
                "Vérifier mode appairage / multi-admin\n"
                "et générer un nouveau code de partage.", 50);
            display_.button({35, 405, 260, 70}, "Réessayer", true);
            display_.button({325, 405, 250, 70}, "Continuer");
            display_.button({610, 405, 315, 70}, "Retour");
            break;
        case AppState::MATTER_CODE_INVALID:
            display_.title("Code Matter invalide");
            display_.multiline(35, 130,
                m.matterCodeError + "\n\n"
                "Utilisez le code numérique fourni par l'app IKEA Home smart.\n"
                "Espaces et tirets sont acceptés.", 58);
            display_.button({35, 405, 390, 70}, "Modifier", true);
            display_.button({455, 405, 470, 70}, "Retour Wi-Fi");
            break;
        case AppState::MATTER_COMMISSIONING_PLACEHOLDER:
            display_.title("Appairage Matter");
            display_.multiline(35, 125,
                "Commissioning désactivé dans cette build.\n\n"
                "Wi-Fi connecté\n"
                "Passerelle joignable\n"
                "Code Matter disponible\n\n"
                "Activer ESP-Matter sera le prochain jalon.", 52);
            display_.button(BOTTOM_LEFT, "Retour");
            break;
        case AppState::MATTER_COMMISSIONING:
            display_.title("Appairage Matter");
            display_.multiline(35, 135,
                "Appairage en cours...\n"
                "Gardez IKEA Home smart ouvert si nécessaire.\n\n"
                "Aucun rafraîchissement pendant l'opération.", 58);
            break;
        case AppState::MATTER_COMMISSIONING_SUCCESS:
            display_.title("Appairage réussi");
            if (m.matterAttestationBypassed || m.matterDevInsecureMode || !m.matterAttestationVerified) {
                display_.multiline(35, 120,
                    "Appairage réussi\n"
                    "Attestation non vérifiée\n"
                    "Mode dev uniquement\n\n"
                    "Utiliser Matter pour lister les appareils.", 54);
            } else {
                display_.multiline(35, 125,
                    "Attestation vérifiée\n"
                    "Fabric : à confirmer\n"
                    "Utiliser Matter pour lister les appareils.", 58);
            }
            display_.button(BOTTOM_LEFT, "Utiliser Matter", true);
            break;
        case AppState::MATTER_COMMISSIONING_FAILED:
            if (m.matterFailureReason == "pairing_code_discovery_filter_mismatch_or_dns_sd_failure") {
                display_.title("Découverte Matter échouée");
                display_.multiline(35, 120,
                    "Le code est valide, mais l'ESP32 ne voit pas le hub\n\n"
                    "Vérifier le mode multi-admin et les services mDNS.", 52);
                display_.button({35, 405, 260, 70}, "Réessayer", true);
                display_.button({325, 405, 250, 70}, "Diagnostic mDNS");
                display_.button({610, 405, 315, 70}, "Retour");
                break;
            }
            if (m.matterFailureReason == "paa_not_found") {
                display_.title("Échec attestation");
                display_.multiline(35, 112,
                    "PAA non trouvé\n"
                    "Trust store Matter manquant\n\n"
                    "Ajouter un trust store Matter/DCL\n"
                    "ou utiliser le mode dev explicitement.", 48);
            } else if (m.matterFailureReason == "send_noc_fabric_conflict") {
                display_.title("Conflit fabric Matter");
                display_.multiline(35, 130,
                    "Matter déjà appairé ou fabric en conflit\n"
                    "Oublier Matter côté IKEA puis réessayer", 58);
            } else {
                display_.title("Échec appairage");
                display_.multiline(35, 112,
                    m.matterCodeError + "\n\n"
                    "Vérifier le code Matter ; générer un nouveau code\n"
                    "dans IKEA Home smart ; vérifier le même réseau ;\n"
                    "vérifier que le hub est en mode appairage Matter.", 46);
            }
            display_.button({35, 405, 260, 70}, "Réessayer", true);
            display_.button({325, 405, 250, 70}, "Modifier");
            display_.button({610, 405, 315, 70}, "Retour");
            break;
        case AppState::MATTER_NOT_IMPLEMENTED:
            display_.title("Matter — prochain jalon");
            display_.multiline(30, 125,
                "Appairage Matter non encore implémenté.\n"
                "Code Matter requis depuis l'app IKEA.\n\n"
                "Matter ne s'utilise pas comme HTTP.\n"
                "Il faut un code d'appairage Matter.\n"
                "Le contrôleur créera ensuite une fabric sécurisée.", 58);
            display_.button(BOTTOM_LEFT, "Retour");
            break;
    }
    return display_.commitFull(appStateName(m.state));
}

void UiManager::renderDiagnostics(const UiViewModel &m) {
    display_.title("Control");
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "button WIFI_SETUP rect x=%d y=%d w=%d h=%d",
             WIFI_BUTTON.x, WIFI_BUTTON.y, WIFI_BUTTON.width, WIFI_BUTTON.height);
    display_.multiline(35, 120,
        "Carte : " CONTROL_BOARD_NAME " " CONTROL_BOARD_REVISION " — PCB "
        CONTROL_BOARD_PCB_DATE "\nÉcran : " + m.displayStatus +
        "\nTactile : " + m.touchStatus + "\nWi-Fi : " + m.wifiStatus +
        "\nFirmware : " CONTROL_FW_VERSION "  IDF " + std::string(app->idf_ver), 55);
    display_.button(WIFI_BUTTON, "Configurer le Wi-Fi", true);
}

void UiManager::renderNetworks(const UiViewModel &m) {
    display_.title("Réseaux Wi-Fi");
    const size_t start = m.networkPage * NETWORKS_PER_PAGE;
    for (size_t row = 0; row < NETWORKS_PER_PAGE && start + row < m.networks.size(); ++row) {
        const auto &network = m.networks[start + row];
        UiRect rect{25, NETWORK_TOP + static_cast<int>(row) * NETWORK_ROW_HEIGHT, 910, 57};
        display_.rectangle(rect);
        const std::string lock = network.hidden ? "[masqué]" :
            (network.auth == WIFI_AUTH_OPEN ? "[ouvert]" : "[verrouillé]");
        display_.text(40, rect.y + 43, network.ssid + "  " + signalBars(network.rssi) + "  " + lock);
    }
    if (m.networks.empty()) display_.text(45, 180, "Aucun réseau détecté.");
    display_.button(BOTTOM_LEFT, "Retour");
    display_.button(BOTTOM_MIDDLE, "Rafraîchir");
    const size_t pages = std::max<size_t>(1, (m.networks.size() + NETWORKS_PER_PAGE - 1) / NETWORKS_PER_PAGE);
    display_.button({545, 455, 180, 65}, "Précédent");
    display_.button({745, 455, 185, 65}, "Suivant");
    display_.text(780, 445, std::to_string(m.networkPage + 1) + "/" + std::to_string(pages));
}

void UiManager::renderPassword(const UiViewModel &m) {
    display_.title(m.openNetworkConfirmation ? "Confirmer le réseau ouvert" : "Mot de passe Wi-Fi");
    display_.text(30, 110, "SSID : " + m.selectedSsid);
    if (m.openNetworkConfirmation) {
        display_.multiline(35, 190,
            "Ce réseau n'utilise aucun chiffrement.\nConfirmez explicitement la connexion.", 70);
        display_.button({250, 350, 300, 80}, "Annuler");
        display_.button({580, 350, 330, 80}, "Se connecter", true);
        return;
    }
    ESP_LOGI(TAG, "password_field rect x=%d y=%d w=%d h=%d",
             PASSWORD_FIELD.x, PASSWORD_FIELD.y, PASSWORD_FIELD.width, PASSWORD_FIELD.height);
    ESP_LOGI(TAG, "keyboard rect x=%d y=%d w=%d h=%d",
             PASSWORD_KEYBOARD_REGION.x, PASSWORD_KEYBOARD_REGION.y,
             PASSWORD_KEYBOARD_REGION.width, PASSWORD_KEYBOARD_REGION.height);
    renderPasswordField(m);
    renderPasswordVisibilityButton(m);
    renderKeyboard(m);
}

void UiManager::renderMatterCodeEntry(const UiViewModel &m) {
    display_.title("Code Matter");
    display_.multiline(35, 92,
        "Code depuis l'app IKEA Home smart.\n"
        "RAM uniquement. Non journalisé.", 34);
    ESP_LOGI(TAG, "matter_code_field rect x=%d y=%d w=%d h=%d",
             MATTER_CODE_FIELD.x, MATTER_CODE_FIELD.y,
             MATTER_CODE_FIELD.width, MATTER_CODE_FIELD.height);
    renderMatterCodeField(m);
    display_.button(MATTER_CODE_VALIDATE, "Valider", true);
    display_.button(MATTER_CODE_CLEAR, "Effacer");
    display_.button(MATTER_CODE_CANCEL, "Annuler");

    const std::array<const char *, 4> rows{{"123", "456", "789", "C0<"}};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 3; ++col) {
            const char key = rows[row][col];
            UiRect rect{MATTER_KEY_X + col * MATTER_KEY_STEP_X,
                        MATTER_KEY_Y + row * MATTER_KEY_STEP_Y,
                        MATTER_KEY_W,
                        MATTER_KEY_H};
            const char *label = key == '<' ? "<-" : (key == 'C' ? "Eff." : nullptr);
            display_.button(rect, label ? std::string(label) : std::string(1, key));
        }
    }
}

void UiManager::renderMatterCodeField(const UiViewModel &m) {
    display_.fillRectangle(MATTER_CODE_FIELD, 0xFF);
    display_.rectangle(MATTER_CODE_FIELD);
    const std::string code = formattedMatterCode(m.matterCodeRaw);
    display_.text(55, 178, code.empty() ? "____-___-____" : code);
}

void UiManager::renderPasswordField(const UiViewModel &m) {
    display_.fillRectangle(PASSWORD_FIELD, 0xFF);
    display_.rectangle(PASSWORD_FIELD);
    display_.text(42, 176, m.passwordVisible ? m.password : maskedPassword(m.password.size()));
}

void UiManager::renderPasswordVisibilityButton(const UiViewModel &m) {
    display_.fillRectangle(PASSWORD_VISIBILITY_BUTTON, 0xFF);
    display_.button(PASSWORD_VISIBILITY_BUTTON, m.passwordVisible ? "Masquer" : "Afficher");
}

bool UiManager::refreshPasswordField(const UiViewModel &m) {
    if (m.state != AppState::WIFI_PASSWORD_ENTRY || m.openNetworkConfirmation) return false;
    ESP_LOGI(TAG, "password_field_update len=%u visible=%s",
             static_cast<unsigned>(m.password.size()), m.passwordVisible ? "yes" : "no");
    renderPasswordField(m);
#if CONFIG_CONTROL_PASSWORD_FIELD_PARTIAL_REFRESH
    const esp_err_t err = display_.commitRegion(PASSWORD_FIELD, "password_field");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "password_field partial refresh failed err=%s; no full refresh fallback",
                 esp_err_to_name(err));
        return false;
    }
    return true;
#else
    ESP_LOGW(TAG, "password_field partial refresh disabled; no full refresh fallback");
    return false;
#endif
}

bool UiManager::refreshPasswordVisibilityButton(const UiViewModel &m) {
    if (m.state != AppState::WIFI_PASSWORD_ENTRY || m.openNetworkConfirmation) return false;
    renderPasswordVisibilityButton(m);
#if CONFIG_CONTROL_PASSWORD_FIELD_PARTIAL_REFRESH
    const esp_err_t err = display_.commitRegion(PASSWORD_VISIBILITY_BUTTON, "password_visibility");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "password visibility partial refresh failed err=%s; no full refresh fallback",
                 esp_err_to_name(err));
        return false;
    }
    return true;
#else
    ESP_LOGW(TAG, "password visibility partial refresh disabled; no full refresh fallback");
    return false;
#endif
}

void UiManager::renderKeyboard(const UiViewModel &m) {
    display_.fillRectangle(PASSWORD_KEYBOARD_REGION, 0xFF);
    const auto &rows = m.keyboardMode == 2 ? SYMBOLS : (m.keyboardMode == 1 ? UPPER : LOWER);
    for (int row = 0; row < 4; ++row) {
        const std::string keys = rows[row];
        const int keyWidth = 760 / static_cast<int>(keys.size());
        for (size_t col = 0; col < keys.size(); ++col) {
            UiRect key{25 + static_cast<int>(col) * keyWidth, 210 + row * 58, keyWidth - 5, 50};
            display_.button(key, std::string(1, keys[col]));
        }
    }
    display_.button({805, 210, 130, 50}, "Retour");
    display_.button({805, 268, 130, 50}, m.keyboardMode == 1 ? "abc" : "ABC", m.keyboardMode == 1);
    display_.button({805, 326, 130, 50}, m.keyboardMode == 2 ? "ABC" : "?123", m.keyboardMode == 2);
    display_.button({805, 384, 130, 50}, "Effacer");
    display_.button({25, 455, 210, 65}, "Annuler");
    display_.button({255, 455, 470, 65}, "Espace");
    display_.button({745, 455, 190, 65}, "Connecter", true);
}

bool UiManager::refreshKeyboard(const UiViewModel &m) {
    if (m.state != AppState::WIFI_PASSWORD_ENTRY || m.openNetworkConfirmation) return false;
    ESP_LOGI(TAG, "keyboard_region_update mode=%s", keyboardModeName(m.keyboardMode));
    renderKeyboard(m);
#if CONFIG_CONTROL_PASSWORD_KEYBOARD_PARTIAL_REFRESH
    const esp_err_t err = display_.commitRegion(PASSWORD_KEYBOARD_REGION, "keyboard");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "keyboard partial refresh failed err=%s; no full refresh fallback",
                 esp_err_to_name(err));
        return false;
    }
    return true;
#else
    ESP_LOGW(TAG, "keyboard partial refresh disabled; visual keyboard update skipped");
    return false;
#endif
}

bool UiManager::refreshMatterCodeField(const UiViewModel &m) {
    if (m.state != AppState::MATTER_CODE_ENTRY) return false;
    renderMatterCodeField(m);
#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
    const esp_err_t err = display_.commitRegion(MATTER_CODE_FIELD, "matter_code_field");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "matter code field partial refresh failed err=%s; no full refresh fallback",
                 esp_err_to_name(err));
        return false;
    }
    return true;
#else
    ESP_LOGW(TAG, "matter code field refresh skipped; partial refresh disabled");
    return false;
#endif
}

UiAction UiManager::hitTest(const UiViewModel &m, int x, int y) const {
    ESP_LOGI(TAG, "touch PRESS at x=%d y=%d", x, y);
    ESP_LOGI(TAG, "current_state=%s", appStateName(m.state));
    if (m.state == AppState::HARDWARE_TEST) {
        ESP_LOGI(TAG, "button WIFI_SETUP rect x=%d y=%d w=%d h=%d contains=%s",
                 WIFI_BUTTON.x, WIFI_BUTTON.y, WIFI_BUTTON.width, WIFI_BUTTON.height,
                 WIFI_BUTTON.contains(x, y) ? "yes" : "no");
    } else if (m.state == AppState::WIFI_SCANNING) {
        ESP_LOGI(TAG, "button BACK rect x=%d y=%d w=%d h=%d contains=%s",
                 BOTTOM_LEFT.x, BOTTOM_LEFT.y, BOTTOM_LEFT.width, BOTTOM_LEFT.height,
                 BOTTOM_LEFT.contains(x, y) ? "yes" : "no");
    } else if (m.state == AppState::WIFI_NETWORK_SELECTION) {
        ESP_LOGI(TAG, "button BACK rect x=%d y=%d w=%d h=%d contains=%s",
                 BOTTOM_LEFT.x, BOTTOM_LEFT.y, BOTTOM_LEFT.width, BOTTOM_LEFT.height,
                 BOTTOM_LEFT.contains(x, y) ? "yes" : "no");
        ESP_LOGI(TAG, "button REFRESH rect x=%d y=%d w=%d h=%d contains=%s",
                 BOTTOM_MIDDLE.x, BOTTOM_MIDDLE.y, BOTTOM_MIDDLE.width, BOTTOM_MIDDLE.height,
                 BOTTOM_MIDDLE.contains(x, y) ? "yes" : "no");
    } else if (m.state == AppState::MATTER_CODE_ENTRY) {
        ESP_LOGI(TAG, "matter keypad area x=%d y=%d w=%d h=%d",
                 MATTER_KEY_X, MATTER_KEY_Y,
                 MATTER_KEY_STEP_X * 2 + MATTER_KEY_W,
                 MATTER_KEY_STEP_Y * 3 + MATTER_KEY_H);
        ESP_LOGI(TAG, "matter buttons validate=%s clear=%s cancel=%s",
                 MATTER_CODE_VALIDATE.contains(x, y) ? "yes" : "no",
                 MATTER_CODE_CLEAR.contains(x, y) ? "yes" : "no",
                 MATTER_CODE_CANCEL.contains(x, y) ? "yes" : "no");
    }
    const UiAction result = hitTestInternal(m, x, y);
    if (result.type == UiActionType::NONE) {
        ESP_LOGI(TAG, "no hit target");
    } else {
        ESP_LOGI(TAG, "hit button %s", actionName(result.type));
    }
    return result;
}

UiAction UiManager::hitTestInternal(const UiViewModel &m, int x, int y) const {
    switch (m.state) {
        case AppState::HARDWARE_TEST:
            if (WIFI_BUTTON.contains(x, y)) return action(UiActionType::CONFIGURE_WIFI);
            break;
        case AppState::WIFI_SCANNING:
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::WIFI_NETWORK_SELECTION: {
            const int row = (y - NETWORK_TOP) / NETWORK_ROW_HEIGHT;
            if (x >= 25 && x < 935 && y >= NETWORK_TOP && row >= 0 && row < NETWORKS_PER_PAGE &&
                y < NETWORK_TOP + row * NETWORK_ROW_HEIGHT + 57) {
                return action(UiActionType::SELECT_NETWORK,
                    static_cast<int>(m.networkPage * NETWORKS_PER_PAGE + row));
            }
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::BACK);
            if (BOTTOM_MIDDLE.contains(x, y)) return action(UiActionType::REFRESH);
            if (UiRect{545, 455, 180, 65}.contains(x, y)) return action(UiActionType::PAGE_PREVIOUS);
            if (UiRect{745, 455, 185, 65}.contains(x, y)) return action(UiActionType::PAGE_NEXT);
            break;
        }
        case AppState::WIFI_PASSWORD_ENTRY:
            if (m.openNetworkConfirmation) {
                if (UiRect{250, 350, 300, 80}.contains(x, y)) return action(UiActionType::CANCEL);
                if (UiRect{580, 350, 330, 80}.contains(x, y)) return action(UiActionType::CONNECT);
                break;
            }
            if (UiRect{700, 125, 230, 68}.contains(x, y)) return action(UiActionType::TOGGLE_PASSWORD);
            if (UiRect{805, 210, 130, 50}.contains(x, y)) return action(UiActionType::KEY_BACKSPACE);
            if (UiRect{805, 268, 130, 50}.contains(x, y)) return action(UiActionType::KEY_SHIFT);
            if (UiRect{805, 326, 130, 50}.contains(x, y)) return action(UiActionType::KEY_SYMBOLS);
            if (UiRect{805, 384, 130, 50}.contains(x, y)) return action(UiActionType::KEY_CLEAR);
            if (UiRect{25, 455, 210, 65}.contains(x, y)) return action(UiActionType::CANCEL);
            if (UiRect{255, 455, 470, 65}.contains(x, y)) return action(UiActionType::KEY_CHARACTER, -1, ' ');
            if (UiRect{745, 455, 190, 65}.contains(x, y)) return action(UiActionType::CONNECT);
            if (x >= 25 && x < 785 && y >= 210 && y < 442) {
                const int row = (y - 210) / 58;
                const auto &rows = m.keyboardMode == 2 ? SYMBOLS : (m.keyboardMode == 1 ? UPPER : LOWER);
                if (row >= 0 && row < 4) {
                    const std::string keys = rows[row];
                    const int keyWidth = 760 / static_cast<int>(keys.size());
                    const int col = (x - 25) / keyWidth;
                    if (col >= 0 && col < static_cast<int>(keys.size()))
                        return action(UiActionType::KEY_CHARACTER, -1, keys[col]);
                }
            }
            break;
        case AppState::WIFI_CONNECTING:
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::CANCEL);
            break;
        case AppState::WIFI_CONNECTED:
            if (BOTTOM_RIGHT.contains(x, y)) return action(UiActionType::CONTINUE_MATTER);
            break;
        case AppState::WIFI_CONNECTION_FAILED:
            if (UiRect{30, 250, 280, 80}.contains(x, y)) return action(UiActionType::RETRY);
            if (UiRect{335, 250, 390, 80}.contains(x, y)) return action(UiActionType::CONFIGURE_WIFI);
            if (UiRect{30, 360, 430, 80}.contains(x, y)) return action(UiActionType::ERASE_CREDENTIALS);
            break;
        case AppState::IKEA_DISCOVERY:
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            if (BOTTOM_RIGHT.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::IKEA_FOUND:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::CONTINUE_MATTER);
            if (UiRect{455, 405, 220, 70}.contains(x, y)) return action(UiActionType::RETRY_IKEA);
            if (UiRect{705, 405, 220, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::IKEA_NOT_FOUND:
        case AppState::IKEA_DISCOVERY_FAILED:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::RETRY_IKEA);
            if (UiRect{455, 405, 470, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_INTRO:
            if (m.matterSavedState == MatterSavedState::Valid) {
                if (UiRect{35, 255, 280, 58}.contains(x, y)) return action(UiActionType::MATTER_USE_SAVED);
                if (UiRect{340, 255, 280, 58}.contains(x, y)) return action(UiActionType::REFRESH);
                if (UiRect{645, 255, 280, 58}.contains(x, y)) return action(UiActionType::MATTER_PAIR_CODE);
                if (UiRect{35, 335, 280, 58}.contains(x, y)) return action(UiActionType::MATTER_FORGET);
                if (UiRect{340, 335, 280, 58}.contains(x, y)) return action(UiActionType::MATTER_NETWORK_DIAGNOSTIC);
                if (UiRect{645, 335, 280, 58}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            } else {
                if (UiRect{35, 245, 390, 64}.contains(x, y)) return action(UiActionType::MATTER_PAIR_CODE);
                if (UiRect{455, 245, 390, 64}.contains(x, y)) return action(UiActionType::MATTER_NETWORK_DIAGNOSTIC);
                if (UiRect{455, 330, 220, 64}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            }
            break;
        case AppState::MATTER_NO_SAVED_NODE:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::MATTER_PAIR_CODE);
            if (UiRect{455, 405, 220, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_DEVICE_DISCOVERY:
            break;
        case AppState::MATTER_DEVICE_LIST: {
            const int row = (y - MATTER_DEVICE_TOP) / MATTER_DEVICE_ROW_HEIGHT;
            if (x >= 30 && x < 550 && y >= MATTER_DEVICE_TOP && row >= 0 && row < MATTER_DEVICES_PER_PAGE &&
                y < MATTER_DEVICE_TOP + row * MATTER_DEVICE_ROW_HEIGHT + 56) {
                return action(UiActionType::MATTER_DEVICE_SELECT,
                    static_cast<int>(m.matterDevicePage * MATTER_DEVICES_PER_PAGE + row));
            }
            if (UiRect{35, 385, 150, 56}.contains(x, y)) return action(UiActionType::MATTER_ON);
            if (UiRect{205, 385, 150, 56}.contains(x, y)) return action(UiActionType::MATTER_OFF);
            if (UiRect{375, 385, 190, 56}.contains(x, y)) return action(UiActionType::REFRESH);
            if (UiRect{35, 455, 150, 56}.contains(x, y)) return action(UiActionType::PAGE_PREVIOUS);
            if (UiRect{205, 455, 150, 56}.contains(x, y)) return action(UiActionType::PAGE_NEXT);
            if (UiRect{375, 455, 190, 56}.contains(x, y)) return action(UiActionType::BACK);
            break;
        }
        case AppState::MATTER_COMMAND_IN_PROGRESS:
            break;
        case AppState::MATTER_COMMAND_SUCCESS:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_COMMAND_FAILED:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::RETRY);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_CONNECTION_FAILED:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::RETRY);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::MATTER_PAIR_CODE);
            if (UiRect{610, 405, 315, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_DISCOVERY_FAILED:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::RETRY);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_INIT_FAILED:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::RETRY);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::MATTER_FORGET);
            if (UiRect{610, 405, 315, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_SAVED_INVALID:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::MATTER_PAIR_CODE);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::MATTER_FORGET);
            if (UiRect{610, 405, 315, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_FORGET_CONFIRM:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::MATTER_FORGET_CONFIRM);
            if (UiRect{455, 405, 220, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_FORGOT:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::MATTER_RESTART);
            if (UiRect{455, 405, 220, 70}.contains(x, y)) return action(UiActionType::BACK);
            break;
        case AppState::MATTER_CODE_ENTRY:
            if (MATTER_CODE_VALIDATE.contains(x, y)) return action(UiActionType::MATTER_CODE_VALIDATE);
            if (MATTER_CODE_CLEAR.contains(x, y)) return action(UiActionType::KEY_CLEAR);
            if (MATTER_CODE_CANCEL.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            if (x >= MATTER_KEY_X && x < MATTER_KEY_X + MATTER_KEY_STEP_X * 2 + MATTER_KEY_W &&
                y >= MATTER_KEY_Y && y < MATTER_KEY_Y + MATTER_KEY_STEP_Y * 3 + MATTER_KEY_H) {
                const int row = (y - MATTER_KEY_Y) / MATTER_KEY_STEP_Y;
                const int col = (x - MATTER_KEY_X) / MATTER_KEY_STEP_X;
                const std::array<const char *, 4> rows{{"123", "456", "789", "C0<"}};
                if (row >= 0 && row < 4 && col >= 0 && col < 3) {
                    const UiRect keyRect{MATTER_KEY_X + col * MATTER_KEY_STEP_X,
                                         MATTER_KEY_Y + row * MATTER_KEY_STEP_Y,
                                         MATTER_KEY_W,
                                         MATTER_KEY_H};
                    if (!keyRect.contains(x, y)) break;
                    const char key = rows[row][col];
                    if (key == 'C') return action(UiActionType::KEY_CLEAR);
                    if (key == '<') return action(UiActionType::KEY_BACKSPACE);
                    return action(UiActionType::KEY_CHARACTER, -1, key);
                }
            }
            break;
        case AppState::MATTER_CODE_CONFIRM:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::CONTINUE_MATTER);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::MATTER_CODE_EDIT);
            if (UiRect{610, 405, 315, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_PAIRING_MODE:
            if (UiRect{35, 330, 390, 70}.contains(x, y)) return action(UiActionType::MATTER_PAIR_INITIAL);
            if (UiRect{455, 330, 390, 70}.contains(x, y)) return action(UiActionType::MATTER_PAIR_MULTI_ADMIN);
            if (UiRect{610, 430, 315, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_MDNS_PRESCAN_EMPTY:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::CONTINUE_MATTER);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) return action(UiActionType::MATTER_CONTINUE_ANYWAY);
            if (UiRect{610, 405, 315, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_CODE_INVALID:
            if (UiRect{35, 405, 390, 70}.contains(x, y)) return action(UiActionType::MATTER_CODE_EDIT);
            if (UiRect{455, 405, 470, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_COMMISSIONING_PLACEHOLDER:
        case AppState::MATTER_COMMISSIONING:
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_COMMISSIONING_SUCCESS:
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::MATTER_USE_SAVED);
            break;
        case AppState::MATTER_COMMISSIONING_FAILED:
            if (UiRect{35, 405, 260, 70}.contains(x, y)) return action(UiActionType::CONTINUE_MATTER);
            if (UiRect{325, 405, 250, 70}.contains(x, y)) {
                if (m.matterFailureReason == "pairing_code_discovery_filter_mismatch_or_dns_sd_failure") {
                    return action(UiActionType::MATTER_NETWORK_DIAGNOSTIC);
                }
                return action(UiActionType::MATTER_CODE_EDIT);
            }
            if (UiRect{610, 405, 315, 70}.contains(x, y)) return action(UiActionType::BACK_TO_WIFI);
            break;
        case AppState::MATTER_NOT_IMPLEMENTED:
            if (BOTTOM_LEFT.contains(x, y)) return action(UiActionType::BACK);
            break;
        default: break;
    }
    return {};
}

std::string UiManager::signalBars(int rssi) {
    if (rssi >= -55) return "||||";
    if (rssi >= -67) return "|||.";
    if (rssi >= -75) return "||..";
    return "|...";
}

std::string UiManager::maskedPassword(size_t length) {
    return std::string(std::min<size_t>(length, 24), '*') + (length > 24 ? "..." : "");
}

std::string UiManager::formattedMatterCode(const std::string &raw) {
    std::string digits;
    digits.reserve(raw.size());
    for (char c : raw) {
        if (c >= '0' && c <= '9') digits.push_back(c);
    }
    if (digits.empty()) return {};
    std::string out;
    out.reserve(digits.size() + 2);
    for (size_t i = 0; i < digits.size() && i < 11; ++i) {
        if (i == 4 || i == 7) out.push_back('-');
        out.push_back(digits[i]);
    }
    if (digits.size() > 11) out += "...";
    return out;
}
