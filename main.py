 
#!/usr/bin/env python3
"""
NORA desktop controller (pygame) — mirrors the web dashboard.

USAGE
  python3 nora_controller.py
  -> a connection screen lets you choose WiFi or Bluetooth and edit the
     address before connecting. Command-line args skip the screen:

  WiFi directly:       python3 nora_controller.py --wifi
                       python3 nora_controller.py --host 192.168.4.1 --port 5002 --wifi
  Bluetooth directly:  python3 nora_controller.py --bt /dev/rfcomm0   (Linux)
                       python  nora_controller.py --bt COM5           (Windows)

  Bluetooth needs: PC with a BT adapter, paired with "NORA", and a serial
  port bound to it (Linux: sudo rfcomm bind 0 <NORA_MAC>). If your PC has
  no Bluetooth, just use WiFi — connect the PC to the NORA network first.

INSTALL
  pip install pygame requests            (pyserial too, only for Bluetooth)

CONTROLS
  Arrow keys  drive (fwd/back/strafe)     A / D   turn left / right
  Space       stop                        1/2/3   manual / auto / line mode
  U           cycle UV                    M/N/P/X music play-pause/next/prev/stop
  +/-         speed up / down             Esc     quit
  Everything is also clickable with the mouse.
"""

import argparse
import concurrent.futures
import sys
import threading
import time

import pygame

# ----------------------------------------------------------------------------
# Transports
# ----------------------------------------------------------------------------

class WifiLink:
    """Talks to the ESP32 web server. Full telemetry via /sensors."""

    def __init__(self, host, port):
        import requests
        self.requests = requests
        self.base = f"http://{host}:{port}"
        self.ok = False
        self.data = {}
        self._lock = threading.Lock()
        # commands fire every ~150ms while a key/button is held; a small
        # pool avoids spawning a fresh OS thread for every single one
        self._pool = concurrent.futures.ThreadPoolExecutor(
            max_workers=2, thread_name_prefix="wifi-cmd")
        threading.Thread(target=self._poll, daemon=True).start()

    def _get(self, path):
        try:
            self.requests.get(self.base + path, timeout=1.0)
            self.ok = True
        except Exception:
            self.ok = False

    def get_json(self, path, timeout=1.5):
        """Blocking GET returning parsed JSON, or None on failure."""
        try:
            r = self.requests.get(self.base + path, timeout=timeout)
            return r.json()
        except Exception:
            return None

    def _poll(self):
        while True:
            try:
                r = self.requests.get(self.base + "/sensors", timeout=1.0)
                with self._lock:
                    self.data = r.json()
                self.ok = True
            except Exception:
                self.ok = False
            time.sleep(0.4)

    def telemetry(self):
        with self._lock:
            return dict(self.data)

    # driving -----------------------------------------------------------
    def drive(self, action):   # 'fw' 'bw' 'left' 'right' 'turnL' 'turnR'
        self._pool.submit(self._get, "/" + action)

    def stop(self):
        self._pool.submit(self._get, "/stop")

    # everything else ----------------------------------------------------
    def mode(self, m):         # 'Manual' 'Auto' 'Line'
        self._pool.submit(self._get, "/mode" + m)

    def uv(self, state):       # 0 1 2
        path = ["/uvOff", "/uvOn", "/uvBlink"][state]
        self._pool.submit(self._get, path)

    def music(self, cmd):      # 'Play' 'Next' 'Prev' 'Stop'
        self._pool.submit(self._get, "/mu" + cmd)

    def speed(self, pct):
        self._pool.submit(self._get, f"/setspeed?v={pct}")


