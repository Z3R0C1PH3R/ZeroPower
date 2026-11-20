/*
 * SPDX-License-Identifier: MIT
 */

#include "power_logic.h"

#include <cmath>
#include <cstdio>

namespace power {

void CurrentAverage::add(double milliamps) {
    samples_.push_back(milliamps);
    while (samples_.size() > window_) {
        samples_.pop_front();
    }
}

double CurrentAverage::value() const {
    if (samples_.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double s : samples_) {
        sum += s;
    }
    return sum / static_cast<double>(samples_.size());
}

double hours_left(double charge_mah, double draw_ma) {
    if (draw_ma < 5.0 || charge_mah <= 0.0) {
        return -1.0;
    }
    return charge_mah / draw_ma;
}

std::string format_hours(double hours) {
    if (hours < 0.0) {
        return "—";
    }
    const long minutes = std::lround(hours * 60.0);
    char buf[32];
    if (minutes < 60) {
        std::snprintf(buf, sizeof(buf), "%ld min", minutes);
    } else {
        std::snprintf(buf, sizeof(buf), "%ld h %02ld min", minutes / 60, minutes % 60);
    }
    return buf;
}

bool LowBatteryGuard::update(bool discharging, int capacity_percent, int threshold_percent) {
    if (threshold_percent <= 0 || !discharging || capacity_percent < 0 ||
        capacity_percent > threshold_percent) {
        strikes_ = 0;
        return false;
    }
    ++strikes_;
    return strikes_ >= confirmations_;
}

IdleTracker::Action IdleTracker::update(double now_s, int brightness, bool key_pressed, bool enabled) {
    if (!enabled) {
        dark_ = false;
        if (sleeping_) {
            sleeping_ = false;
            return Action::Wake; // se ha apagado la opción en reposo: despertar
        }
        return Action::None;
    }
    if (key_pressed || brightness > 0) {
        dark_ = false;
        if (sleeping_) {
            sleeping_ = false;
            return Action::Wake;
        }
        return Action::None;
    }
    // Pantalla apagada y sin teclas.
    if (!dark_) {
        dark_ = true;
        dark_since_ = now_s;
    }
    if (!sleeping_ && now_s - dark_since_ >= delay_s_) {
        sleeping_ = true;
        return Action::Sleep;
    }
    return Action::None;
}

} // namespace power
