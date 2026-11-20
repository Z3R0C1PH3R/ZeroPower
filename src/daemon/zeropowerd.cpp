/*
 * SPDX-License-Identifier: MIT
 */

// zeropowerd: lo que ZeroPower hace mientras usas el aparato, como servicio del usuario (el
// mismo que el lanzador, APPLaunch.service), sin root:
//
// - Pantalla apagada (brillo 0) durante 5 s: pausa el lanzador (SIGSTOP) y aparca su audio
//   en un sumidero nulo para que el amplificador y la tarjeta de sonido duerman. Una tecla o
//   la pantalla encendida lo despiertan. Si hay una app abierta desde el lanzador, no se
//   pausa nada (la app seguiría sin pantalla, pero quizá sonando o grabando).
// - «Quiet launcher»: el audio del lanzador aparcado siempre (sin sonido de teclas).
// - Batería baja: apaga ordenadamente tras tres lecturas seguidas en el umbral.
//
// Las ideas (y las medidas de cuánto ahorra cada una) son de cz-battery-saver
// (github.com/InstantComet/cz-battery-saver, MIT); el código es propio.
//
// Gasto propio: duerme en poll() sobre el teclado con 1 s de tope; lee el brillo cada
// segundo, la batería cada 20 s y sus opciones cada 3 s.

#include "battery.h"
#include "power_logic.h"
#include "power_settings.h"
#include "process.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void say(const char* fmt, const std::string& arg = "") {
    std::fprintf(stderr, "zeropowerd: ");
    std::fprintf(stderr, fmt, arg.c_str());
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

std::string config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string root = (xdg && *xdg) ? xdg : std::string(home ? home : "/tmp") + "/.config";
    return root + "/zeropower/power.conf";
}

// --- Lanzador ---

pid_t find_launcher() {
    DIR* proc = ::opendir("/proc");
    if (!proc) {
        return -1;
    }
    pid_t found = -1;
    while (dirent* e = ::readdir(proc)) {
        const pid_t pid = static_cast<pid_t>(std::atoi(e->d_name));
        if (pid <= 0) {
            continue;
        }
        const std::string cmd = platform::read_text_file("/proc/" + std::to_string(pid) + "/cmdline");
        if (cmd.find("M5CardputerZero-APPLaunch") != std::string::npos && cmd.rfind("/", 0) == 0) {
            found = pid;
            break;
        }
    }
    ::closedir(proc);
    return found;
}

// ¿Tiene el lanzador una app abierta (un hijo)?
bool has_children(pid_t parent) {
    const std::string children =
        platform::read_text_file("/proc/" + std::to_string(parent) + "/task/" + std::to_string(parent) + "/children");
    return children.find_first_of("0123456789") != std::string::npos;
}

// --- Audio (pactl de PipeWire) ---

const char* kNullSink = "zeropower_null";

// Índices de los flujos de audio del proceso `pid`.
std::vector<std::string> sink_inputs_of(pid_t pid) {
    const auto r = platform::run_process({"pactl", "list", "sink-inputs"});
    std::vector<std::string> out;
    std::string current;
    const std::string needle = "application.process.id = \"" + std::to_string(pid) + "\"";
    std::size_t pos = 0;
    while (pos < r.output.size()) {
        const std::size_t end = r.output.find('\n', pos);
        const std::string line = r.output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? r.output.size() : end + 1;
        if (line.rfind("Sink Input #", 0) == 0) {
            current = line.substr(12);
        } else if (!current.empty() && line.find(needle) != std::string::npos) {
            out.push_back(current);
        }
    }
    return out;
}

class AudioParker {
public:
    // Mueve los flujos del lanzador al sumidero nulo (se puede llamar a menudo: el lanzador
    // a veces abre uno nuevo).
    void park(pid_t launcher) {
        if (launcher <= 0) {
            return;
        }
        if (module_.empty()) {
            const auto r = platform::run_process(
                {"pactl", "load-module", "module-null-sink", std::string("sink_name=") + kNullSink,
                 "sink_properties=device.description=ZeroPower"});
            if (r.exit_code != 0) {
                return;
            }
            module_ = r.output.substr(0, r.output.find_first_not_of("0123456789"));
            say("audio: launcher parked");
        }
        for (const auto& input : sink_inputs_of(launcher)) {
            platform::run_process({"pactl", "move-sink-input", input, kNullSink});
        }
    }

    void restore(pid_t launcher) {
        if (module_.empty()) {
            return;
        }
        auto sink = platform::run_process({"pactl", "get-default-sink"}).output;
        while (!sink.empty() && (sink.back() == '\n' || sink.back() == ' ')) {
            sink.pop_back();
        }
        if (launcher > 0 && !sink.empty() && sink != kNullSink) {
            for (const auto& input : sink_inputs_of(launcher)) {
                platform::run_process({"pactl", "move-sink-input", input, sink});
            }
        }
        // Al quitar el módulo, lo que quedara en él vuelve solo al sumidero por defecto.
        platform::run_process({"pactl", "unload-module", module_});
        module_.clear();
        say("audio: launcher restored");
    }

