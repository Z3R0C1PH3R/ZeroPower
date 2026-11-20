/*
 * SPDX-License-Identifier: MIT
 */

#include "battery_screen.h"

#include "asset_manager.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace screen {
namespace {

constexpr int32_t kW = view::kScreenWidth;
constexpr int32_t kBarsTop = 92;
constexpr int32_t kBarsH = 30;
constexpr int32_t kBarW = 5;
constexpr int32_t kBarsLeft = (kW - 60 * kBarW) / 2;

void set_text(lv_obj_t* label, const char* text) {
    const char* current = lv_label_get_text(label);
    if (!current || std::strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

} // namespace

BatteryScreen::BatteryScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets), battery_dir_(platform::find_battery_dir()) {
    // En el simulador del portátil se encuentra la batería del portátil: para las capturas,
    // ZEROPOWER_FAKE_BATTERY=1 pone números de un Cardputer.
    if (std::getenv("ZEROPOWER_FAKE_BATTERY")) {
        battery_dir_.clear();
    }
    init();
    this->view_model().set_page_handlers(this, [this](uint32_t key) { return handle_key(key); },
                                         []() { return false; });
}

BatteryScreen::~BatteryScreen() {
    view_model().clear_page_handlers(this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
}

void BatteryScreen::build_content(lv_obj_t* content) {
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    const lv_font_t* big = assets().load_standard_font(40);
    const lv_font_t* medium = assets().load_standard_font(14);
    const lv_font_t* small = assets().load_standard_font(12, app::StandardFontWeight::Regular);

    big_ = lv_label_create(content);
    lv_obj_set_style_text_font(big_, big ? big : &lv_font_montserrat_16, 0);
    lv_obj_set_pos(big_, 10, 2);
    unit_ = lv_label_create(content);
    lv_obj_set_style_text_font(unit_, medium ? medium : &lv_font_montserrat_14, 0);
    lv_label_set_text(unit_, "mA");
    caption_ = lv_label_create(content);
    lv_obj_set_style_text_font(caption_, small ? small : &lv_font_montserrat_12, 0);
    lv_obj_set_pos(caption_, 10, 52);

    for (std::size_t i = 0; i < lines_.size(); ++i) {
        lines_[i] = lv_label_create(content);
        lv_obj_set_style_text_font(lines_[i], i == 0 ? medium : small, 0);
        lv_obj_set_width(lines_[i], 165);
        lv_label_set_long_mode(lines_[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(lines_[i], 150, 4 + static_cast<int32_t>(i) * 19);
    }

    for (int i = 0; i < kBars; ++i) {
        auto* bar = lv_obj_create(content);
        lv_obj_remove_style_all(bar);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        bars_[i] = bar;
    }

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, small ? small : &lv_font_montserrat_12, 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(hint_, "Last minute   ·   R restart average   ·   ESC back");

    lv_subject_add_observer_obj(view_model().dark_mode_subject(), theme_observer_cb, content, this);
    timer_ = lv_timer_create(timer_cb, 1000, this);
    sample();
    refresh();
}

bool BatteryScreen::handle_key(uint32_t key) {
    if (key == 'r' || key == 'R') {
        average_.clear();
        history_count_ = 0;
        sample();
        refresh();
        return true;
    }
    return false;
}

void BatteryScreen::sample() {
    if (battery_dir_.empty()) {
        // Simulador: unos números que se mueven, para ver la pantalla.
        static int tick = 0;
        ++tick;
        reading_.present = true;
        reading_.status = "Discharging";
        reading_.capacity = 82;
        reading_.voltage_v = 3.98;
        reading_.current_ma = 300 + 40 * std::sin(tick / 3.0) + (tick % 7) * 6;
        reading_.charge_mah = 1281;
        reading_.charge_full_mah = 1750;
    } else {
        reading_ = platform::read_battery(battery_dir_);
    }
    if (!reading_.present) {
        return;
    }
    average_.add(reading_.current_ma);
    if (history_count_ < kBars) {
        history_[static_cast<std::size_t>(history_count_++)] = reading_.current_ma;
    } else {
        std::rotate(history_.begin(), history_.begin() + 1, history_.end());
        history_.back() = reading_.current_ma;
    }
}

void BatteryScreen::refresh() {
    if (!hint_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    char buf[96];

    if (!reading_.present) {
        set_text(big_, "—");
        set_text(caption_, "No battery gauge found");
        for (auto* line : lines_) {
            set_text(line, "");
        }
        return;
    }

    const double avg = average_.value();
    const bool charging = avg < 0;
    std::snprintf(buf, sizeof(buf), "%s%.0f", charging ? "+" : "", std::fabs(avg));
    set_text(big_, buf);
    lv_obj_update_layout(big_);
    lv_obj_set_pos(unit_, 10 + lv_obj_get_width(big_) + 4, 28);
    std::snprintf(buf, sizeof(buf), "%s %zu s · now %.0f", charging ? "charging, avg" : "avg",
                  average_.count(), std::fabs(reading_.current_ma));
    set_text(caption_, buf);

    std::snprintf(buf, sizeof(buf), "%s · %d %%", reading_.status.c_str(), reading_.capacity);
    set_text(lines_[0], buf);
    std::snprintf(buf, sizeof(buf), "%.2f V", reading_.voltage_v);
    set_text(lines_[1], buf);
    if (reading_.charge_mah >= 0 && reading_.charge_full_mah > 0) {
        std::snprintf(buf, sizeof(buf), "%.0f / %.0f mAh", reading_.charge_mah, reading_.charge_full_mah);
    } else {
        buf[0] = '\0';
    }
    set_text(lines_[2], buf);
    if (charging) {
        set_text(lines_[3], "Left: charging");
    } else {
        const double hours = power::hours_left(reading_.charge_mah, avg);
        std::snprintf(buf, sizeof(buf), "Left: %s", power::format_hours(hours).c_str());
        set_text(lines_[3], buf);
    }

    // Barras: la escala va de 0 al máximo del minuto (como poco 400 mA, para que un consumo
    // bajo se vea bajo).
    double top = 400.0;
    for (int i = 0; i < history_count_; ++i) {
        top = std::max(top, std::fabs(history_[static_cast<std::size_t>(i)]));
    }
    for (int i = 0; i < kBars; ++i) {
        lv_obj_t* bar = bars_[static_cast<std::size_t>(i)];
        const int index = i - (kBars - history_count_); // las más nuevas a la derecha
        if (index < 0) {
            lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const double v = history_[static_cast<std::size_t>(index)];
        const int32_t h = std::max<int32_t>(1, static_cast<int32_t>(std::lround(kBarsH * std::fabs(v) / top)));
        lv_obj_set_size(bar, kBarW - 1, h);
        lv_obj_set_pos(bar, kBarsLeft + i * kBarW, kBarsTop + kBarsH - h);
        lv_obj_set_style_bg_color(bar, v < 0 ? colors.info : colors.primary, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_text_color(big_, charging ? colors.info : colors.text, 0);
    lv_obj_set_style_text_color(unit_, colors.text_disabled, 0);
    lv_obj_set_style_text_color(caption_, colors.text_disabled, 0);
    lv_obj_set_style_text_color(lines_[0], colors.primary, 0);
    for (std::size_t i = 1; i < lines_.size(); ++i) {
        lv_obj_set_style_text_color(lines_[i], colors.text, 0);
    }
    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

void BatteryScreen::timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<BatteryScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->sample();
        screen->refresh();
    }
}

void BatteryScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<BatteryScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
