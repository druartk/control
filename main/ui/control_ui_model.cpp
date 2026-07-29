#include "ui/control_ui_model.h"

namespace {
UiDevice salonDevices[] = {
    {101, "Plafonnier", true, 100},
    {102, "Lampe canape", false, 75},
    {103, "Lampe TV", true, 60},
};

UiDevice cuisineDevices[] = {
    {201, "Plafonnier", false, 100},
    {202, "Plan de travail", false, 80},
};

UiDevice chambreDevices[] = {
    {301, "Plafonnier", false, 100},
    {302, "Lampe de chevet", true, 50},
};

UiRoom rooms[] = {
    {1, "Salon", salonDevices, sizeof(salonDevices) / sizeof(salonDevices[0])},
    {2, "Cuisine", cuisineDevices, sizeof(cuisineDevices) / sizeof(cuisineDevices[0])},
    {3, "Chambre", chambreDevices, sizeof(chambreDevices) / sizeof(chambreDevices[0])},
};
}

UiRoom *control_ui_mock_rooms() {
    return rooms;
}

void control_ui_mock_reset() {
    salonDevices[0].is_on = true;
    salonDevices[1].is_on = false;
    salonDevices[2].is_on = true;
    cuisineDevices[0].is_on = false;
    cuisineDevices[1].is_on = false;
    chambreDevices[0].is_on = false;
    chambreDevices[1].is_on = true;
}
