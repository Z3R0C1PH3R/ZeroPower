/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "asset_manager.h"
#include "theme.h"

#include <cstdio>
#include <cstring>

namespace screen {
namespace {

constexpr int32_t kW = view::kScreenWidth;
constexpr int32_t kMargin = 6;
constexpr int32_t kRowH = 17;
constexpr int32_t kTop = 3;

const lv_color_t kWarnColor = lv_color_hex(0xF9A825);
const lv_color_t kErrorColor = lv_color_hex(0xE53935);

void set_text(lv_obj_t* label, const std::string& text) {
    const char* current = lv_label_get_text(label);
    if (!current || text != current) {
        lv_label_set_text(label, text.c_str());
    }
}

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets,
                               power::Group group)
    : BaseScreen(view_model, assets), group_(group), settings_(power::settings_in(group)) {
    init();
    this->view_model().set_page_handlers(this, [this](uint32_t key) { return handle_key(key); },
                                         [this]() {
                                             // ESC con un valor elegido sin aplicar: se descarta
                                             // y se queda; otro ESC vuelve al menú.
                                             if (!pending_.empty() || !confirm_key_.empty()) {
                                                 pending_.clear();
                                                 confirm_key_.clear();
                                                 refresh();
                                                 return true;
                                             }
                                             return false;
                                         });
    start_job([this]() {
        auto values = control_.read_all();
        std::lock_guard<std::mutex> lock(mutex_);
        values_ = std::move(values);
        loaded_ = true;
    });
}

SettingsScreen::~SettingsScreen() {
    view_model().clear_page_handlers(this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
    if (worker_.joinable()) {
        worker_.join(); // un cambio a medias se termina: no se deja el sistema a medias
    }
}

void SettingsScreen::build_content(lv_obj_t* content) {
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    const lv_font_t* title_font = assets().load_standard_font(13);
    const lv_font_t* value_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    const lv_font_t* small = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    for (int i = 0; i < kMaxRows; ++i) {
        auto* box = lv_obj_create(content);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, kW - 2 * kMargin, kRowH);
        lv_obj_set_pos(box, kMargin, kTop + i * kRowH);
        lv_obj_set_style_radius(box, 3, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        row_boxes_[i] = box;

        row_titles_[i] = lv_label_create(box);
        lv_obj_set_style_text_font(row_titles_[i], title_font ? title_font : &lv_font_montserrat_12, 0);
        lv_obj_align(row_titles_[i], LV_ALIGN_LEFT_MID, 5, 0);

        row_values_[i] = lv_label_create(box);
        lv_obj_set_style_text_font(row_values_[i], value_font ? value_font : &lv_font_montserrat_12, 0);
        lv_obj_align(row_values_[i], LV_ALIGN_RIGHT_MID, -5, 0);
    }

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, small ? small : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint_, kW - 2 * kMargin - 6);
    // Justo debajo de la última fila (como mucho 5 por grupo).
    lv_obj_set_pos(hint_, kMargin + 3, kTop + static_cast<int32_t>(settings_.size()) * kRowH + 4);

    status_ = lv_label_create(content);
    lv_obj_set_style_text_font(status_, small ? small : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(status_, kW - 2 * kMargin - 6);
    lv_obj_align(status_, LV_ALIGN_BOTTOM_LEFT, kMargin + 3, -2);

    lv_subject_add_observer_obj(view_model().dark_mode_subject(), theme_observer_cb, content, this);
    timer_ = lv_timer_create(timer_cb, 150, this);
    refresh();
}

void SettingsScreen::start_job(std::function<void()> job) {
    if (busy_) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    busy_ = true;
    worker_ = std::thread([this, job = std::move(job)]() {
        job();
        busy_ = false;
    });
}

bool SettingsScreen::handle_key(uint32_t key) {
    if (settings_.empty()) {
        return false;
    }
    const int n = static_cast<int>(settings_.size());
    const power::SettingInfo& info = *settings_[static_cast<std::size_t>(row_)];
    std::string current;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = values_.find(info.key);
        current = it != values_.end() ? it->second : "";
    }
    const bool absent = current == "absent";

    switch (key) {
        case LV_KEY_UP:
        case 'f':
        case 'F':
            row_ = (row_ + n - 1) % n;
            confirm_key_.clear();
            break;
        case LV_KEY_DOWN:
        case 'x':
        case 'X':
            row_ = (row_ + 1) % n;
            confirm_key_.clear();
            break;
        case LV_KEY_LEFT:
        case 'z':
        case 'Z':
        case LV_KEY_RIGHT:
        case 'c':
        case 'C': {
            if (absent || busy_ || current.empty()) {
                break;
            }
            const int delta = (key == LV_KEY_LEFT || key == 'z' || key == 'Z') ? -1 : 1;
            const auto it = pending_.find(info.key);
            const std::string from = it != pending_.end() ? it->second : current;
            const std::string next = power::step_option(info, from, delta);
            if (next == current) {
                pending_.erase(info.key);
            } else {
                pending_[info.key] = next;
            }
            confirm_key_.clear();
            break;
        }
        case LV_KEY_ENTER:
        case '\r':
        case ' ': {
            if (absent || busy_ || current.empty()) {
                break;
            }
            // Sin valor elegido, ENTER pasa al siguiente (como un interruptor).
            std::string target = pending_.count(info.key) ? pending_[info.key] : power::step_option(info, current, 1);
            const bool losing_remote = info.warn_remote && target == "off";
            if (losing_remote && confirm_key_ != info.key) {
                pending_[info.key] = target;
                confirm_key_ = info.key;
                break;
            }
            confirm_key_.clear();
            pending_.erase(info.key);
            const std::string key_name = info.key;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                message_ = "Applying " + std::string(info.title) + "…";
                message_error_ = false;
            }
            start_job([this, key_name, target]() {
                const auto result = control_.apply(key_name, target);
                auto values = control_.read_all();
                std::lock_guard<std::mutex> lock(mutex_);
                values_ = std::move(values);
                const power::SettingInfo* applied = power::find_setting(key_name);
                if (result.ok) {
                    message_ = std::string(applied->title) + ": " + power::option_label(*applied, result.value);
                    if (applied->needs_reboot) {
                        message_ += " (reboot to apply)";
                    }
                    message_error_ = false;
                } else {
                    message_ = std::string(applied->title) + ": " + result.message;
                    message_error_ = true;
                }
            });
            break;
        }
        default:
            return false;
    }
    refresh();
    return true;
}