    bool parked() const {
        return !module_.empty();
    }

private:
    std::string module_;
};

// --- Teclado ---

int open_keyboard() {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/dev/input", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0) {
            continue;
        }
        const int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        unsigned long keys[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0) {
            const auto bit = [&](int code) {
                return (keys[code / (8 * sizeof(unsigned long))] >> (code % (8 * sizeof(unsigned long)))) & 1UL;
            };
            // Un teclado de verdad (letras y Enter), no los botones de encendido o el IR.
            if (bit(KEY_A) && bit(KEY_ENTER) && bit(KEY_ESC)) {
                return fd;
            }
        }
        ::close(fd);
    }
    return -1;
}

// Lee lo pendiente; true si había alguna pulsación.
bool drain_keys(int fd) {
    bool pressed = false;
    input_event ev{};
    while (::read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type == EV_KEY && ev.value == 1) {
            pressed = true;
        }
    }
    return pressed;
}

} // namespace

int main() {
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    std::signal(SIGHUP, on_signal);

    const std::string battery_dir = platform::find_battery_dir();
    const std::string backlight_dir = platform::find_backlight_dir();
    const std::string conf = config_path();
    say("started, options in %s", conf);

    power::DaemonConfig config;
    long long config_mtime = -1;
    power::IdleTracker idle(5.0);
    power::LowBatteryGuard low(3);
    AudioParker audio;
    pid_t launcher = -1;
    bool launcher_stopped = false;
    int kbd = -1;
    double next_config = 0, next_battery = 0, next_scan = 0, next_audio = 0;

    auto resume_launcher = [&]() {
        if (launcher_stopped && launcher > 0) {
            ::kill(launcher, SIGCONT);
            say("launcher resumed");
        }
        launcher_stopped = false;
    };

    while (!g_stop) {
        const double t = now_s();

        if (t >= next_config) {
            next_config = t + 3;
            struct stat st{};
            const long long mtime = ::stat(conf.c_str(), &st) == 0 ? static_cast<long long>(st.st_mtime) : 0;
            if (mtime != config_mtime) {
                config_mtime = mtime;
                config = power::daemon_config_from(power::parse_key_values(platform::read_text_file(conf)));
            }
        }

        if (t >= next_scan) {
            next_scan = t + 5;
            if (launcher <= 0 || ::kill(launcher, 0) != 0) {
                launcher = find_launcher();
                launcher_stopped = false;
            }
            if (kbd < 0) {
                kbd = open_keyboard();
            }
        }

        // Teclado: se duerme aquí (1 s como mucho).
        bool key = false;
        if (kbd >= 0) {
            pollfd pfd{kbd, POLLIN, 0};
            const int ready = ::poll(&pfd, 1, 1000);
            if (ready > 0) {
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    ::close(kbd);
                    kbd = -1;
                } else {
                    key = drain_keys(kbd);
                }
            }
        } else {
            ::usleep(1000 * 1000);
        }

        // Pantalla apagada.
        const int brightness = platform::read_brightness(backlight_dir);
        const bool app_open = launcher > 0 && has_children(launcher);
        const bool idle_enabled = config.idle_saver && launcher > 0 && !app_open && brightness >= 0;
        switch (idle.update(now_s(), brightness, key, idle_enabled)) {
            case power::IdleTracker::Action::Sleep:
                audio.park(launcher);
                if (::kill(launcher, SIGSTOP) == 0) {
                    launcher_stopped = true;
                    say("screen off: launcher paused");
                }
                break;
            case power::IdleTracker::Action::Wake:
                resume_launcher();
                if (!config.quiet_launcher) {
                    audio.restore(launcher);
                }
                break;
            case power::IdleTracker::Action::None:
                break;
        }

        // Lanzador silencioso: se repasa de vez en cuando por si abre un flujo nuevo.
        if (t >= next_audio) {
            next_audio = t + 5;
            if (config.quiet_launcher && !launcher_stopped) {
                audio.park(launcher);
            } else if (!config.quiet_launcher && !idle.sleeping() && audio.parked()) {
                audio.restore(launcher);
            }
        }

        // Batería baja.
        if (t >= next_battery) {
            next_battery = t + 20;
            const auto b = platform::read_battery(battery_dir);
            if (b.present && low.update(b.discharging(), b.capacity, config.low_battery_percent)) {
                say("battery at %s %%: powering off", std::to_string(b.capacity));
                resume_launcher();
                platform::run_process({"systemctl", "poweroff", "--check-inhibitors=no"});
            }
        }
    }

    // Al parar el servicio (o desinstalar) no se deja nada a medias.
    resume_launcher();
    audio.restore(launcher);
    if (kbd >= 0) {
        ::close(kbd);
    }
    say("stopped");
    return 0;
}
