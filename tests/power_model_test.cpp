/*
 * SPDX-License-Identifier: MIT
 */

#include "power_logic.h"
#include "power_settings.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

void test_catalog() {
    // Cada ajuste tiene clave única, opciones y un valor por defecto que es una de ellas.
    for (const auto& info : power::all_settings()) {
        CHECK(!info.options.empty());
        CHECK(power::option_index(info, info.default_value) >= 0);
        CHECK(power::find_setting(info.key) == &info);
    }
    CHECK(power::find_setting("nope") == nullptr);
    CHECK(power::settings_in(power::Group::Hardware).size() == 5);
    CHECK(power::settings_in(power::Group::Saving).size() == 3);

    const auto* eth = power::find_setting("ethernet");
    CHECK(eth && power::step_option(*eth, "on", 1) == "auto");
    CHECK(eth && power::step_option(*eth, "off", 1) == "on");  // vuelta
    CHECK(eth && power::step_option(*eth, "on", -1) == "off");
    CHECK(eth && power::option_label(*eth, "auto") == "Auto-suspend");
    const auto* gpu = power::find_setting("gpu_mem");
    // Un valor puesto a mano que no es opción: se salta a la primera.
    CHECK(gpu && power::step_option(*gpu, "48", 1) == "16");
    CHECK(gpu && power::option_label(*gpu, "48") == "48");
    CHECK(gpu && gpu->needs_reboot);
}

void test_key_values_and_daemon_config() {
    const auto kv = power::parse_key_values(
        "# comentario\n ethernet = auto \nwifi=on\n\nbasura\n=sin clave\ngpu_mem=16\r\n");
    CHECK(kv.size() == 3);
    CHECK(kv.at("ethernet") == "auto");
    CHECK(kv.at("gpu_mem") == "16");

    auto config = power::daemon_config_from({});
    CHECK(config.idle_saver && !config.quiet_launcher && config.low_battery_percent == 0);
    config = power::daemon_config_from(
        {{"idle_saver", "off"}, {"quiet_launcher", "on"}, {"low_battery", "8"}});
    CHECK(!config.idle_saver && config.quiet_launcher && config.low_battery_percent == 8);
    // Valores no válidos: se quedan los de por defecto.
    config = power::daemon_config_from({{"idle_saver", "maybe"}, {"low_battery", "50"}});
    CHECK(config.idle_saver && config.low_battery_percent == 0);
    // Ida y vuelta por el texto.
    power::DaemonConfig c;
    c.idle_saver = false;
    c.quiet_launcher = true;
    c.low_battery_percent = 10;
    const auto back = power::daemon_config_from(power::parse_key_values(power::daemon_config_text(c)));
    CHECK(!back.idle_saver && back.quiet_launcher && back.low_battery_percent == 10);
}

void test_battery_math() {
    power::CurrentAverage avg(3);
    CHECK(avg.empty() && avg.value() == 0.0);
    avg.add(300);
    avg.add(400);
    avg.add(500);
    avg.add(600); // la primera sale de la ventana
    CHECK(std::fabs(avg.value() - 500.0) < 1e-9);
    CHECK(std::fabs(power::hours_left(1750, 350) - 5.0) < 1e-9);
    CHECK(power::hours_left(1750, -200) < 0); // cargando
    CHECK(power::format_hours(4.5833) == "4 h 35 min");
    CHECK(power::format_hours(0.5) == "30 min");
    CHECK(power::format_hours(-1) == "—");
}

void test_low_battery_guard() {
    power::LowBatteryGuard guard(3);
    CHECK(!guard.update(true, 7, 8));
    CHECK(!guard.update(true, 7, 8));
    CHECK(guard.update(true, 7, 8)); // tercera seguida
    guard = power::LowBatteryGuard(3);
    CHECK(!guard.update(true, 7, 8));
    CHECK(!guard.update(false, 7, 8)); // enchufado: se reinicia la cuenta
    CHECK(guard.strikes() == 0);
    CHECK(!guard.update(true, 9, 8));  // por encima del umbral
    CHECK(!guard.update(true, 1, 0));  // desactivado
}

void test_idle_tracker() {
    using A = power::IdleTracker::Action;
    power::IdleTracker idle(5.0);
    CHECK(idle.update(0, 50, false, true) == A::None);
    CHECK(idle.update(1, 0, false, true) == A::None); // se apaga la pantalla
    CHECK(idle.update(5, 0, false, true) == A::None); // 4 s
    CHECK(idle.update(6, 0, false, true) == A::Sleep);
    CHECK(idle.sleeping());
    CHECK(idle.update(7, 0, false, true) == A::None); // ya dormido
    CHECK(idle.update(8, 0, true, true) == A::Wake);  // tecla
    CHECK(!idle.sleeping());
    CHECK(idle.update(9, 0, false, true) == A::None); // vuelve a contar desde 0
    CHECK(idle.update(14, 0, false, true) == A::Sleep);
    CHECK(idle.update(15, 80, false, true) == A::Wake); // pantalla encendida
    // Apagar la opción estando dormido despierta.
    CHECK(idle.update(20, 0, false, true) == A::None);
    CHECK(idle.update(26, 0, false, true) == A::Sleep);
    CHECK(idle.update(27, 0, false, false) == A::Wake);
    CHECK(idle.update(40, 0, false, false) == A::None);
}

} // namespace

int main() {
    test_catalog();
    test_key_values_and_daemon_config();
    test_battery_math();
    test_low_battery_guard();
    test_idle_tracker();
    if (failures == 0) {
        std::printf("power_model_test: todo bien\n");
    }
    return failures == 0 ? 0 : 1;
}
