/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

// ZeroPower: un menú (Home), la batería y las tres listas de ajustes.
enum class AppPage {
    Home = 0,
    Battery = 1,  // consumo en vivo, carga y tiempo que queda
    Hardware = 2, // Ethernet, Wi-Fi, Bluetooth, escritorio HDMI, memoria de la GPU
    Services = 3, // Tailscale, servicios de fondo, journal en RAM
    Saving = 4,   // lo que hace zeropowerd: pantalla apagada, lanzador silencioso, batería baja
};

} // namespace model