class BtLink:
    """Talks over a Bluetooth serial port using the single-char protocol."""

    DRIVE = {"fw": "F", "bw": "B", "left": "L", "right": "R",
             "turnL": "Q", "turnR": "E"}

    def __init__(self, port):
        import serial   # only needed for Bluetooth; pip install pyserial
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self.ok = True
        self.data = {}
        self._lock = threading.Lock()
        # guards the serial port itself: the poll thread and drive commands
        # from the main thread both read/write it, and pyserial doesn't
        # promise that's safe without serializing the calls
        self._io_lock = threading.Lock()
        threading.Thread(target=self._poll, daemon=True).start()

    def _send(self, ch):
        try:
            with self._io_lock:
                self.ser.write(ch.encode())
            self.ok = True
        except Exception:
            self.ok = False

    def _poll(self):
        """Ask for status with '?' and parse the reply line."""
        buf = ""
        while True:
            self._send("?")
            time.sleep(0.1)
            try:
                with self._io_lock:
                    chunk = self.ser.read(256)
                buf += chunk.decode(errors="ignore")
            except Exception:
                self.ok = False
                time.sleep(1)
                continue
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                self._parse(line.strip())
            time.sleep(0.6)

    def _parse(self, line):
        # "F:12.0 L:33.0 B:-1.0 R:8.5 mode:1 dir:0 lf:010 track:101 state:1 light:45% sound:no"
        if not line.startswith("F:"):
            return
        d = {}
        for tok in line.split():
            if ":" not in tok:
                continue
            k, v = tok.split(":", 1)
            if k == "lf":
                if len(v) == 3 and v.isdigit():
                    d["lfl"], d["lfm"], d["lfr"] = int(v[0]), int(v[1]), int(v[2])
                continue
            if k == "sound":
                d["snd"] = 1 if v == "yes" else 0
                continue
            v = v.rstrip("%")
            try:
                d[{"F": "F", "L": "L", "B": "B", "R": "R", "mode": "mode", "dir": "dir",
                   "track": "mt", "state": "ms", "light": "lt"}[k]] = float(v)
            except (KeyError, ValueError):
                pass
        with self._lock:
            self.data.update(d)

    def telemetry(self):
        with self._lock:
            return dict(self.data)

    def drive(self, action):
        self._send(self.DRIVE[action])

    def stop(self):
        self._send("S")

    def mode(self, m):
        self._send({"Manual": "1", "Auto": "2", "Line": "3"}[m])

    def uv(self, state):
        self._send("U")     # BT protocol cycles; state arg unused

    def music(self, cmd):
        self._send({"Play": "M", "Next": "N", "Prev": "P", "Stop": "X"}[cmd])

    def speed(self, pct):
        pass  # BT protocol has no "set absolute speed" command; slider is WiFi-only

    def nudge_speed(self, up):
        """BT's only speed control: a single '+'/'-' step handled onboard."""
        self._send("+" if up else "-")


# ----------------------------------------------------------------------------
# UI
# ----------------------------------------------------------------------------

BG      = (17, 17, 17)
PANEL   = (30, 30, 30)
BORDER  = (51, 51, 51)
TEXT    = (238, 238, 238)
DIM     = (102, 102, 102)
ACCENT  = (0, 170, 255)
WARN    = (255, 153, 0)
CRIT    = (255, 68, 68)
GOOD    = (0, 255, 136)

W, H = 420, 780


class Button:
    def __init__(self, rect, label, cb, font, active=False, enabled=True):
        self.rect = pygame.Rect(rect)
        self.label = label
        self.cb = cb
        self.font = font
        self.active = active
        self.pressed = False
        self.enabled = enabled

    def draw(self, surf):
        if not self.enabled:
            pygame.draw.rect(surf, PANEL, self.rect, border_radius=8)
            pygame.draw.rect(surf, (40, 40, 40), self.rect, 2, border_radius=8)
            txt = self.font.render(self.label, True, (60, 60, 60))
            surf.blit(txt, txt.get_rect(center=self.rect.center))
            return
        color = ACCENT if (self.active or self.pressed) else BORDER
        bg = (0, 26, 42) if (self.active or self.pressed) else PANEL
        pygame.draw.rect(surf, bg, self.rect, border_radius=8)
        pygame.draw.rect(surf, color, self.rect, 2, border_radius=8)
        txt = self.font.render(self.label, True,
                               ACCENT if (self.active or self.pressed) else (170, 170, 170))
        surf.blit(txt, txt.get_rect(center=self.rect.center))

    def hit(self, pos):
        return self.rect.collidepoint(pos)


