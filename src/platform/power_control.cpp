/*
 * SPDX-License-Identifier: MIT
 */

#include "power_control.h"

#include "power_settings.h"
#include "process.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace platform {
namespace {

namespace fs = std::filesystem;

constexpr const char* kHelper = "/usr/lib/zeropower/zeropower-helper";

std::string daemon_config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string root = (xdg && *xdg) ? xdg : std::string(home ? home : "/tmp") + "/.config";
    return root + "/zeropower/power.conf";
}

std::string last_line(const std::string& text) {
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    const auto nl = s.rfind('\n');
    return nl == std::string::npos ? s : s.substr(nl + 1);
}

} // namespace

PowerControl::PowerControl() {
    std::error_code ec;
    simulated_ = !fs::exists(kHelper, ec);
    if (simulated_) {
        for (const auto& info : power::all_settings()) {
            if (info.applier == power::Applier::System) {
                fake_system_[info.key] = info.default_value;
            }
        }
        fake_system_["gpu_mem_active"] = "64";
    }
}

std::map<std::string, std::string> PowerControl::read_daemon_values() const {
    // Lo que no esté en el fichero vale lo de por defecto de zeropowerd.
    const auto config = power::daemon_config_from(power::parse_key_values(read_text_file(daemon_config_path())));
    return {
        {"idle_saver", config.idle_saver ? "on" : "off"},
        {"quiet_launcher", config.quiet_launcher ? "on" : "off"},
        {"low_battery", std::to_string(config.low_battery_percent)},
    };
}

std::map<std::string, std::string> PowerControl::read_all() {
    std::map<std::string, std::string> values;
    if (simulated_) {
        values = fake_system_;
    } else {
        values = power::parse_key_values(run_process({kHelper, "status"}).output);
    }
    for (const auto& [k, v] : read_daemon_values()) {
        values[k] = v;
    }
    return values;
}

PowerControl::Result PowerControl::apply(const std::string& key, const std::string& value) {
    Result result;
    const power::SettingInfo* info = power::find_setting(key);
    if (!info || power::option_index(*info, value) < 0) {
        result.message = "Invalid value";
        return result;
    }

    if (info->applier == power::Applier::Daemon) {
        auto values = read_daemon_values();
        values[key] = value;
        const auto config = power::daemon_config_from(values);
        const fs::path path(daemon_config_path());
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        // Temporal + rename: zeropowerd nunca lee un fichero a medias.
        const std::string tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << power::daemon_config_text(config);
            if (!out) {
                result.message = "Cannot write " + path.string();
                return result;
            }
        }
        fs::rename(tmp, path, ec);
        if (ec) {
            result.message = "Cannot write " + path.string();
            return result;
        }
        result.ok = true;
        result.value = value;
        return result;
    }

    if (simulated_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // como si tardara
        fake_system_[key] = value;
        result.ok = true;
        result.value = value;
        return result;
    }

    const auto r = run_process({"sudo", "-n", kHelper, "set", key, value});
    const std::string line = last_line(r.output);
    if (r.exit_code == 0) {
        const auto kv = power::parse_key_values(line);
        const auto it = kv.find(key);
        result.value = it != kv.end() ? it->second : value;
        result.ok = result.value == value;
        if (!result.ok) {
            result.message = "Now: " + power::option_label(*info, result.value);
        }
    } else if (line.find("password") != std::string::npos || line.find("sudo") != std::string::npos) {
        result.message = "Not allowed (sudo)";
    } else {
        result.message = line.empty() ? "Failed" : line;
    }
    return result;
}

} // namespace platform
