/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace power {

// --- Batería ---

// Media móvil del consumo: `current_now` del bq27220 salta mucho de un segundo a otro (la
// CPU, la wifi), y una cifra quieta es la que permite comparar un ajuste con otro.
class CurrentAverage {
public:
    explicit CurrentAverage(std::size_t window = 30) : window_(window) {}
    void add(double milliamps);
    void clear() {
        samples_.clear();
    }
    bool empty() const {
        return samples_.empty();
    }
    std::size_t count() const {
        return samples_.size();
    }
    double value() const; // 0 si no hay muestras

private:
    std::size_t window_;
    std::deque<double> samples_;
};

// Horas que quedan con `charge_mah` al consumo `draw_ma` (positivo = gasta). -1 si no se
// puede saber (cargando, o consumo casi nulo).
double hours_left(double charge_mah, double draw_ma);

// "4 h 35 min", "52 min", "—".
std::string format_hours(double hours);

// --- Batería baja ---

// Apaga cuando, descargando, la carga está en el umbral o por debajo en `confirmations`
// lecturas seguidas: una lectura suelta baja (el medidor se recalibra, un pico) no apaga.
class LowBatteryGuard {
public:
    explicit LowBatteryGuard(int confirmations = 3) : confirmations_(confirmations) {}
    // Devuelve true cuando toca apagar.
    bool update(bool discharging, int capacity_percent, int threshold_percent);
    int strikes() const {
        return strikes_;
    }

private:
    int confirmations_;
    int strikes_ = 0;
};

// --- Pantalla apagada ---

// Qué hacer con el lanzador según la pantalla (brillo) y el teclado. Con la pantalla a 0
// durante `delay_s` se entra en reposo (pausar el lanzador, aparcar su audio); una tecla o
// la pantalla encendida despiertan.
class IdleTracker {
public:
    enum class Action { None, Sleep, Wake };

    explicit IdleTracker(double delay_s = 5.0) : delay_s_(delay_s) {}
    // `now_s`: reloj monótono en segundos.
    Action update(double now_s, int brightness, bool key_pressed, bool enabled);
    bool sleeping() const {
        return sleeping_;
    }

private:
    double delay_s_;
    bool sleeping_ = false;
    bool dark_ = false;
    double dark_since_ = 0.0;
};

// --- Tiempo de pantalla ---

// Red de seguridad: la pantalla la apaga el lanzador (su salvapantallas), pero si no lo hace
// (una app abierta, o el lanzador se queda despierto) se quedaría encendida hasta agotar la
// batería. Con la pantalla encendida y `timeout_s` sin teclas, la apaga el servicio; la
// siguiente tecla le devuelve el brillo que tenía. Si otro la enciende mientras tanto (una
// app, el lanzador), deja de ser cosa nuestra y se vuelve a contar desde ahí.
class ScreenTimeout {
public:
    enum class Action { None, TurnOff, TurnOn };

    // `brightness` < 0: no hay pantalla que mirar. `timeout_s` <= 0: desactivado.
    Action update(double now_s, int brightness, bool key_pressed, double timeout_s);
    bool forced_off() const {
        return forced_;
    }
    // El brillo que había al apagarla (para TurnOn y para dejarlo como estaba al parar).
    int saved_brightness() const {
        return saved_;
    }

private:
    bool started_ = false;
    bool forced_ = false;
    int saved_ = 0;
    int last_brightness_ = -1;
    double last_activity_ = 0.0;
};

} // namespace power