def sensor_bar(surf, font, x, y, w, label, value, traveling=False):
    box = pygame.Rect(x, y, w, 46)
    color = BORDER
    if 0 < value < 15:
        color = CRIT
    elif 0 < value < 28:
        color = WARN
    elif traveling:
        color = ACCENT
    bg = (0, 26, 42) if (traveling and color == ACCENT) else PANEL
    pygame.draw.rect(surf, bg, box, border_radius=8)
    pygame.draw.rect(surf, color, box, 2, border_radius=8)
    txt = font.render(f"{label}  {value:.0f} cm" if value > 0 else f"{label}  --",
                      True, TEXT)
    surf.blit(txt, (x + 10, y + 7))
    # distance bar
    bar_bg = pygame.Rect(x + 10, y + 32, w - 20, 5)
    pygame.draw.rect(surf, (42, 42, 42), bar_bg, border_radius=3)
    if value > 0:
        pct = min(value / 100.0, 1.0)
        fill = CRIT if value < 15 else WARN if value < 28 else ACCENT
        pygame.draw.rect(surf, fill,
                         (bar_bg.x, bar_bg.y, int(bar_bg.w * pct), 5), border_radius=3)


def connection_picker(screen, clock, f_big, f_med, f_sml, host, port, btport):
    """Startup screen: choose WiFi or Bluetooth, edit address, connect.
    Returns (link, transport_label) or (None, None) if the user quit."""
    fields = {"host": host, "port": str(port), "btport": btport}
    focus = None          # which field is being typed into
    choice = "wifi"       # 'wifi' or 'bt'
    error = ""

    wifi_btn  = pygame.Rect(40, 120, W - 80, 60)
    bt_btn    = pygame.Rect(40, 195, W - 80, 60)
    host_box  = pygame.Rect(40, 300, 220, 40)
    port_box  = pygame.Rect(270, 300, 110, 40)
    btp_box   = pygame.Rect(40, 300, W - 80, 40)
    go_btn    = pygame.Rect(40, 370, W - 80, 55)

    def try_connect():
        nonlocal error
        try:
            if choice == "wifi":
                link = WifiLink(fields["host"], int(fields["port"]))
                # quick reachability probe so bad addresses fail here, not later
                try:
                    link.requests.get(link.base + "/sensors", timeout=2.0)
                except Exception:
                    error = "No response — is this PC on the NORA WiFi network?"
                    return None, None
                return link, f"WiFi {fields['host']}:{fields['port']}"
            else:
                link = BtLink(fields["btport"])
                return link, "BT " + fields["btport"]
        except ImportError:
            missing = "pyserial" if choice == "bt" else "requests"
            error = f"{missing} not installed:  pip install {missing}"
        except Exception as e:
            label = "Bluetooth" if choice == "bt" else "WiFi"
            error = f"{label} failed: {type(e).__name__}: {e}"[:60]
        return None, None

    while True:
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                return None, None
            elif ev.type == pygame.KEYDOWN:
                if ev.key == pygame.K_ESCAPE:
                    return None, None
                elif ev.key == pygame.K_RETURN:
                    link, label = try_connect()
                    if link:
                        return link, label
                elif focus and ev.key == pygame.K_BACKSPACE:
                    fields[focus] = fields[focus][:-1]
                elif focus and ev.unicode and ev.unicode.isprintable():
                    fields[focus] += ev.unicode
            elif ev.type == pygame.MOUSEBUTTONDOWN:
                focus = None
                if wifi_btn.collidepoint(ev.pos):
                    choice = "wifi"; error = ""
                elif bt_btn.collidepoint(ev.pos):
                    choice = "bt"; error = ""
                elif choice == "wifi" and host_box.collidepoint(ev.pos):
                    focus = "host"
                elif choice == "wifi" and port_box.collidepoint(ev.pos):
                    focus = "port"
                elif choice == "bt" and btp_box.collidepoint(ev.pos):
                    focus = "btport"
                elif go_btn.collidepoint(ev.pos):
                    link, label = try_connect()
                    if link:
                        return link, label

        screen.fill(BG)
        title = f_big.render("NORA", True, ACCENT)
        screen.blit(title, title.get_rect(centerx=W // 2, y=30))
        sub = f_sml.render("choose a connection", True, DIM)
        screen.blit(sub, sub.get_rect(centerx=W // 2, y=68))

        for rect, key, label, note in (
            (wifi_btn, "wifi", "WiFi",
             "PC joins the NORA network — full dashboard"),
            (bt_btn, "bt", "Bluetooth",
             "needs a BT adapter + paired serial port"),
        ):
            sel = choice == key
            pygame.draw.rect(screen, (0, 26, 42) if sel else PANEL, rect,
                             border_radius=10)
            pygame.draw.rect(screen, ACCENT if sel else BORDER, rect, 2,
                             border_radius=10)
            screen.blit(f_med.render(label, True, ACCENT if sel else TEXT),
                        (rect.x + 16, rect.y + 8))
            screen.blit(f_sml.render(note, True, DIM),
                        (rect.x + 16, rect.y + 32))

        # address fields
        if choice == "wifi":
            for box, key, label in ((host_box, "host", "host"),
                                    (port_box, "port", "port")):
                pygame.draw.rect(screen, PANEL, box, border_radius=6)
                pygame.draw.rect(screen, ACCENT if focus == key else BORDER,
                                 box, 2, border_radius=6)
                screen.blit(f_sml.render(label, True, DIM), (box.x, box.y - 18))
                screen.blit(f_med.render(fields[key], True, TEXT),
                            (box.x + 10, box.y + 10))
        else:
            pygame.draw.rect(screen, PANEL, btp_box, border_radius=6)
            pygame.draw.rect(screen, ACCENT if focus == "btport" else BORDER,
                             btp_box, 2, border_radius=6)
            screen.blit(f_sml.render("serial port (e.g. /dev/rfcomm0 or COM5)",
                                     True, DIM), (btp_box.x, btp_box.y - 18))
            screen.blit(f_med.render(fields["btport"], True, TEXT),
                        (btp_box.x + 10, btp_box.y + 10))

        pygame.draw.rect(screen, (0, 60, 30), go_btn, border_radius=10)
        pygame.draw.rect(screen, GOOD, go_btn, 2, border_radius=10)
        go = f_med.render("CONNECT", True, GOOD)
        screen.blit(go, go.get_rect(center=go_btn.center))

        if error:
            err = f_sml.render(error, True, CRIT)
            screen.blit(err, err.get_rect(centerx=W // 2, y=440))

        hint = f_sml.render("click a field to edit · Enter = connect · Esc = quit",
                            True, DIM)
        screen.blit(hint, hint.get_rect(centerx=W // 2, y=H - 28))

        pygame.display.flip()
        clock.tick(60)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--port", type=int, default=5002)
    ap.add_argument("--wifi", action="store_true",
                    help="skip the picker and connect over WiFi")
    ap.add_argument("--bt", metavar="SERIAL_PORT",
                    help="skip the picker and connect over this serial port")
    args = ap.parse_args()

    pygame.init()
    screen = pygame.display.set_mode((W, H))
    pygame.display.set_caption("NORA Control")
    clock = pygame.time.Clock()
    f_big = pygame.font.SysFont("dejavusans", 26, bold=True)
    f_med = pygame.font.SysFont("dejavusans", 15, bold=True)
    f_sml = pygame.font.SysFont("dejavusans", 13)

    if args.bt:
        try:
            link, transport = BtLink(args.bt), "BT " + args.bt
        except ImportError:
            print("pyserial not installed:  pip install pyserial", file=sys.stderr)
            pygame.quit(); sys.exit(1)
        except Exception as e:
            print(f"Bluetooth connect failed: {type(e).__name__}: {e}", file=sys.stderr)
            pygame.quit(); sys.exit(1)
    elif args.wifi:
        try:
            link, transport = WifiLink(args.host, args.port), \
                              f"WiFi {args.host}:{args.port}"
        except ImportError:
            print("requests not installed:  pip install requests", file=sys.stderr)
            pygame.quit(); sys.exit(1)
    else:
        link, transport = connection_picker(screen, clock, f_big, f_med,
                                            f_sml, args.host, args.port,
                                            "/dev/rfcomm0")
        if link is None:
            pygame.quit()
            sys.exit(0)

    # pull current mode/speed from the robot so the UI doesn't reset them
    # to "Manual"/100% out from under whatever was already running
    init_mode, init_speed = "Manual", 100
    if isinstance(link, WifiLink):
        cal = link.get_json("/getcal")
        if cal and "spd" in cal:
            init_speed = max(0, min(100, int(cal["spd"])))
        sensors = link.get_json("/sensors")
        mode_names = ["Manual", "Auto", "Line"]
        if sensors and int(sensors.get("mode", 0)) in (0, 1, 2):
            init_mode = mode_names[int(sensors["mode"])]

    mode = init_mode
    uv_state = 0
    speed = init_speed
    active_drive = None       # current held drive action
    last_repeat = 0

    buttons = []

    def add(rect, label, cb, active=False, enabled=True):
        b = Button(rect, label, cb, f_med, active, enabled)
        buttons.append(b)
        return b

    # mode buttons
    def set_mode(m):
        nonlocal mode
        mode = m
        link.mode(m)
        for b, name in zip(mode_btns, ("Manual", "Auto", "Line")):
            b.active = (name == m)
        is_manual = (m == "Manual")
        for db in dpad_btns.values():
            db.enabled = is_manual
        stop_btn.enabled = is_manual

    mode_btns = [
        add((20, 320, 120, 38), "MANUAL", lambda: set_mode("Manual"), init_mode == "Manual"),
        add((150, 320, 120, 38), "AUTO", lambda: set_mode("Auto"), init_mode == "Auto"),
        add((280, 320, 120, 38), "LINE", lambda: set_mode("Line"), init_mode == "Line"),
    ]

    def cycle_uv():
        nonlocal uv_state
        uv_state = (uv_state + 1) % 3
        link.uv(uv_state)
        uv_btn.label = ["UV OFF", "UV ON", "UV BLINK"][uv_state]
        uv_btn.active = uv_state != 0

    uv_btn = add((20, 368, 120, 38), "UV OFF", cycle_uv)

    # music
    add((150, 368, 60, 38), "|<", lambda: link.music("Prev"))
    add((215, 368, 60, 38), ">||", lambda: link.music("Play"))
    add((280, 368, 60, 38), ">|", lambda: link.music("Next"))
    add((345, 368, 55, 38), "[]", lambda: link.music("Stop"))

    # dpad
    def start_drive(action):
        nonlocal active_drive
        active_drive = action
        link.drive(action)

    def stop_drive():
        nonlocal active_drive
        if active_drive:
            active_drive = None
            link.stop()

    pad_cx, pad_cy, bs, gap = W // 2, 570, 80, 8
    dpad_defs = [
        ("fw",    (pad_cx - bs // 2, pad_cy - bs - gap - bs // 2), "^"),
        ("bw",    (pad_cx - bs // 2, pad_cy + gap + bs // 2), "v"),
        ("left",  (pad_cx - bs - gap - bs // 2, pad_cy + gap + bs // 2), "<"),
        ("right", (pad_cx + gap + bs // 2, pad_cy + gap + bs // 2), ">"),
        ("turnL", (pad_cx - bs - gap - bs // 2, pad_cy - bs // 2), "@<"),
        ("turnR", (pad_cx + gap + bs // 2, pad_cy - bs // 2), ">@"),
    ]
    dpad_btns = {}
    for action, (bx, by), lbl in dpad_defs:
        dpad_btns[action] = add((bx, by, bs, bs), lbl,
                                lambda a=action: start_drive(a),
                                enabled=(init_mode == "Manual"))
    stop_btn = add((pad_cx - bs // 2, pad_cy - bs // 2, bs, bs), "STOP",
                   lambda: link.stop(), enabled=(init_mode == "Manual"))

    key_drive = {
        pygame.K_UP: "fw", pygame.K_DOWN: "bw",
        pygame.K_LEFT: "left", pygame.K_RIGHT: "right",
        pygame.K_a: "turnL", pygame.K_d: "turnR",
    }

    slider = pygame.Rect(70, 425, W - 140, 8)
    dragging_slider = False

    is_bt = isinstance(link, BtLink)   # BT has no absolute-speed command; slider is WiFi-only

    def set_speed_from_mouse(mx):
        nonlocal speed
        pct = round((mx - slider.x) / slider.w * 100)
        speed = max(0, min(100, pct))
        link.speed(speed)

    running = True
    while running:
        now = time.time()
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                running = False

            elif ev.type == pygame.KEYDOWN:
                if ev.key == pygame.K_ESCAPE:
                    running = False
                elif ev.key in key_drive and mode == "Manual":
                    start_drive(key_drive[ev.key])
                elif ev.key == pygame.K_SPACE:
                    stop_drive(); link.stop()
                elif ev.key == pygame.K_1: set_mode("Manual")
                elif ev.key == pygame.K_2: set_mode("Auto")
                elif ev.key == pygame.K_3: set_mode("Line")
                elif ev.key == pygame.K_u: cycle_uv()
                elif ev.key == pygame.K_m: link.music("Play")
                elif ev.key == pygame.K_n: link.music("Next")
                elif ev.key == pygame.K_p: link.music("Prev")
                elif ev.key == pygame.K_x: link.music("Stop")
                elif ev.key in (pygame.K_PLUS, pygame.K_EQUALS, pygame.K_KP_PLUS):
                    speed = min(100, speed + 10)
                    link.nudge_speed(True) if is_bt else link.speed(speed)
                elif ev.key in (pygame.K_MINUS, pygame.K_KP_MINUS):
                    speed = max(0, speed - 10)
                    link.nudge_speed(False) if is_bt else link.speed(speed)

            elif ev.type == pygame.KEYUP:
                if ev.key in key_drive and active_drive == key_drive[ev.key]:
                    stop_drive()

            elif ev.type == pygame.MOUSEBUTTONDOWN:
                if not is_bt and slider.inflate(0, 20).collidepoint(ev.pos):
                    dragging_slider = True
                    set_speed_from_mouse(ev.pos[0])
                for b in buttons:
                    if b.hit(ev.pos) and b.enabled:
                        b.pressed = True
                        b.cb()

            elif ev.type == pygame.MOUSEBUTTONUP:
                dragging_slider = False
                for b in buttons:
                    b.pressed = False
                # releasing anywhere stops the held drive command (like the website)
                if active_drive:
                    stop_drive()

            elif ev.type == pygame.MOUSEMOTION and dragging_slider:
                set_speed_from_mouse(ev.pos[0])

        # repeat held drive command every 150ms like the website
        if active_drive and mode == "Manual" and now - last_repeat > 0.15:
            link.drive(active_drive)
            last_repeat = now

        # ---- draw ----
        screen.fill(BG)
        t = link.telemetry()

        title = f_big.render("NORA", True, ACCENT)
        screen.blit(title, title.get_rect(centerx=W // 2, y=14))
        sub = f_sml.render(transport + ("   connected" if link.ok else "   OFFLINE"),
                           True, GOOD if link.ok else CRIT)
        screen.blit(sub, sub.get_rect(centerx=W // 2, y=46))

        # DIR_FRONT/RIGHT/BACK/LEFT = 0/1/2/3, reported in "dir" while mode==1 (Auto)
        auto_dir = int(t["dir"]) if t.get("mode") == 1 and "dir" in t else -1

        colw = (W - 50) // 2
        sensor_bar(screen, f_sml, 20, 75, colw, "Front", t.get("F", -1), auto_dir == 0)
        sensor_bar(screen, f_sml, 30 + colw, 75, colw, "Back", t.get("B", -1), auto_dir == 2)
        sensor_bar(screen, f_sml, 20, 130, colw, "Left", t.get("L", -1), auto_dir == 3)
        sensor_bar(screen, f_sml, 30 + colw, 130, colw, "Right", t.get("R", -1), auto_dir == 1)

        # line follower + light + sound
        y = 195
        screen.blit(f_sml.render("Line:", True, DIM), (20, y + 3))
        for i, key in enumerate(("lfl", "lfm", "lfr")):
            on = t.get(key, 0) == 1
            pygame.draw.circle(screen, ACCENT if on else (34, 34, 34),
                               (75 + i * 30, y + 10), 10)
            pygame.draw.circle(screen, ACCENT if on else (68, 68, 68),
                               (75 + i * 30, y + 10), 10, 2)
        lt = t.get("lt")
        screen.blit(f_sml.render(
            f"Light: {int(lt)}%" if lt is not None else "Light: --",
            True, TEXT), (180, y + 3))
        snd = t.get("snd", 0) == 1
        pygame.draw.circle(screen, GOOD if snd else (34, 34, 34), (300, y + 10), 10)
        pygame.draw.circle(screen, GOOD if snd else (68, 68, 68), (300, y + 10), 10, 2)
        screen.blit(f_sml.render("sound", True, DIM), (315, y + 3))

        # music readout
        mt, ms = int(t.get("mt", 0)), int(t.get("ms", 0))
        state = ["stopped", "playing", "paused"][ms] if ms in (0, 1, 2) else "?"
        col = GOOD if ms == 1 else WARN if ms == 2 else DIM
        screen.blit(f_sml.render(f"MUSIC   track{mt:03d}", True, ACCENT), (20, 240))
        screen.blit(f_sml.render(state, True, col), (170, 240))

        # speed (BT has no absolute-set command, only +/- nudges the firmware tracks itself)
        speed_col = DIM if is_bt else ACCENT
        screen.blit(f_sml.render("Speed (BT: +/- only)" if is_bt else "Speed", True, DIM),
                   (20, 418))
        pygame.draw.rect(screen, (42, 42, 42), slider, border_radius=4)
        pygame.draw.rect(screen, speed_col,
                         (slider.x, slider.y, int(slider.w * speed / 100), 8),
                         border_radius=4)
        knob_x = slider.x + int(slider.w * speed / 100)
        pygame.draw.circle(screen, speed_col, (knob_x, slider.y + 4), 9)
        screen.blit(f_med.render(f"{speed}%", True, speed_col), (W - 60, 415))

        for b in buttons:
            b.draw(screen)
        # highlight held direction
        if active_drive:
            pygame.draw.rect(screen, ACCENT, dpad_btns[active_drive].rect, 3,
                             border_radius=8)

        hint = f_sml.render(
            "arrows drive · A/D turn · space stop · U uv · M/N/P/X music · +/- speed",
            True, DIM)
        screen.blit(hint, hint.get_rect(centerx=W // 2, y=H - 28))

        pygame.display.flip()
        clock.tick(60)

    stop_drive()
    if is_bt:
        link.ser.close()
    pygame.quit()
    sys.exit(0)


if __name__ == "__main__":
    main()
