// NORA desktop controller -- C++/SDL2 port of scripts/controller.py.
//
// Same layout, same palette, same keybindings and mode/UV/speed/lock
// telemetry-sync behavior as the Python version and the web dashboard.
// WiFi/WinHTTP only in this port -- no Bluetooth serial transport yet
// (see http_client.hpp for why).
//
// CONTROLS (identical to the Python controller and the website):
//   W/S, arrows   forward/backward          A/D, arrows  strafe left/right
//   Q/E           turn left/right           Space        music play/pause
//   M             next track                X            cycle speed 25/50/75/100%
//   U             cycle UV                  1/2/3/4      manual/auto/line/remote mode
//   Esc           quit
//
// USAGE
//   nora_controller.exe                  connection picker (host/port fields)
//   nora_controller.exe --wifi           skip picker, use --host/--port
//   nora_controller.exe --host 192.168.4.1 --port 5002 --wifi
//   nora_controller.exe --font "C:\path\to\font.ttf"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "http_client.hpp"
#include "json.hpp"

// ----------------------------------------------------------------------------
// Tiny fixed-size thread pool -- fire-and-forget command sends use this
// instead of spawning a fresh OS thread per command (a held drive key
// resends every ~150ms), matching the Python controller's
// ThreadPoolExecutor(max_workers=2).
// ----------------------------------------------------------------------------
class ThreadPool {
public:
    explicit ThreadPool(size_t n) {
        for (size_t i = 0; i < n; i++) workers_.emplace_back([this] { loop(); });
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_)
            if (w.joinable()) w.join();
    }
    void submit(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lk(m_);
            tasks_.push(std::move(f));
        }
        cv_.notify_one();
    }

private:
    void loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// ----------------------------------------------------------------------------
// WiFi link -- polls /sensors + the fleet registry (/robots on port 5000)
// in the background, and fire-and-forgets commands through the pool above.
// ----------------------------------------------------------------------------
class WifiLink {
public:
    WifiLink(std::string host, int port) : host_(std::move(host)), port_(port), pool_(2) {
        poll_thread_ = std::thread([this] { poll_loop(); });
        fleet_thread_ = std::thread([this] { fleet_loop(); });
    }
    ~WifiLink() {
        running_ = false;
        if (poll_thread_.joinable()) poll_thread_.join();
        if (fleet_thread_.joinable()) fleet_thread_.join();
    }

    bool ok() const { return ok_.load(); }

    double get(const std::map<std::string, double>& t, const char* key, double fallback = -1) {
        auto it = t.find(key);
        return it == t.end() ? fallback : it->second;
    }

    std::map<std::string, double> telemetry() const {
        std::lock_guard<std::mutex> lk(data_mutex_);
        return data_;
    }

    JsonValue fleet_snapshot() const {
        std::lock_guard<std::mutex> lk(fleet_mutex_);
        return fleet_;
    }

    void send(std::string path) {
        std::string h = host_;
        int p = port_;
        pool_.submit([h, p, path] {
            std::string body;
            http_get(h, p, path, body, 800);
        });
    }

    void drive(const std::string& action) { send("/" + action); }
    void stop() { send("/stop"); }
    void mode(const std::string& m) { send("/mode" + m); }
    void uv(int state) {
        static const char* paths[3] = {"/uvOff", "/uvOn", "/uvBlink"};
        send(paths[std::max(0, std::min(2, state))]);
    }
    void music(const std::string& cmd) { send("/mu" + cmd); }
    void speed(int pct) { send("/setspeed?v=" + std::to_string(pct)); }
    void motorlock(bool locked, const std::string& pw = "") {
        if (locked) send("/motorlockOn");
        else send("/motorlockOff?pw=" + pw);
    }