void SettingsScreen::refresh() {
    if (!status_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    std::map<std::string, std::string> values;
    bool loaded;
    std::string message;
    bool message_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        values = values_;
        loaded = loaded_;
        message = message_;
        message_error = message_error_;
    }

    for (int i = 0; i < kMaxRows; ++i) {
        if (i >= static_cast<int>(settings_.size())) {
            lv_obj_add_flag(row_boxes_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const power::SettingInfo& info = *settings_[static_cast<std::size_t>(i)];
        const bool selected = i == row_;
        lv_obj_remove_flag(row_boxes_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(row_boxes_[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row_boxes_[i], colors.primary, 0);
        set_text(row_titles_[i], info.title);

        std::string text;
        const auto it = values.find(info.key);
        const auto pend = pending_.find(info.key);
        bool dim = false;
        if (!loaded || it == values.end()) {
            text = "…";
        } else if (it->second == "absent") {
            text = "not installed";
            dim = true;
        } else if (pend != pending_.end()) {
            text = "‹ " + power::option_label(info, pend->second) + " › •";
        } else {
            text = "‹ " + power::option_label(info, it->second) + " ›";
            // La memoria de la GPU de config.txt vale tras reiniciar: si no es la de ahora, se dice.
            if (std::strcmp(info.key, "gpu_mem") == 0) {
                const auto active = values.find("gpu_mem_active");
                if (active != values.end() && !active->second.empty() && active->second != it->second) {
                    text = "‹ " + power::option_label(info, it->second) + " › (now " + active->second + ")";
                }
            }
        }
        set_text(row_values_[i], text);
        lv_obj_set_style_text_color(row_titles_[i], selected ? colors.background : colors.text, 0);
        lv_obj_set_style_text_color(row_values_[i],
                                    selected ? colors.background : (dim ? colors.text_disabled : colors.text), 0);
    }

    if (!settings_.empty()) {
        const power::SettingInfo& info = *settings_[static_cast<std::size_t>(row_)];
        set_text(hint_, info.hint);
        lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
    }

    std::string status;
    lv_color_t status_color = colors.text_disabled;
    if (!confirm_key_.empty()) {
        status = "Remote access will be lost. ENTER again to confirm.";
        status_color = kWarnColor;
    } else if (busy_ && loaded) {
        status = message.empty() ? "Working…" : message;
    } else if (!loaded) {
        status = "Reading the system…";
    } else if (!pending_.empty()) {
        status = "ENTER apply   ESC discard";
        status_color = kWarnColor;
    } else if (!message.empty()) {
        status = message;
        status_color = message_error ? kErrorColor : colors.text_disabled;
    } else {
        status = control_.simulated() ? "Simulated system (no helper)" : "F/X row   Z/C value   ENTER apply";
    }
    set_text(status_, status);
    lv_obj_set_style_text_color(status_, status_color, 0);
}

void SettingsScreen::timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<SettingsScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->refresh();
    }
}

void SettingsScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
