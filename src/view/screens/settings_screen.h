/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "power_control.h"
#include "power_settings.h"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

/**
 * Una lista de ajustes (hardware, servicios o ahorro). F/X elige fila. Los de sí/no son un
 * interruptor (como en iOS) que ENTER, Z o C cambian y aplican al momento; los de varias
 * opciones se eligen con Z/C (queda pendiente, marcado) y ENTER los aplica. Leer el estado y aplicar se hace en otro hilo: el
 * ayudante tarda unas décimas y la pantalla no se queda congelada.
 */
class SettingsScreen : public BaseScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, power::Group group);
    ~SettingsScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static constexpr int kMaxRows = 6;

    bool handle_key(uint32_t key);
    void start_job(std::function<void()> job);
    void apply_value(const power::SettingInfo& info, const std::string& target);
    void refresh();

    static void timer_cb(lv_timer_t* timer);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    power::Group group_;
    std::vector<const power::SettingInfo*> settings_;
    platform::PowerControl control_;

    int row_ = 0;
    std::map<std::string, std::string> pending_; // valor elegido y aún sin aplicar
    std::string confirm_key_;                    // segundo ENTER pendiente (acceso remoto)
    std::string applying_key_;                   // el interruptor ya se ve en su nuevo valor
    std::string applying_value_;

    // Lo que comparte el hilo de trabajo con la pantalla.
    std::mutex mutex_;
    std::map<std::string, std::string> values_; // estado real
    bool loaded_ = false;
    std::string message_;
    bool message_error_ = false;
    std::atomic<bool> busy_{false};
    std::thread worker_;

    std::array<lv_obj_t*, kMaxRows> row_boxes_{};
    std::array<lv_obj_t*, kMaxRows> row_titles_{};
    std::array<lv_obj_t*, kMaxRows> row_values_{};
    std::array<lv_obj_t*, kMaxRows> row_switches_{};
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_timer_t* timer_ = nullptr;
};

} // namespace screen