    // Blocking GET+parse -- used once at startup to seed initial state.
    bool get_json(const std::string& path, JsonValue& out, int timeout_ms = 1500) {
        std::string body;
        if (!http_get(host_, port_, path, body, timeout_ms)) return false;
        out = JsonValue::parse(body);
        return true;
    }

private:
    void poll_loop() {
        static const char* keys[] = {"F",   "L",   "B",  "R",   "lfl",  "lfm", "lfr", "mode",
                                      "dir", "uv",  "mt", "ms",  "lt",   "snd", "lock", "spd",
                                      "temp","hum"};
        while (running_) {
            std::string body;
            bool success = http_get(host_, port_, "/sensors", body, 1000);
            if (success) {
                JsonValue j = JsonValue::parse(body);
                std::map<std::string, double> newdata;
                for (auto k : keys) newdata[k] = j[k].as_number(-1);
                std::lock_guard<std::mutex> lk(data_mutex_);
                data_ = std::move(newdata);
            }
            ok_ = success;
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }

    void fleet_loop() {
        while (running_) {
            std::string body;
            if (http_get(host_, 5000, "/robots", body, 1500)) {
                JsonValue j = JsonValue::parse(body);
                std::lock_guard<std::mutex> lk(fleet_mutex_);
                fleet_ = std::move(j);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

    std::string host_;
    int port_;
    std::atomic<bool> running_{true};
    std::atomic<bool> ok_{false};
    mutable std::mutex data_mutex_;
    std::map<std::string, double> data_;
    mutable std::mutex fleet_mutex_;
    JsonValue fleet_;
    std::thread poll_thread_, fleet_thread_;
    ThreadPool pool_;
};

// ----------------------------------------------------------------------------
// Palette -- same as the website / Python controller (KIDA's HUD theme).
// ----------------------------------------------------------------------------
static const SDL_Color BG        = {7, 9, 15, 255};
static const SDL_Color PANEL     = {10, 14, 26, 255};
static const SDL_Color BORDER    = {30, 52, 88, 255};
static const SDL_Color TEXT_VAL  = {215, 230, 255, 255};
static const SDL_Color DIM       = {74, 95, 128, 255};
static const SDL_Color ACCENT    = {100, 150, 230, 255};
static const SDL_Color WARN      = {255, 190, 80, 255};
static const SDL_Color CRIT      = {200, 64, 64, 255};
static const SDL_Color GOOD      = {70, 215, 100, 255};
static const SDL_Color ACCENT_BG = {18, 26, 41, 255};
static const SDL_Color GOOD_BG   = {18, 46, 30, 255};
static const SDL_Color DISABLED_BORDER = {16, 22, 36, 255};
static const SDL_Color DISABLED_TEXT   = {40, 52, 74, 255};

static const int W = 420, H = 870;

// ----------------------------------------------------------------------------
// Text rendering helpers
// ----------------------------------------------------------------------------
static void draw_text(SDL_Renderer* r, TTF_Font* font, const std::string& s, int x, int y,
                       SDL_Color color, bool center_x = false) {
    if (s.empty()) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, s.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst{x, y, surf->w, surf->h};
    if (center_x) dst.x = x - surf->w / 2;
    SDL_FreeSurface(surf);
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

static void text_size(TTF_Font* font, const std::string& s, int& w, int& h) {
    if (s.empty()) { w = 0; h = TTF_FontHeight(font); return; }
    TTF_SizeText(font, s.c_str(), &w, &h);
}

static void fill_round_rect(SDL_Renderer* r, const SDL_Rect& rect, SDL_Color color) {
    // Plain rect -- good enough at this size; SDL2 has no built-in rounded
    // rect, and a hand-rolled one isn't worth it for a HUD this small.
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect_border(SDL_Renderer* r, const SDL_Rect& rect, SDL_Color color, int px = 2) {
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    for (int i = 0; i < px; i++) {
        SDL_Rect rr{rect.x + i, rect.y + i, rect.w - 2 * i, rect.h - 2 * i};
        SDL_RenderDrawRect(r, &rr);
    }
}

// ----------------------------------------------------------------------------
// Button
// ----------------------------------------------------------------------------
struct Button {
    SDL_Rect rect;
    std::string label;
    std::function<void()> cb;
    bool active = false;
    bool pressed = false;
    bool enabled = true;

    bool hit(int mx, int my) const {
        return mx >= rect.x && mx < rect.x + rect.w && my >= rect.y && my < rect.y + rect.h;
    }

    void draw(SDL_Renderer* r, TTF_Font* font) const {
        if (!enabled) {
            fill_round_rect(r, rect, PANEL);
            draw_rect_border(r, rect, DISABLED_BORDER);
            int tw, th;
            text_size(font, label, tw, th);
            draw_text(r, font, label, rect.x + rect.w / 2, rect.y + (rect.h - th) / 2,
                      DISABLED_TEXT, true);
            return;
        }
        bool hi = active || pressed;
        fill_round_rect(r, rect, hi ? ACCENT_BG : PANEL);
        draw_rect_border(r, rect, hi ? ACCENT : BORDER);
        int tw, th;
        text_size(font, label, tw, th);
        draw_text(r, font, label, rect.x + rect.w / 2, rect.y + (rect.h - th) / 2,
                  hi ? ACCENT : DIM, true);
    }
};

// ----------------------------------------------------------------------------
// Sensor bar (Front/Back/Left/Right ultrasonic readout)
// ----------------------------------------------------------------------------
static void sensor_bar(SDL_Renderer* r, TTF_Font* font, int x, int y, int w, const char* label,
                        double value, bool traveling) {
    SDL_Rect box{x, y, w, 46};
    SDL_Color color = BORDER;
    if (value > 0 && value < 15) color = CRIT;
    else if (value > 0 && value < 28) color = WARN;
    else if (traveling) color = ACCENT;
    SDL_Color bg = (traveling && color.r == ACCENT.r && color.g == ACCENT.g) ? ACCENT_BG : PANEL;

    fill_round_rect(r, box, bg);
    draw_rect_border(r, box, color);

    char buf[64];
    if (value > 0) std::snprintf(buf, sizeof(buf), "%s  %.0f cm", label, value);
    else std::snprintf(buf, sizeof(buf), "%s  --", label);
    draw_text(r, font, buf, x + 10, y + 7, TEXT_VAL);

    SDL_Rect bar_bg{x + 10, y + 32, w - 20, 5};
    fill_round_rect(r, bar_bg, {16, 22, 36, 255});
    if (value > 0) {
        double pct = std::min(value / 100.0, 1.0);
        SDL_Color fill = value < 15 ? CRIT : value < 28 ? WARN : ACCENT;
        SDL_Rect fill_rect{bar_bg.x, bar_bg.y, static_cast<int>(bar_bg.w * pct), 5};
        fill_round_rect(r, fill_rect, fill);
    }
}

// ----------------------------------------------------------------------------
// Simple single-line text field (host/port entry on the connection screen,
// and the digit-only motor-unlock password prompt).
// ----------------------------------------------------------------------------
struct TextField {
    SDL_Rect rect;
    std::string value;
    bool digits_only = false;

    void draw(SDL_Renderer* r, TTF_Font* font, bool focused, const char* mask_char = nullptr) const {
        fill_round_rect(r, rect, PANEL);
        draw_rect_border(r, rect, focused ? ACCENT : BORDER);
        std::string shown = mask_char ? std::string(value.size(), mask_char[0]) : value;
        if (shown.empty()) shown = " ";
        draw_text(r, font, shown, rect.x + 10, rect.y + rect.h / 2 - TTF_FontHeight(font) / 2,
                  TEXT_VAL);
    }
};

// ----------------------------------------------------------------------------
// Connection picker -- WiFi only in this port (see http_client.hpp).
// Returns true and fills host/port on success (after a quick reachability
// probe), false if the user quit.
// ----------------------------------------------------------------------------
static bool connection_picker(SDL_Renderer* renderer, TTF_Font* f_big, TTF_Font* f_med,
                               TTF_Font* f_sml, std::string& host, int& port) {
    TextField host_box{{40, 300, 220, 40}, host};
    TextField port_box{{270, 300, 110, 40}, std::to_string(port), true};
    SDL_Rect go_btn{40, 370, W - 80, 55};
    std::string focus = "host";
    std::string error;

    SDL_StartTextInput();

    auto try_connect = [&]() -> bool {
        int p = std::atoi(port_box.value.c_str());
        if (p <= 0) { error = "invalid port"; return false; }
        std::string body;
        if (!http_get(host_box.value, p, "/sensors", body, 2000)) {
            error = "No response - is this PC on the NORA WiFi network?";
            return false;
        }
        host = host_box.value;
        port = p;
        return true;
    };

    bool running = true, connected = false;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = false; }
            else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) { running = false; }
                else if (ev.key.keysym.sym == SDLK_RETURN) {
                    if (try_connect()) { connected = true; running = false; }
                } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                    TextField& f = (focus == "host") ? host_box : port_box;
                    if (!f.value.empty()) f.value.pop_back();
                } else if (ev.key.keysym.sym == SDLK_TAB) {
                    focus = (focus == "host") ? "port" : "host";
                }
            } else if (ev.type == SDL_TEXTINPUT) {
                TextField& f = (focus == "host") ? host_box : port_box;
                std::string in = ev.text.text;
                if (f.digits_only) {
                    for (char c : in)
                        if (std::isdigit(static_cast<unsigned char>(c))) f.value += c;
                } else {
                    f.value += in;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                int mx = ev.button.x, my = ev.button.y;
                auto inside = [](const SDL_Rect& r, int x, int y) {
                    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
                };
                if (inside(host_box.rect, mx, my)) focus = "host";
                else if (inside(port_box.rect, mx, my)) focus = "port";
                else if (inside(go_btn, mx, my)) {
                    if (try_connect()) { connected = true; running = false; }
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, BG.r, BG.g, BG.b, 255);
        SDL_RenderClear(renderer);

        int tw, th;
        text_size(f_big, "NORA", tw, th);
        draw_text(renderer, f_big, "NORA", W / 2, 30, ACCENT, true);
        draw_text(renderer, f_sml, "connect over WiFi", W / 2, 68, DIM, true);

        draw_text(renderer, f_sml, "host", host_box.rect.x, host_box.rect.y - 18, DIM);
        host_box.draw(renderer, f_med, focus == "host");
        draw_text(renderer, f_sml, "port", port_box.rect.x, port_box.rect.y - 18, DIM);
        port_box.draw(renderer, f_med, focus == "port");

        fill_round_rect(renderer, go_btn, GOOD_BG);
        draw_rect_border(renderer, go_btn, GOOD);
        text_size(f_med, "CONNECT", tw, th);
        draw_text(renderer, f_med, "CONNECT", W / 2, go_btn.y + (go_btn.h - th) / 2, GOOD, true);

        if (!error.empty()) draw_text(renderer, f_sml, error, W / 2, 440, CRIT, true);

        draw_text(renderer, f_sml, "Tab = switch field . Enter = connect . Esc = quit", W / 2,
                  H - 28, DIM, true);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_StopTextInput();
    return connected;
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::string host = "192.168.4.1";
    int port = 5002;
    bool skip_picker = false;
    std::string font_path =
#ifdef _WIN32
        "C:\\Windows\\Fonts\\consola.ttf";
#else
        "";
#endif

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--wifi") skip_picker = true;
        else if (a == "--font" && i + 1 < argc) font_path = argv[++i];
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }

    SDL_Window* window =
        SDL_CreateWindow("NORA Control", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (font_path.empty()) {
        std::fprintf(stderr,
                      "No default font path for this platform -- pass --font <path-to-ttf>\n");
        return 1;
    }
    TTF_Font* f_big = TTF_OpenFont(font_path.c_str(), 26);
    TTF_Font* f_med = TTF_OpenFont(font_path.c_str(), 15);
    TTF_Font* f_sml = TTF_OpenFont(font_path.c_str(), 13);
    if (!f_big || !f_med || !f_sml) {
        std::fprintf(stderr, "Failed to load font '%s': %s\n", font_path.c_str(), TTF_GetError());
        return 1;
    }

    if (!skip_picker) {
        if (!connection_picker(renderer, f_big, f_med, f_sml, host, port)) {
            TTF_CloseFont(f_big);
            TTF_CloseFont(f_med);
            TTF_CloseFont(f_sml);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            TTF_Quit();
            SDL_Quit();
            return 0;
        }
    }

    WifiLink link(host, port);

    // Pull current mode/speed/lock so the UI doesn't reset them to
    // "Manual"/100%/locked out from under whatever was already running.
    static const char* MODE_NAMES[4] = {"Manual", "Auto", "Line", "IR"};
    std::string mode = "Manual";
    int speed = 100;
    bool motor_locked = true;
    {
        JsonValue cal;
        if (link.get_json("/getcal", cal) && cal.contains("spd"))
            speed = std::max(0, std::min(100, cal["spd"].as_int(100)));
        JsonValue sensors;
        if (link.get_json("/sensors", sensors)) {
            int m = sensors["mode"].as_int(0);
            if (m >= 0 && m < 4) mode = MODE_NAMES[m];
            if (sensors.contains("lock")) motor_locked = sensors["lock"].as_int(1) == 1;
        }
    }

    int uv_state = 0;
    bool pw_prompt = false;
    std::string pw_buffer, pw_error;
    std::string active_drive;
    Uint32 last_repeat = 0;

    std::vector<Button> buttons;
    // add_button() hands back a pointer into this vector (&buttons.back()).
    // A push_back that triggers reallocation would silently invalidate every
    // pointer taken so far (uv_btn, lock_btn, mode_btns, dpad_btns,
    // stop_btn), so reserve enough up front that it never reallocates --
    // 16 buttons are added below, 32 is a comfortable margin.
    buttons.reserve(32);
    auto add_button = [&](SDL_Rect rect, std::string label, std::function<void()> cb,
                           bool active = false, bool enabled = true) -> Button* {
        buttons.push_back(Button{rect, std::move(label), std::move(cb), active, false, enabled});
        return &buttons.back();
    };

    std::vector<Button*> mode_btns;  // Manual / Auto / Line (Remote/IR is keyboard-only, same as
                                      // the website -- no 4th button)
    std::map<std::string, Button*> dpad_btns;
    Button* uv_btn = nullptr;
    Button* lock_btn = nullptr;
    Button* stop_btn = nullptr;

    auto update_dpad_enabled = [&] {
        bool enabled = (mode == "Manual") && !motor_locked;
        for (auto& kv : dpad_btns) kv.second->enabled = enabled;
        if (stop_btn) stop_btn->enabled = enabled;
    };

    auto set_mode_ui = [&](const std::string& m) {
        mode = m;
        for (auto* b : mode_btns) {
            // labels are exactly "MANUAL"/"AUTO"/"LINE"
            std::string want = (b->label == "MANUAL") ? "Manual" : (b->label == "AUTO") ? "Auto" : "Line";
            b->active = (want == m);
        }
        update_dpad_enabled();
    };
    auto set_mode = [&](const std::string& m) {
        link.mode(m);
        set_mode_ui(m);
    };

    mode_btns.push_back(add_button({20, 410, 120, 38}, "MANUAL", [&] { set_mode("Manual"); },
                                    mode == "Manual"));
    mode_btns.push_back(
        add_button({150, 410, 120, 38}, "AUTO", [&] { set_mode("Auto"); }, mode == "Auto"));
    mode_btns.push_back(
        add_button({280, 410, 120, 38}, "LINE", [&] { set_mode("Line"); }, mode == "Line"));

    static const char* UV_LABELS[3] = {"UV OFF", "UV ON", "UV BLINK"};
    auto cycle_uv = [&] {
        uv_state = (uv_state + 1) % 3;
        link.uv(uv_state);
        uv_btn->label = UV_LABELS[uv_state];
        uv_btn->active = uv_state != 0;
    };
    uv_btn = add_button({20, 458, 120, 38}, "UV OFF", cycle_uv);

    auto apply_lock_ui = [&] {
        lock_btn->label = motor_locked ? "MOTOR LOCK: ON" : "MOTOR LOCK: OFF";
        lock_btn->active = motor_locked;
        update_dpad_enabled();
    };
    auto toggle_lock = [&] {
        if (motor_locked) {
            pw_prompt = true;
            pw_buffer.clear();
            pw_error.clear();
        } else {
            motor_locked = true;
            link.motorlock(true);
            apply_lock_ui();
        }
    };
    lock_btn = add_button({20, 370, W - 40, 32}, motor_locked ? "MOTOR LOCK: ON" : "MOTOR LOCK: OFF",
                           toggle_lock, motor_locked);

    add_button({150, 458, 60, 38}, "|<", [&] { link.music("Prev"); });
    add_button({215, 458, 60, 38}, ">||", [&] { link.music("Play"); });
    add_button({280, 458, 60, 38}, ">|", [&] { link.music("Next"); });
    add_button({345, 458, 55, 38}, "[]", [&] { link.music("Stop"); });

    auto start_drive = [&](const std::string& action) {
        active_drive = action;
        link.drive(action);
    };
    auto stop_drive = [&] {
        if (!active_drive.empty()) {
            active_drive.clear();
            link.stop();
        }
    };

    int pad_cx = W / 2, pad_cy = 660, bs = 80, gap = 8;
    struct DpadDef { const char* action; int cx, cy; const char* label; };
    std::vector<DpadDef> dpad_defs = {
        {"fw", pad_cx, pad_cy - bs - gap, "^"},
        {"bw", pad_cx, pad_cy + gap + bs, "v"},
        {"left", pad_cx - bs - gap, pad_cy + gap + bs, "<"},
        {"right", pad_cx + gap + bs, pad_cy + gap + bs, ">"},
        {"turnL", pad_cx - bs - gap, pad_cy, "@<"},
        {"turnR", pad_cx + gap + bs, pad_cy, ">@"},
    };
    bool dpad_enabled_init = (mode == "Manual") && !motor_locked;
    for (auto& d : dpad_defs) {
        SDL_Rect rect{d.cx - bs / 2, d.cy - bs / 2, bs, bs};
        std::string action = d.action;
        dpad_btns[action] = add_button(rect, d.label, [&, action] { start_drive(action); }, false,
                                        dpad_enabled_init);
    }
    stop_btn = add_button({pad_cx - bs / 2, pad_cy - bs / 2, bs, bs}, "STOP", [&] { link.stop(); },
                           false, dpad_enabled_init);

    static const std::map<int, std::string> KEY_DRIVE = {
        {SDLK_UP, "fw"},   {SDLK_DOWN, "bw"},   {SDLK_LEFT, "left"}, {SDLK_RIGHT, "right"},
        {SDLK_w, "fw"},    {SDLK_s, "bw"},      {SDLK_a, "left"},    {SDLK_d, "right"},
        {SDLK_q, "turnL"}, {SDLK_e, "turnR"},
    };

    static const int SPEED_PRESETS[4] = {25, 50, 75, 100};
    auto cycle_speed = [&] {
        int idx = 0;
        int best_diff = std::abs(SPEED_PRESETS[0] - speed);
        for (int i = 1; i < 4; i++) {
            int diff = std::abs(SPEED_PRESETS[i] - speed);
            if (diff < best_diff) { best_diff = diff; idx = i; }
        }
        idx = (idx + 1) % 4;
        speed = SPEED_PRESETS[idx];
        link.speed(speed);
    };

    SDL_Rect slider{70, 515, W - 140, 8};
    bool dragging_slider = false;
    auto set_speed_from_mouse = [&](int mx) {
        int pct = static_cast<int>(std::round((mx - slider.x) / static_cast<double>(slider.w) * 100.0));
        speed = std::max(0, std::min(100, pct));
        link.speed(speed);
    };

    bool running = true;
    while (running) {
        Uint32 now = SDL_GetTicks();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;

            else if (ev.type == SDL_KEYDOWN) {
                if (pw_prompt) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) pw_prompt = false;
                    else if (ev.key.keysym.sym == SDLK_RETURN) {
                        if (pw_buffer == "1234") {
                            motor_locked = false;
                            link.motorlock(false, pw_buffer);
                            apply_lock_ui();
                            pw_prompt = false;
                            pw_error.clear();
                        } else {
                            pw_error = "Wrong password";
                            pw_buffer.clear();
                        }
                    } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        if (!pw_buffer.empty()) pw_buffer.pop_back();
                    }
                    continue;
                }

                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) { running = false; }
                else if (k == SDLK_SPACE) { link.music("Play"); }
                else if (k == SDLK_1) { set_mode("Manual"); }
                else if (k == SDLK_2) { set_mode("Auto"); }
                else if (k == SDLK_3) { set_mode("Line"); }
                else if (k == SDLK_4) { set_mode("IR"); }
                else if (k == SDLK_u) { cycle_uv(); }
                else if (k == SDLK_m) { link.music("Next"); }
                else if (k == SDLK_x) { cycle_speed(); }
                else if (mode == "Manual" && KEY_DRIVE.count(k)) {
                    start_drive(KEY_DRIVE.at(k));
                }
            } else if (ev.type == SDL_TEXTINPUT) {
                if (pw_prompt) {
                    for (char c : std::string(ev.text.text))
                        if (std::isdigit(static_cast<unsigned char>(c))) pw_buffer += c;
                }
            } else if (ev.type == SDL_KEYUP) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (KEY_DRIVE.count(k) && active_drive == KEY_DRIVE.at(k)) stop_drive();
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                int mx = ev.button.x, my = ev.button.y;
                if (pw_prompt) continue;
                SDL_Rect infl{slider.x, slider.y - 10, slider.w, slider.h + 20};
                if (mx >= infl.x && mx < infl.x + infl.w && my >= infl.y && my < infl.y + infl.h) {
                    dragging_slider = true;
                    set_speed_from_mouse(mx);
                }
                for (auto& b : buttons) {
                    if (b.enabled && b.hit(mx, my)) {
                        b.pressed = true;
                        b.cb();
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP) {
                dragging_slider = false;
                for (auto& b : buttons) b.pressed = false;
                if (!active_drive.empty()) stop_drive();
            } else if (ev.type == SDL_MOUSEMOTION && dragging_slider) {
                set_speed_from_mouse(ev.motion.x);
            }
        }

        SDL_StartTextInput();  // harmless if already active; needed for the pw prompt's digits

        // repeat held drive command every 150ms, matching the website/pygame
        if (!active_drive.empty() && mode == "Manual" && now - last_repeat > 150) {
            link.drive(active_drive);
            last_repeat = now;
        }

        // ---- draw ----
        SDL_SetRenderDrawColor(renderer, BG.r, BG.g, BG.b, 255);
        SDL_RenderClear(renderer);

        auto t = link.telemetry();

        // Reflect state that changed elsewhere (website, IR remote) instead
        // of showing something stale -- update UI only, never re-send.
        if (t.count("mode")) {
            int m = static_cast<int>(t["mode"]);
            if (m >= 0 && m < 4 && MODE_NAMES[m] != mode) set_mode_ui(MODE_NAMES[m]);
        }
        if (t.count("uv") && static_cast<int>(t["uv"]) != uv_state) {
            uv_state = static_cast<int>(t["uv"]);
            uv_btn->label = UV_LABELS[uv_state];
            uv_btn->active = uv_state != 0;
        }
        if (t.count("spd") && static_cast<int>(t["spd"]) != speed) {
            speed = static_cast<int>(t["spd"]);
        }
        if (t.count("lock") && !pw_prompt) {
            bool srv_locked = static_cast<int>(t["lock"]) == 1;
            if (srv_locked != motor_locked) {
                motor_locked = srv_locked;
                apply_lock_ui();
            }
        }

        draw_text(renderer, f_big, "NORA", W / 2, 14, ACCENT, true);
        std::string status_line =
            std::string("WiFi ") + host + ":" + std::to_string(port) + (link.ok() ? "   connected" : "   OFFLINE");
        draw_text(renderer, f_sml, status_line, W / 2, 46, link.ok() ? GOOD : CRIT, true);

        int auto_dir = (t.count("mode") && static_cast<int>(t["mode"]) == 1 && t.count("dir"))
                           ? static_cast<int>(t["dir"])
                           : -1;

        int colw = (W - 50) / 2;
        sensor_bar(renderer, f_sml, 20, 75, colw, "Front", link.get(t, "F"), auto_dir == 0);
        sensor_bar(renderer, f_sml, 30 + colw, 75, colw, "Back", link.get(t, "B"), auto_dir == 2);
        sensor_bar(renderer, f_sml, 20, 130, colw, "Left", link.get(t, "L"), auto_dir == 3);
        sensor_bar(renderer, f_sml, 30 + colw, 130, colw, "Right", link.get(t, "R"), auto_dir == 1);

        int y = 195;
        draw_text(renderer, f_sml, "Line:", 20, y + 3, DIM);
        for (int i = 0; i < 3; i++) {
            const char* key = i == 0 ? "lfl" : i == 1 ? "lfm" : "lfr";
            bool on = link.get(t, key, 0) == 1;
            SDL_Rect dot{75 + i * 30 - 10, y, 20, 20};
            fill_round_rect(renderer, dot, on ? ACCENT : PANEL);
            draw_rect_border(renderer, dot, on ? ACCENT : BORDER);
        }
        double lt = link.get(t, "lt");
        char light_buf[32];
        if (lt >= 0) std::snprintf(light_buf, sizeof(light_buf), "Light: %d%%", static_cast<int>(lt));
        else std::snprintf(light_buf, sizeof(light_buf), "Light: --");
        draw_text(renderer, f_sml, light_buf, 180, y + 3, TEXT_VAL);

        bool snd = link.get(t, "snd", 0) == 1;
        SDL_Rect snd_dot{290, y, 20, 20};
        fill_round_rect(renderer, snd_dot, snd ? GOOD : PANEL);
        draw_rect_border(renderer, snd_dot, snd ? GOOD : BORDER);
        draw_text(renderer, f_sml, "sound", 315, y + 3, DIM);

        // temp/humidity (AHT10) -- not in the original Python port, added
        // here since the ESP32 exposes it in /sensors already
        double temp = link.get(t, "temp"), hum = link.get(t, "hum");
        char env_buf[64];
        std::snprintf(env_buf, sizeof(env_buf), "Temp: %s   Hum: %s",
                      temp >= 0 ? (std::to_string(static_cast<int>(temp)) + "C").c_str() : "--",
                      hum >= 0 ? (std::to_string(static_cast<int>(hum)) + "%").c_str() : "--");
        draw_text(renderer, f_sml, env_buf, 20, y + 24, TEXT_VAL);

        int mt = static_cast<int>(link.get(t, "mt", 0)), ms = static_cast<int>(link.get(t, "ms", 0));
        static const char* MS_LABELS[3] = {"stopped", "playing", "paused"};
        SDL_Color ms_col = ms == 1 ? GOOD : ms == 2 ? WARN : DIM;
        char track_buf[32];
        std::snprintf(track_buf, sizeof(track_buf), "MUSIC   track%03d", mt);
        draw_text(renderer, f_sml, track_buf, 20, 260, ACCENT);
        draw_text(renderer, f_sml, (ms >= 0 && ms < 3) ? MS_LABELS[ms] : "?", 170, 260, ms_col);

        // fleet panel
        int fy = 288;
        draw_text(renderer, f_sml, "FLEET", 20, fy, DIM);
        JsonValue fleet = link.fleet_snapshot();
        std::string authority = fleet.contains("authority") ? fleet["authority"].as_string("NORA") : "NORA";
        bool is_self = authority == "NORA";
        draw_text(renderer, f_sml, is_self ? "authority: NORA (self)" : "authority: " + authority,
                  75, fy, is_self ? ACCENT : WARN);

        const JsonValue& robots = fleet["robots"];
        int row_y = fy + 18, shown = 0;
        for (size_t i = 0; i < robots.size() && shown < 3; i++) {
            const JsonValue& rbt = robots[i];
            if (rbt["name"].as_string() == "NORA") continue;
            std::string line = "- " + rbt["name"].as_string("?") + " (" + rbt["type"].as_string("?") + ")";
            draw_text(renderer, f_sml, line, 20, row_y, TEXT_VAL);
            row_y += 16;
            shown++;
        }
        if (shown == 0) draw_text(renderer, f_sml, "(no other robots registered)", 20, row_y, DIM);

        // speed slider
        draw_text(renderer, f_sml, "Speed", 20, 508, DIM);
        fill_round_rect(renderer, slider, {16, 22, 36, 255});
        SDL_Rect fill{slider.x, slider.y, static_cast<int>(slider.w * speed / 100.0), slider.h};
        fill_round_rect(renderer, fill, ACCENT);
        int knob_x = slider.x + static_cast<int>(slider.w * speed / 100.0);
        SDL_Rect knob{knob_x - 9, slider.y - 5, 18, 18};
        fill_round_rect(renderer, knob, ACCENT);
        draw_text(renderer, f_med, std::to_string(speed) + "%", W - 60, 415, ACCENT);

        for (auto& b : buttons) b.draw(renderer, f_med);
        if (!active_drive.empty() && dpad_btns.count(active_drive)) {
            draw_rect_border(renderer, dpad_btns[active_drive]->rect, ACCENT, 3);
        }

        draw_text(renderer, f_sml,
                  "WASD/arrows drive . Q/E turn . space play/pause . M next . X speed . U uv . 1-4 mode",
                  W / 2, H - 28, DIM, true);

        if (pw_prompt) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
            SDL_Rect overlay{0, 0, W, H};
            SDL_RenderFillRect(renderer, &overlay);

            SDL_Rect box{40, H / 2 - 70, W - 80, 140};
            fill_round_rect(renderer, box, PANEL);
            draw_rect_border(renderer, box, ACCENT);
            draw_text(renderer, f_med, "Enter password to unlock motors", W / 2, box.y + 16, TEXT_VAL, true);
            std::string masked(pw_buffer.size(), '*');
            draw_text(renderer, f_big, masked.empty() ? " " : masked, W / 2, box.y + 46, ACCENT, true);
            if (!pw_error.empty()) draw_text(renderer, f_sml, pw_error, W / 2, box.y + 88, CRIT, true);
            draw_text(renderer, f_sml, "Enter = confirm . Esc = cancel", W / 2, box.y + 114, DIM, true);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    stop_drive();
    TTF_CloseFont(f_big);
    TTF_CloseFont(f_med);
    TTF_CloseFont(f_sml);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
