/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "battery.h"
#include "power_logic.h"

#include <array>
#include <string>

namespace screen {

/**
 * Batería: el consumo de ahora (media de los últimos 30 s, que es la cifra que sirve para
 * comparar un ajuste con otro), la carga y cuánto queda a ese ritmo. Abajo, los últimos
 * minutos en barras. R empieza la media de cero (tras cambiar un ajuste). En medio, la
 * posición del interruptor USB del lateral, que en Host gasta unos 90 mA más.
 */
class BatteryScreen : public BaseScreen {
public:
    BatteryScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~BatteryScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static constexpr int kBars = 60; // un minuto, una barra por segundo

    bool handle_key(uint32_t key);
    void sample();
    void refresh();

    static void timer_cb(lv_timer_t* timer);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::string battery_dir_;
    platform::BatteryReading reading_;
    power::CurrentAverage average_{30};
    std::array<double, kBars> history_{};
    int history_count_ = 0;

    lv_obj_t* big_ = nullptr;
    lv_obj_t* unit_ = nullptr;
    lv_obj_t* caption_ = nullptr;
    std::array<lv_obj_t*, 4> lines_{};
    std::array<lv_obj_t*, kBars> bars_{};
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* usb_ = nullptr; // posición del interruptor USB (Host gasta más)
    power::UsbSwitch usb_switch_ = power::UsbSwitch::Unknown;
    lv_timer_t* timer_ = nullptr;
};

} // namespace screen
