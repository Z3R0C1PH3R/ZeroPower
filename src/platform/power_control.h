/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <map>
#include <string>

namespace platform {

/**
 * Lee y cambia los ajustes de ZeroPower:
 * - los del sistema, con el ayudante (`zeropower-helper status` sin root; `sudo -n
 *   zeropower-helper set` para cambiar),
 * - los de zeropowerd, en ~/.config/zeropower/power.conf (el servicio lo relee solo).
 *
 * Sin el ayudante instalado (el simulador de escritorio) simula el sistema en memoria.
 * Las llamadas bloquean (el ayudante tarda unas décimas): la pantalla las hace en otro hilo.
 */
class PowerControl {
public:
    PowerControl();

    bool simulated() const {
        return simulated_;
    }

    // Todos los valores actuales, por clave (incluye "gpu_mem_active" si se sabe).
    std::map<std::string, std::string> read_all();

    struct Result {
        bool ok = false;
        std::string value;   // el valor que queda puesto
        std::string message; // por qué falló
    };
    Result apply(const std::string& key, const std::string& value);

private:
    std::map<std::string, std::string> read_daemon_values() const;

    bool simulated_ = false;
    std::map<std::string, std::string> fake_system_;
};

} // namespace platform
