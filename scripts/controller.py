 
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

  Emulate:             python3 nora_controller.py --emulate   (a pretend NORA on this PC: fake_nora.py)

CONTROLS (mirrors the web dashboard exactly -- same keys, same meanings)
  W/S, arrows  forward / backward          A/D, arrows  strafe left / right
  Q / E        turn left / right           Space        music play/pause
  Two at once blend (W+A drives forward and slides left, W+Q curves), and so does the pad's stick
  (her /drive; firmware without it: the stronger of the two). Driving while she drives herself
  (AUTO / LINE / IR) takes over: she goes to MANUAL and stops when you let go. STOP: any mode.
  M            next track                  X            cycle speed 25/50/75/100%
  U            cycle UV                    1/2/3/4      manual / auto / line / remote mode
  +/-          speed nudge (BT only --      Esc          quit
               BT has no absolute "set speed", just a relative step)
  F1           every key, remote button and BT letter, in its own window (scripts/keymap.py)
  Everything is also clickable with the mouse.

  Mode 4 (remote/IR) hands driving to the physical IR receiver, same as
  pressing "4" on it -- it isn't reachable over Bluetooth (the BT single-char
  protocol has no code for it), so key '4' is a no-op there.

  Whichever surface changes mode/UV/speed/lock -- this app, the web page, or
  the IR remote -- the others pick it up on their next /sensors poll instead
  of showing a stale state.
"""

import os
import argparse
import concurrent.futures
import sys
import threading
import time

import pygame

import keymap   # scripts/keymap.py: the keymap window (F1)
import controller_kit as kit   # scripts/controller_kit.py: themes, settings that stick, blended keys (every robot's)
from gamepad_drive import PadDrive, rumble_level   # any gamepad, or ARM's flight stick
import mind          # scripts/mind.py: the LLM minds (N: hers, H: IDA's), the same file in every controller
import mind_panel    # scripts/mind_panel.py: a mind's strip in the window

# ----------------------------------------------------------------------------
# Transports
# ----------------------------------------------------------------------------

class WifiLink:
    """Talks to the ESP32 web server. Full telemetry via /sensors."""

    def __init__(self, host, port):
        import requests
        self.requests = requests
        self.base = f"http://{host}:{port}"
        self.fleet_base = f"http://{host}:5000"   # NORA's fleet registry — fixed port
        self.ok = False
        self.data = {}
        self.fleet = {"authority": "NORA", "robots": []}
        self.mind_host = False   # mind.can_think() said yes: check in on /sensors?ctl=1 (MIND mode needs it)
        self.no_mix = False      # her firmware has no /drive (older than blended driving): one move at a time
        self._lock = threading.Lock()
        # commands fire every ~150ms while a key/button is held; a small
        # pool avoids spawning a fresh OS thread for every single one
        self._pool = concurrent.futures.ThreadPoolExecutor(
            max_workers=2, thread_name_prefix="wifi-cmd")
        threading.Thread(target=self._poll, daemon=True).start()
        threading.Thread(target=self._poll_fleet, daemon=True).start()

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
                r = self.requests.get(self.base + "/sensors" + ("?ctl=1" if self.mind_host else ""), timeout=1.0)
                with self._lock:
                    self.data = r.json()
                self.ok = True
            except Exception:
                self.ok = False
            time.sleep(0.4)

    def _poll_fleet(self):
        # fleet membership changes slowly — a slower cadence than telemetry is fine,
        # and a failed poll just keeps the last-known snapshot on screen
        while True:
            try:
                r = self.requests.get(self.fleet_base + "/robots", timeout=1.5)
                with self._lock:
                    self.fleet = r.json()
            except Exception:
                pass
            time.sleep(2.0)

    def telemetry(self):
        with self._lock:
            return dict(self.data)

    def fleet_snapshot(self):
        with self._lock:
            return dict(self.fleet)

    def become_mind_host(self):
        """Her mind's controller: unlock with her motor password and ctl=1, then check in."""
        try:
            self.requests.get(self.base + "/unlock", params={"pw": MOTOR_PASSWORD, "ctl": "1"}, timeout=2)
            self.mind_host = True
        except Exception:
            pass

    def serial(self, cmd):   # a line straight to her UNO (SAY:<n> plays a phrase-book line)
        self._pool.submit(self._get, "/serial?cmd=" + cmd)

    # driving -----------------------------------------------------------
    def drive(self, action):   # 'fw' 'bw' 'left' 'right' 'turnL' 'turnR'
        self._pool.submit(self._get, "/" + action)

    def stop(self):
        self._pool.submit(self._get, "/stop")

    def drive_mix(self, f, s, t):
        """Blended: forward, sideways (+ left), turn (+ left), -1..1 each. False if her firmware can't."""
        if self.no_mix:
            return False
        self._pool.submit(self._mix, f, s, t)
        return True

    def _mix(self, f, s, t):
        try:
            r = self.requests.get(self.base + "/drive", params={"f": f, "s": s, "t": t}, timeout=1.0)
            self.no_mix = r.status_code == 404
            self.ok = True
        except Exception:
            self.ok = False

    # everything else ----------------------------------------------------
    def mode(self, m):         # 'Manual' 'Auto' 'Line' 'Follow'
        self._pool.submit(self._get, "/mode" + m)

    def act(self, what):       # 'hello' 'dance': on the spot, in Manual
        self._pool.submit(self._get, "/act?do=" + what)

    def uv(self, state):       # 0 1 2
        path = ["/uvOff", "/uvOn", "/uvBlink"][state]
        self._pool.submit(self._get, path)

    def music(self, cmd):      # 'Play' 'Next' 'Prev' 'Stop'
        self._pool.submit(self._get, "/mu" + cmd)

    def speed(self, pct):
        self._pool.submit(self._get, f"/setspeed?v={pct}")

    def motorlock(self, locked, pw=None):
        if locked:
            self._pool.submit(self._get, "/motorlockOn")
        else:
            self._pool.submit(self._get, f"/motorlockOff?pw={pw}")

    def ida(self, c):          # IDA link: 'fw' 'bw' 'left' 'right' 'stop' 'auto' 'manual' 'speed'
        self._pool.submit(self._get, "/ida?c=" + c)

    def link(self, robot, c):  # fleet link: robot 'ida' 'mila' 'whip' 'kida', same commands as ida()
        self._pool.submit(self._get, f"/link?r={robot}&c={c}")


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
                   "track": "mt", "state": "ms", "light": "lt", "lock": "lock"}[k]] = float(v)
            except (KeyError, ValueError):
                pass
        with self._lock:
            self.data.update(d)

    def telemetry(self):
        with self._lock:
            return dict(self.data)

    def drive(self, action):
        self._send(self.DRIVE[action])

    def drive_mix(self, f, s, t):   # the single-letter protocol has no blended drive
        return False

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

    def motorlock(self, locked, pw=None):
        self._send("K" if locked else "V" + (pw or ""))

    IDA = {"fw": "F", "bw": "B", "left": "L", "right": "R",
           "stop": "S", "auto": "O", "manual": "W", "speed": "X",
           "unlock": "U", "lock": "K"}

    def ida(self, c):          # IDA link over BT: 'I' + one letter
        self._send("I" + self.IDA[c])

    ROBOT = {"ida": "I", "mila": "M", "whip": "W", "kida": "K", "nika": "N"}

    def link(self, robot, c):  # fleet link over BT: 'T' + robot letter + command letter
        self._send("T" + self.ROBOT[robot] + self.IDA[c])


# ----------------------------------------------------------------------------
# UI
# ----------------------------------------------------------------------------

# Colours: always from her colour_scheme.xml (sampled from her avatar), through controller_kit's
# themes: DEFAULT, LIGHT or CUSTOM (THEME, top right; kept in controller_settings.json).
def apply_theme(c):
    globals().update(
        BG=c["background"], PANEL=c["panel"], BORDER=c["border"], DIVIDER=c["divider"], TEXT=c["text"],
        DIM=c["text_sec"], ACCENT=c["accent"], WARN=kit.mix(c["accent"], c["accent2"], 0.5), CRIT=c["accent2"],
        GOOD=c["accent_hover"], ACCENT_BG=c["button_hover_bg"], DANGER_BG=c["danger_bg"],
        GOOD_BG=kit.mix(c["accent_hover"], c["background"], 0.8), OFF_TEXT=kit.mix(c["text_sec"], c["background"], 0.6))


SETTINGS = kit.Settings()
apply_theme(SETTINGS.colours())

W, H = 420, 870   # NORA's column (+90 over the base layout to fit the fleet panel)
IDA_W = 230        # the IDA link column to its right
WIN_W = W + IDA_W
IDA_COL = (232, 160, 90)   # IDA's avatar amber, so her panel reads as hers
# Robots NORA can drive over the IR link, in selector order, each in its own
# accent colour (from its colour_scheme.xml) so the panel reads as theirs
LINK_ROBOTS = [("ida", "IDA", IDA_COL), ("mila", "MILA", (31, 212, 222)),
               ("whip", "WHIP", (211, 72, 89)), ("kida", "KIDA", (223, 91, 163)),
               ("nika", "NIKA", (217, 112, 58))]

IMAGES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images")


def round_avatar(path, size):
    """The avatar as a circular surface, or None if it can't be loaded."""
    try:
        img = pygame.transform.smoothscale(pygame.image.load(path).convert(), (size, size))
    except (pygame.error, OSError):
        return None
    mask = pygame.Surface((size, size), pygame.SRCALPHA)
    pygame.draw.circle(mask, (255, 255, 255, 255), (size // 2, size // 2), size // 2)
    out = pygame.Surface((size, size), pygame.SRCALPHA)
    out.blit(img, (0, 0))
    out.blit(mask, (0, 0), special_flags=pygame.BLEND_RGBA_MIN)
    return out


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
            pygame.draw.rect(surf, PANEL, self.rect, border_radius=6)
            pygame.draw.rect(surf, DIVIDER, self.rect, 2, border_radius=6)
            txt = self.font.render(self.label, True, OFF_TEXT)
            surf.blit(txt, txt.get_rect(center=self.rect.center))
            return
        color = ACCENT if (self.active or self.pressed) else BORDER
        bg = ACCENT_BG if (self.active or self.pressed) else PANEL
        pygame.draw.rect(surf, bg, self.rect, border_radius=6)
        pygame.draw.rect(surf, color, self.rect, 2, border_radius=6)
        txt = self.font.render(self.label, True,
                               ACCENT if (self.active or self.pressed) else DIM)
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
    bg = ACCENT_BG if (traveling and color == ACCENT) else PANEL
    pygame.draw.rect(surf, bg, box, border_radius=6)
    pygame.draw.rect(surf, color, box, 2, border_radius=6)
    txt = font.render(f"{label}  {value:.0f} cm" if value > 0 else f"{label}  --",
                      True, TEXT)
    surf.blit(txt, (x + 10, y + 7))
    # distance bar
    bar_bg = pygame.Rect(x + 10, y + 32, w - 20, 5)
    pygame.draw.rect(surf, DIVIDER, bar_bg, border_radius=3)
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

    wifi_btn  = pygame.Rect(40, 100, W - 80, 54)
    bt_btn    = pygame.Rect(40, 162, W - 80, 54)
    emu_btn   = pygame.Rect(40, 224, W - 80, 54)
    host_box  = pygame.Rect(40, 300, 220, 40)
    port_box  = pygame.Rect(270, 300, 110, 40)
    btp_box   = pygame.Rect(40, 300, W - 80, 40)
    go_btn    = pygame.Rect(40, 370, W - 80, 55)

    def try_connect():
        nonlocal error
        try:
            if choice == "emulate":
                return emulated_link()
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
                elif emu_btn.collidepoint(ev.pos):
                    choice = "emulate"; error = ""
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
            (emu_btn, "emulate", "Emulate",
             "a pretend NORA on this PC: no robot needed"),
        ):
            sel = choice == key
            pygame.draw.rect(screen, ACCENT_BG if sel else PANEL, rect,
                             border_radius=6)
            pygame.draw.rect(screen, ACCENT if sel else BORDER, rect, 2,
                             border_radius=6)
            screen.blit(f_med.render(label, True, ACCENT if sel else TEXT),
                        (rect.x + 16, rect.y + 8))
            screen.blit(f_sml.render(note, True, DIM),
                        (rect.x + 16, rect.y + 32))

        # address fields
        if choice == "emulate":
            pass
        elif choice == "wifi":
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

        pygame.draw.rect(screen, GOOD_BG, go_btn, border_radius=6)
        pygame.draw.rect(screen, GOOD, go_btn, 2, border_radius=6)
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


# ---------------------------------------------------------------------------
# The minds (mind.py): NORA's (N) and IDA's (H). NORA's runs her MIND mode (/mind on her ESP32);
# IDA has no WiFi, so her mind drives her over NORA's IR link and senses nothing: she's blind, and
# so very hesitant, one short step at a time.
# ---------------------------------------------------------------------------
MOTOR_PASSWORD = "1234"   # NORA's (secrets.h MOTOR_PASSWORD): a safety catch, not security
NORA_PERSONA = (
    "You're NORA, a four-wheeled mecanum robot, and the boss of the DREAM Robotics fleet: you host its "
    "network when RIFT is away, you drive IDA, MILA, WHIP, the KIDAs and NIKA over your IR link, and "
    "everyone registers with you. Warm, confident, a bit bossy, very organised; you narrate like a "
    "captain on her bridge. You love music (you have a speaker and an SD card full of songs) and your "
    "UV light. You can slide sideways as well as turn."
)
NORA_MOVES = {"fw": "drive forward", "bw": "drive backward", "left": "slide left (mecanum)",
              "right": "slide right (mecanum)", "turnL": "turn left on the spot", "turnR": "turn right on the spot"}
NORA_SAFETY = "she won't drive into anything closer than 15 cm on any side, and stops when her wheels are locked"
NORA_VOICE = ("Microsoft Zira Desktop", 0, "medium")   # the voice her SD card's lines are made with
# Her phrase book (make_voice.ps1 lines 11-31, on her SD card): she says these from her own speaker.
# Until the lines are on the card, leave PHRASEBOOK_ON_CARD False: she speaks through this PC instead.
PHRASEBOOK_ON_CARD = False
NORA_PHRASES = {
    11: "Mind mode.", 12: "Let me think about this.", 13: "All clear. Moving out.", 14: "Wall ahead. Changing course.",
    15: "Sliding over.", 16: "Turning around.", 17: "Hmm. Tight spot.", 18: "Nice and open in here.",
    19: "Fleet, report.", 20: "I've got this.", 21: "Easy does it.", 22: "Something's close behind me.",
    23: "My wheels are locked.", 24: "Back to the bridge.", 25: "Who turned the lights off?",
    26: "Music time.", 27: "Exploring.", 28: "That was close.", 29: "Good. Very good.", 30: "Stopping here.",
    31: "Where is everyone?",
}
IDA_PERSONA = (
    "You're IDA, a small tracked robot and NORA's loyal little sidekick. You have no WiFi: you only hear "
    "NORA's IR blinks, and you can't tell anyone what you see. So you're timid and careful, a bit "
    "anxious, devoted to NORA, and you talk in short nervous sentences."
)
IDA_MOVES = {"fw": "creep forward", "bw": "creep backward", "left": "turn left", "right": "turn right"}
IDA_SAFETY = "she stops by herself before a wall when driving forward (her own sensor), and NORA can lock her wheels"
IDA_VOICE = ("Microsoft Zira Desktop", 1, "high")
MIND_STEP_S = 0.4          # one step of a move (MIND_STEP_MS on her ESP32)
MIND_MOVE_TIMEOUT_S = 6.0


def nora_body(link):
    def look():
        s = link.get_json("/sensors?ctl=1") or {}
        return {
            "distances_cm": {k: s.get(c) for k, c in (("front", "F"), ("left", "L"), ("back", "B"), ("right", "R"))},
            "wheels_locked": bool(s.get("lock")), "stopped_at_a_wall_last_move": bool(s.get("blocked")),
            "temperature_c": s.get("temp"), "light_pct": s.get("lt"), "heard_a_sound": bool(s.get("snd")),
            "music": ["stopped", "playing", "paused"][s.get("ms")] if s.get("ms") in (0, 1, 2) else None,
        }

    def act(move, steps):
        r = link.get_json(f"/mind?act={move}&steps={steps}", timeout=3)
        if r is None:
            raise ConnectionError("NORA isn't answering")
        if move == "stop" or not r.get("ok"):
            return r
        t0 = time.time()
        while time.time() - t0 < MIND_MOVE_TIMEOUT_S:
            time.sleep(0.3)
            s = link.get_json("/sensors?ctl=1") or {}
            if not s.get("mindSteps"):
                return {"ok": True, "stopped_at_a_wall": bool(s.get("blocked")), "front_cm": s.get("F")}
        return {"ok": True, "note": "still moving"}

    def phrase(n):
        link.serial(f"SAY:{n}")
        return True

    return look, act, phrase


def ida_body(link):
    def look():
        return {"senses": "none: you can't see or measure anything, and nobody can tell you what's around",
                "how_you_move": "NORA blinks your moves to you over IR; you don't even know if you heard them",
                "warning": "Be very hesitant: one short step at a time, and stop often."}

    def act(move, steps):
        if move == "stop":
            link.ida("stop")
            return {"ok": True}
        end = time.time() + MIND_STEP_S   # one step only, whatever she planned: she's blind
        while time.time() < end:
            link.ida(move)                # resent while held, like the keys
            time.sleep(0.15)
        link.ida("stop")
        return {"ok": True, "steps": 1, "note": "sent over NORA's IR: no way to know how it went"}

    return look, act


FAKE = {"robot": None}   # EMULATE: the fake NORA (fake_nora.py) running in here


def emulated_link():
    """A fake NORA on this PC and a WiFi link to her: (link, label), or (None, None)."""
    import fake_nora
    try:
        FAKE["robot"] = fake_nora.start(quiet=True)
    except OSError as e:
        print(f"Can't start the fake NORA: {e}", file=sys.stderr)
        return None, None
    return WifiLink("127.0.0.1", 5002), "EMULATED NORA"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--port", type=int, default=5002)
    ap.add_argument("--wifi", action="store_true",
                    help="skip the picker and connect over WiFi")
    ap.add_argument("--bt", metavar="SERIAL_PORT",
                    help="skip the picker and connect over this serial port")
    ap.add_argument("--emulate", action="store_true",
                    help="a pretend NORA on this PC (fake_nora.py), no robot needed")
    ap.add_argument("--ladder", action="store_true",
                    help="join / host the fleet's WiFi (fleet_ladder.py; FLEET_LADDER=1 does the same)")
    args = ap.parse_args()
    from fleet_ladder import FleetLadder   # the fleet's WiFi ladder (fleet TODO, section 2)
    FleetLadder("NORACTL", enabled=args.ladder or None).start()

    pygame.init()
    screen = pygame.display.set_mode((WIN_W, H))
    pygame.display.set_caption("NORA Control")

    try:  # window icon: the robot's avatar
        pygame.display.set_icon(pygame.image.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images", "nora-icon.png")))
    except (pygame.error, OSError):
        pass
    clock = pygame.time.Clock()
    # monospace to match KIDA's HUD font family
    f_big = pygame.font.SysFont("monospace", 26, bold=True)
    f_med = pygame.font.SysFont("monospace", 15, bold=True)
    f_sml = pygame.font.SysFont("monospace", 13)

    if args.emulate:
        link, transport = emulated_link()
        if link is None:
            pygame.quit(); sys.exit(1)
    elif args.bt:
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

    # the minds: only over WiFi, and only on a PC that can run them (mind.can_think())
    mind_state = {"capable": False, "why": "checking this PC..."}
    nora_mind = ida_mind = None
    if isinstance(link, WifiLink):
        n_look, n_act, n_phrase = nora_body(link)
        nora_mind = mind.Mind("NORA", NORA_PERSONA, NORA_MOVES, n_look, n_act, voice=NORA_VOICE[0], rate=NORA_VOICE[1],
                              pitch=NORA_VOICE[2], safety=NORA_SAFETY,
                              phrases=NORA_PHRASES if PHRASEBOOK_ON_CARD else None,
                              robot_phrase=n_phrase if PHRASEBOOK_ON_CARD else None)
        i_look, i_act = ida_body(link)
        ida_mind = mind.Mind("IDA", IDA_PERSONA, IDA_MOVES, i_look, i_act, voice=IDA_VOICE[0], rate=IDA_VOICE[1],
                             pitch=IDA_VOICE[2], safety=IDA_SAFETY)

        def check_mind():
            mind_state["capable"], mind_state["why"] = mind.can_think()
            if mind_state["capable"]:
                link.become_mind_host()
        threading.Thread(target=check_mind, daemon=True).start()

    # pull current mode/speed from the robot so the UI doesn't reset them
    # to "Manual"/100% out from under whatever was already running
    init_mode, init_speed, init_locked = "Manual", 100, True
    if isinstance(link, WifiLink):
        cal = link.get_json("/getcal")
        if cal and "spd" in cal:
            init_speed = max(0, min(100, int(cal["spd"])))
        sensors = link.get_json("/sensors")
        mode_names = ["Manual", "Auto", "Line", "IR"]
        if sensors and int(sensors.get("mode", 0)) in (0, 1, 2, 3):
            init_mode = mode_names[int(sensors["mode"])]
        if sensors and "lock" in sensors:
            init_locked = int(sensors["lock"]) == 1

    mode_names_all = ("Manual", "Auto", "Line", "IR", "Mind", "Follow")
    mode = init_mode
    uv_state = 0
    speed = init_speed
    motor_locked = init_locked   # firmware boots locked; mirror that until told otherwise
    pw_prompt = False
    pw_buffer = ""
    pw_error = ""
    active_drive = None       # current held drive action
    last_repeat = 0

    buttons = []

    def add(rect, label, cb, active=False, enabled=True):
        b = Button(rect, label, cb, f_med, active, enabled)
        buttons.append(b)
        return b

    def update_dpad_enabled():
        # any mode: pressing one takes over (she goes to MANUAL); only her wheel lock stops them
        enabled = not motor_locked
        for db in dpad_btns.values():
            db.enabled = enabled

    pending = kit.Pending()   # mode / UV / speed re-sent until her /sensors says so (WiFi)
    is_wifi = isinstance(link, WifiLink)

    # mode buttons
    def set_mode(m):
        nonlocal mode
        mode = m
        if is_wifi and m in mode_names_all:
            pending.want("mode", mode_names_all.index(m), lambda: link.mode(m))
        else:
            link.mode(m)
        for b, name in zip(mode_btns, ("Manual", "Auto", "Line", "Follow")):
            b.active = (name == m)
        update_dpad_enabled()

    mode_btns = [
        add((20, 410, 90, 38), "MANUAL", lambda: set_mode("Manual"), init_mode == "Manual"),
        add((116, 410, 90, 38), "AUTO", lambda: set_mode("Auto"), init_mode == "Auto"),
        add((212, 410, 90, 38), "LINE", lambda: set_mode("Line"), init_mode == "Line"),
        add((308, 410, 92, 38), "FOLLOW", lambda: set_mode("Follow"), enabled=is_wifi),
    ]
    add((20, 500, 185, 28), "HELLO", lambda: link.act("hello"), enabled=is_wifi)   # on the spot, in Manual
    add((215, 500, 185, 28), "DANCE", lambda: link.act("dance"), enabled=is_wifi)

    def cycle_uv():
        nonlocal uv_state
        uv_state = (uv_state + 1) % 3
        if is_wifi:
            pending.want("uv", uv_state, lambda u=uv_state: link.uv(u))
        else:
            link.uv(uv_state)   # BT's U cycles: never sent twice
        uv_btn.label = ["UV OFF", "UV ON", "UV BLINK"][uv_state]
        uv_btn.active = uv_state != 0

    uv_btn = add((20, 458, 120, 38), "UV OFF", cycle_uv)

    # motor lock — locking is instant, unlocking needs the password prompt below
    def toggle_lock():
        nonlocal motor_locked, pw_prompt, pw_buffer, pw_error
        if motor_locked:
            pw_prompt = True
            pw_buffer = ""
            pw_error = ""
        else:
            motor_locked = True
            link.motorlock(True)
            lock_btn.label = "MOTOR LOCK: ON"
            lock_btn.active = True
            update_dpad_enabled()

    lock_btn = add((20, 370, W - 40, 32),
                   "MOTOR LOCK: ON" if motor_locked else "MOTOR LOCK: OFF",
                   toggle_lock, active=motor_locked)

    # music
    add((150, 458, 60, 38), "|<", lambda: link.music("Prev"))
    add((215, 458, 60, 38), ">||", lambda: link.music("Play"))
    add((280, 458, 60, 38), ">|", lambda: link.music("Next"))
    add((345, 458, 55, 38), "[]", lambda: link.music("Stop"))

    # dpad
    def take_over():
        """Driving her while she drives herself (AUTO / LINE / IR): MANUAL, so she does what's
        pressed and stops when it's let go. (Her firmware only drives from the web in MANUAL.)"""
        if mode not in ("Manual", "Mind"):
            set_mode("Manual")

    def stop_all():
        """STOP, in any mode: MANUAL, wheels still."""
        nonlocal active_drive, keys_driving
        active_drive, keys_driving = None, False
        if mode != "Manual":
            set_mode("Manual")
        link.stop()

    def start_drive(action):
        nonlocal active_drive
        take_over()
        active_drive = action
        link.drive(action)

    def stop_drive():
        nonlocal active_drive
        if active_drive:
            active_drive = None
            link.stop()

    pad_cx, pad_cy, bs, gap = W // 2, 660, 80, 8
    dpad_defs = [
        ("fw",    (pad_cx - bs // 2, pad_cy - bs - gap - bs // 2), "^"),
        ("bw",    (pad_cx - bs // 2, pad_cy + gap + bs // 2), "v"),
        ("left",  (pad_cx - bs - gap - bs // 2, pad_cy + gap + bs // 2), "<"),
        ("right", (pad_cx + gap + bs // 2, pad_cy + gap + bs // 2), ">"),
        ("turnL", (pad_cx - bs - gap - bs // 2, pad_cy - bs // 2), "@<"),
        ("turnR", (pad_cx + gap + bs // 2, pad_cy - bs // 2), ">@"),
    ]
    dpad_btns = {}
    dpad_enabled_init = not motor_locked
    for action, (bx, by), lbl in dpad_defs:
        dpad_btns[action] = add((bx, by, bs, bs), lbl,
                                lambda a=action: start_drive(a),
                                enabled=dpad_enabled_init)
    stop_btn = add((pad_cx - bs // 2, pad_cy - bs // 2, bs, bs), "STOP", stop_all)

    # held keys blend (controller_kit.keys_vector): W+A = forward + slide left, W+Q = a curve
    KEYS = {"fwd": (pygame.K_w, pygame.K_UP), "back": (pygame.K_s, pygame.K_DOWN),
            "left": (pygame.K_a, pygame.K_LEFT), "right": (pygame.K_d, pygame.K_RIGHT),
            "turn_l": (pygame.K_q,), "turn_r": (pygame.K_e,)}
    keys_driving = False      # the keys / stick are driving her (not a held D-pad button)
    last_mix = 0.0

    def theme_next():
        apply_theme(SETTINGS.next_theme())
        theme_btn.label = f"THEME: {SETTINGS.theme}"

    theme_btn = add((W - 152, 12, 140, 26), f"THEME: {SETTINGS.theme}", theme_next)

    # ---- fleet remote panel: drives IDA, MILA, WHIP or KIDA through NORA's
    # IR transmitter; the avatar, name and colour follow the selected robot ----
    nora_face = round_avatar(os.path.join(IMAGES, "nora_avatar.jpg"), 52)
    link_faces = {key: round_avatar(os.path.join(IMAGES, f"{key}_avatar.jpg"), 96) for key, _, _ in LINK_ROBOTS}
    link_idx = 0               # selected robot in LINK_ROBOTS
    ida_active = None          # held drive command
    ida_last = 0
    ida_status = "ready"

    def ida_send(c):
        nonlocal ida_status
        link.link(LINK_ROBOTS[link_idx][0], c)
        ida_status = c

    def select_robot(step):
        nonlocal link_idx, ida_status
        ida_stop()
        link_idx = (link_idx + step) % len(LINK_ROBOTS)
        ida_status = "ready"

    def ida_start(c):
        nonlocal ida_active, ida_last
        ida_active = c
        ida_last = time.time()
        ida_send(c)

    def ida_stop():
        nonlocal ida_active
        if ida_active:
            ida_active = None
            ida_send("stop")

    ix = W + 12                      # IDA column left edge
    icx = W + IDA_W // 2             # IDA column centre
    add((ix - 4, 52, 30, 40), "<", lambda: select_robot(-1))
    add((W + IDA_W - 44, 52, 30, 40), ">", lambda: select_robot(1))
    add((ix, 215, 100, 34), "MANUAL", lambda: ida_send("manual"))
    add((ix + 106, 215, 100, 34), "AUTO", lambda: ida_send("auto"))
    add((ix, 255, 66, 34), "SPEED", lambda: ida_send("speed"))
    add((ix + 70, 255, 66, 34), "UNLOCK", lambda: ida_send("unlock"))   # the picked robot's wheels
    add((ix + 140, 255, 66, 34), "LOCK", lambda: ida_send("lock"))
    ibs, igap, ipy = 60, 6, 395      # IDA pad: button size, gap, centre y
    ida_btns = {}
    for c, (dx, dy), lbl in (("fw", (0, -1), "^"), ("bw", (0, 1), "v"),
                             ("left", (-1, 0), "<"), ("right", (1, 0), ">")):
        ida_btns[c] = add((icx - ibs // 2 + dx * (ibs + igap), ipy - ibs // 2 + dy * (ibs + igap), ibs, ibs),
                          lbl, lambda c=c: ida_start(c))
    add((icx - ibs // 2, ipy - ibs // 2, ibs, ibs), "STOP", lambda: (ida_stop(), ida_send("stop")))
    key_ida = {pygame.K_i: "fw", pygame.K_k: "bw", pygame.K_j: "left", pygame.K_l: "right"}

    SPEED_PRESETS = [25, 50, 75, 100]

    def send_speed(pct):
        if is_wifi:
            pending.want("spd", pct, lambda: link.speed(pct))
        else:
            link.speed(pct)   # no-op over BT (no absolute-set command there)

    def cycle_speed():
        nonlocal speed
        idx = min(range(len(SPEED_PRESETS)), key=lambda i: abs(SPEED_PRESETS[i] - speed))
        idx = (idx + 1) % len(SPEED_PRESETS)
        speed = SPEED_PRESETS[idx]
        send_speed(speed)

    slider = pygame.Rect(70, 515, W - 140, 8)
    dragging_slider = False

    is_bt = isinstance(link, BtLink)   # BT has no absolute-speed command; slider is WiFi-only

    def set_speed_from_mouse(mx):
        nonlocal speed
        pct = round((mx - slider.x) / slider.w * 100)
        speed = max(0, min(100, pct))
        send_speed(speed)

    pad = PadDrive()          # a gamepad / joystick: drives like the keys (keymap.py, Gamepad)
    pad_drive = None          # the drive the pad is holding

    running = True
    while running:
        now = time.time()
        for ev in pygame.event.get():
            pad.handle(ev)
            if ev.type == pygame.QUIT:
                running = False

            elif ev.type == pygame.KEYDOWN:
                if pw_prompt:
                    if ev.key == pygame.K_ESCAPE:
                        pw_prompt = False
                    elif ev.key == pygame.K_RETURN:
                        if pw_buffer == "1234":
                            motor_locked = False
                            link.motorlock(False, pw_buffer)
                            lock_btn.label = "MOTOR LOCK: OFF"
                            lock_btn.active = False
                            update_dpad_enabled()
                            pw_prompt = False
                            pw_error = ""
                        else:
                            pw_error = "Wrong password"
                            pw_buffer = ""
                    elif ev.key == pygame.K_BACKSPACE:
                        pw_buffer = pw_buffer[:-1]
                    elif ev.unicode and ev.unicode.isdigit():
                        pw_buffer += ev.unicode
                    continue
                if ev.key == pygame.K_ESCAPE:
                    running = False
                elif ev.key == pygame.K_F1:
                    keymap.open_window()   # every key, remote button and BT letter, in its own window
                elif ev.key == pygame.K_TAB:
                    select_robot(-1 if ev.mod & pygame.KMOD_SHIFT else 1)
                elif ev.key in key_ida:
                    ida_start(key_ida[ev.key])
                elif ev.key == pygame.K_o: ida_send("auto")
                elif ev.key == pygame.K_p: ida_send("manual")
                elif ev.key == pygame.K_y: ida_send("speed")
                elif ev.key == pygame.K_g: ida_send("unlock")   # the picked robot's wheels
                elif ev.key == pygame.K_v: ida_send("lock")
                elif ev.key == pygame.K_SPACE:
                    link.music("Play")   # play/pause toggle, matches the website
                elif ev.key == pygame.K_1: set_mode("Manual")
                elif ev.key == pygame.K_2: set_mode("Auto")
                elif ev.key == pygame.K_3: set_mode("Line")
                elif ev.key == pygame.K_4:
                    if not is_bt: set_mode("IR")   # no BT command for this mode
                elif ev.key == pygame.K_f:
                    if not is_bt: set_mode("Follow")
                elif ev.key == pygame.K_b:
                    if not is_bt: link.act("hello")
                elif ev.key == pygame.K_c:
                    if not is_bt: link.act("dance")
                elif ev.key == pygame.K_u: cycle_uv()
                elif ev.key == pygame.K_m: link.music("Next")
                elif ev.key == pygame.K_n and nora_mind and mind_state["capable"]:   # her MIND mode on / off
                    link.mode("Manual" if nora_mind.on else "Mind")
                elif ev.key == pygame.K_h and ida_mind and mind_state["capable"]:   # IDA's mind on / off
                    if ida_mind.on:
                        ida_mind.stop()
                    else:
                        ida_mind.start(goal="explore a little, carefully, and stay near NORA")
                elif ev.key == pygame.K_x: cycle_speed()
                elif ev.key in (pygame.K_PLUS, pygame.K_EQUALS, pygame.K_KP_PLUS):
                    speed = min(100, speed + 10)
                    link.nudge_speed(True) if is_bt else send_speed(speed)
                elif ev.key in (pygame.K_MINUS, pygame.K_KP_MINUS):
                    speed = max(0, speed - 10)
                    link.nudge_speed(False) if is_bt else send_speed(speed)

            elif ev.type == pygame.KEYUP:
                if ev.key in key_ida and ida_active == key_ida[ev.key]:
                    ida_stop()

            elif ev.type == pygame.MOUSEBUTTONDOWN:
                if pw_prompt:
                    continue
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
                ida_stop()

            elif ev.type == pygame.MOUSEMOTION and dragging_slider:
                set_speed_from_mouse(ev.pos[0])

        # held keys and the pad's stick drive her, blended (W+A, the stick half left...): her
        # /drive, resent every 150 ms; firmware without it gets the strongest of them, one at a time
        if not pw_prompt:
            pressed = pygame.key.get_pressed()
            vec = kit.keys_vector(pressed, KEYS)
            if not any(vec):
                vec = pad.vector()
            pad_drive = None
            if any(vec) and not motor_locked:
                take_over()
                f, sd, tn = vec
                if now - last_mix > 0.15 or not keys_driving:
                    if not link.drive_mix(f, sd, tn):
                        action = ("fw" if f > 0 else "bw") if abs(f) >= max(abs(sd), abs(tn)) else \
                                 ("left" if sd > 0 else "right") if abs(sd) >= abs(tn) else ("turnL" if tn > 0 else "turnR")
                        if action != active_drive:
                            active_drive = action
                            link.drive(action)
                    last_mix = now
                keys_driving = True
                pad_drive = "fw" if f > 0 else "bw" if f < 0 else "left" if sd > 0 else "right" if sd < 0 else None
            elif keys_driving:
                keys_driving = False
                active_drive = None
                link.stop()
            for b in pad.presses():
                if b == 0: stop_all()
                elif b == 1: cycle_uv()
                elif b == 2: link.music("Play")
                elif b == 3: link.music("Next")
                elif b == 6: set_mode("Auto")
                elif b == 7: set_mode("Manual")

        # repeat held drive command every 150ms like the website
        if active_drive and not keys_driving and mode == "Manual" and now - last_repeat > 0.15:
            link.drive(active_drive)
            last_repeat = now
        if ida_active and now - ida_last > 0.15:
            link.link(LINK_ROBOTS[link_idx][0], ida_active)
            ida_last = now

        # ---- draw ----
        screen.fill(BG)
        t = link.telemetry()
        # the pad buzzes as she nears a wall in the way it drives her (gamepad_drive.rumble_level)
        side = {"fw": "F", "bw": "B", "left": "L", "right": "R"}.get(pad_drive)
        pad.rumble(*rumble_level(t.get(side) if side else None))

        # another client (web dashboard, IR remote) may have changed mode,
        # UV, speed, or the motor lock -- reflect it here instead of showing
        # a stale state. Only update local UI state, never re-send the
        # command (that would just echo whatever we just received).
        if nora_mind:
            in_mind = link.ok and t.get("mode") == 4
            if in_mind and not nora_mind.on and mind_state["capable"]:
                nora_mind.start(goal="patrol your room, keep the fleet in order, and tell everyone what you see")
            elif not in_mind and nora_mind.on:
                nora_mind.on = False   # she's out of MIND already: just stop thinking
                nora_mind.state = "off"
        if is_wifi and link.ok:
            pending.check(t)
        if "mode" in t and pending.value("mode") is None:
            try:
                srv_mode = mode_names_all[int(t["mode"])]
            except (ValueError, IndexError):
                srv_mode = mode
            if srv_mode != mode:
                mode = srv_mode
                for b, name in zip(mode_btns, ("Manual", "Auto", "Line", "Follow")):
                    b.active = (name == mode)
                update_dpad_enabled()

        if "uv" in t and int(t["uv"]) != uv_state and pending.value("uv") is None:
            uv_state = int(t["uv"])
            uv_btn.label = ["UV OFF", "UV ON", "UV BLINK"][uv_state]
            uv_btn.active = uv_state != 0

        if not is_bt and "spd" in t and int(t["spd"]) != speed and pending.value("spd") is None and not dragging_slider:
            speed = int(t["spd"])

        if "lock" in t and (int(t["lock"]) == 1) != motor_locked and not pw_prompt:
            motor_locked = int(t["lock"]) == 1
            lock_btn.label = "MOTOR LOCK: ON" if motor_locked else "MOTOR LOCK: OFF"
            lock_btn.active = motor_locked
            update_dpad_enabled()

        title = f_big.render("NORA", True, ACCENT)
        screen.blit(title, title.get_rect(centerx=W // 2, y=14))
        if nora_face:
            screen.blit(nora_face, (16, 10))
            pygame.draw.circle(screen, ACCENT, (16 + 26, 10 + 26), 27, 2)

        # IDA link column
        col = pygame.Rect(W + 2, 10, IDA_W - 12, H - 52)
        pygame.draw.rect(screen, PANEL, col, border_radius=6)
        pygame.draw.rect(screen, BORDER, col, 1, border_radius=6)
        robot_key, robot_name, robot_col = LINK_ROBOTS[link_idx]
        face = link_faces.get(robot_key)
        if face:
            screen.blit(face, (icx - 48, 24))
        pygame.draw.circle(screen, robot_col, (icx, 24 + 48), 49, 2)
        lbl = f_big.render(robot_name, True, robot_col)
        screen.blit(lbl, lbl.get_rect(centerx=icx, y=128))
        lbl = f_sml.render("via NORA's IR link", True, DIM)
        screen.blit(lbl, lbl.get_rect(centerx=icx, y=160))
        lbl = f_sml.render("last: " + ida_status, True, TEXT)
        screen.blit(lbl, lbl.get_rect(centerx=icx, y=184))
        for i, line in enumerate(("I J K L  drive", "O auto · P manual", "Y speed · Tab robot")):
            lbl = f_sml.render(line, True, DIM)
            screen.blit(lbl, lbl.get_rect(centerx=icx, y=500 + i * 18))
        status_text = transport + ("   connected" if link.ok else "   OFFLINE")
        if FAKE["robot"]:
            status_text = f"EMULATED  {FAKE['robot'].last_log()}"
        sub = f_sml.render(kit.fit(status_text, f_sml, W - 88), True, GOOD if link.ok else CRIT)
        screen.blit(sub, sub.get_rect(centerx=max(W // 2, 76 + sub.get_width() // 2), y=46))   # clear of her avatar

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
            pygame.draw.circle(screen, ACCENT if on else PANEL,
                               (75 + i * 30, y + 10), 10)
            pygame.draw.circle(screen, ACCENT if on else BORDER,
                               (75 + i * 30, y + 10), 10, 2)
        lt = t.get("lt")
        screen.blit(f_sml.render(
            f"Light: {int(lt)}%" if lt is not None else "Light: --",
            True, TEXT), (180, y + 3))
        snd = t.get("snd", 0) == 1
        pygame.draw.circle(screen, GOOD if snd else PANEL, (300, y + 10), 10)
        pygame.draw.circle(screen, GOOD if snd else BORDER, (300, y + 10), 10, 2)
        screen.blit(f_sml.render("sound", True, DIM), (315, y + 3))

        # music readout
        mt, ms = int(t.get("mt", 0)), int(t.get("ms", 0))
        state = ["stopped", "playing", "paused"][ms] if ms in (0, 1, 2) else "?"
        col = GOOD if ms == 1 else WARN if ms == 2 else DIM
        screen.blit(f_sml.render(f"MUSIC   track{mt:03d}", True, ACCENT), (20, 240))
        screen.blit(f_sml.render(state, True, col), (170, 240))

        # fleet panel — who's on the network via NORA's registry (port 5000).
        # BT is a direct serial link, not on the LAN, so it has no access to this.
        fy = 268
        screen.blit(f_sml.render("FLEET", True, DIM), (20, fy))
        if is_bt:
            screen.blit(f_sml.render("unavailable over BT", True, DIM), (75, fy))
        else:
            fleet = link.fleet_snapshot()
            authority = fleet.get("authority") or "NORA"
            is_self = authority == "NORA"
            auth_txt = "authority: NORA (self)" if is_self else f"authority: {authority}"
            screen.blit(f_sml.render(auth_txt, True, ACCENT if is_self else WARN), (75, fy))

            others = [r for r in fleet.get("robots", []) if r.get("name") != "NORA"]
            row_y = fy + 18
            if not others:
                screen.blit(f_sml.render("(no other robots registered)", True, DIM), (20, row_y))
            else:
                for r in others[:3]:
                    line = f"• {r.get('name', '?')} ({r.get('type', '?')})"
                    screen.blit(f_sml.render(line, True, TEXT), (20, row_y))
                    row_y += 16
                if len(others) > 3:
                    screen.blit(f_sml.render(f"+{len(others) - 3} more", True, DIM), (20, row_y))

        # speed (BT has no absolute-set command, only +/- nudges the firmware tracks itself)
        speed_col = DIM if is_bt else ACCENT
        screen.blit(f_sml.render("Speed (BT: +/- only)" if is_bt else "Speed", True, DIM),
                   (20, 508))
        pygame.draw.rect(screen, DIVIDER, slider, border_radius=4)
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
                             border_radius=6)
        if ida_active:
            pygame.draw.rect(screen, robot_col, ida_btns[ida_active].rect, 3, border_radius=6)

        hint = f_sml.render(kit.fit(
            "WASD drive (W+A blends) · Q/E turn · space play · M next · X speed · U uv · 1-4 mode · F1 all keys",
            f_sml, WIN_W - 20), True, DIM)
        screen.blit(hint, hint.get_rect(centerx=WIN_W // 2, y=H - 28))

        if pw_prompt:
            overlay = pygame.Surface((WIN_W, H), pygame.SRCALPHA)
            overlay.fill((0, 0, 0, 180))
            screen.blit(overlay, (0, 0))
            box = pygame.Rect(40, H // 2 - 70, W - 80, 140)
            pygame.draw.rect(screen, PANEL, box, border_radius=6)
            pygame.draw.rect(screen, ACCENT, box, 2, border_radius=6)
            prompt_txt = f_med.render("Enter password to unlock motors", True, TEXT)
            screen.blit(prompt_txt, prompt_txt.get_rect(centerx=W // 2, y=box.y + 16))
            pw_txt = f_big.render("*" * len(pw_buffer) or " ", True, ACCENT)
            screen.blit(pw_txt, pw_txt.get_rect(centerx=W // 2, y=box.y + 46))
            if pw_error:
                err_txt = f_sml.render(pw_error, True, CRIT)
                screen.blit(err_txt, err_txt.get_rect(centerx=W // 2, y=box.y + 88))
            hint2 = f_sml.render("Enter = confirm · Esc = cancel", True, DIM)
            screen.blit(hint2, hint2.get_rect(centerx=W // 2, y=box.y + 114))

        for who, m, key in (("NORA", nora_mind, "N"), ("IDA", ida_mind, "H")):
            if m and m.on:
                mind_panel.draw(screen, pygame.Rect(8, H - 178, W - 16, 170), m, f_med, f_sml, who,
                                note=f"{key}: off", colours=(TEXT, DIM, WARN, ACCENT))
                break

        # her from above: the wheels as she's driving (the pad held here, else her /status), walls on all four sides
        f, s, r = {"fw": (1, 0, 0), "bw": (-1, 0, 0), "left": (0, 1, 0), "right": (0, -1, 0)}.get(active_drive, (0, 0, 0))
        if not (f or s or r) and t.get("moving"):
            f = 1
        kit.draw_top_view(screen, pygame.Rect(W + 12, H - 300, IDA_W - 24, 250), SETTINGS.theme, SETTINGS.get("custom"), f_sml, "mecanum",
                          [f - s - r, f + s + r, f + s - r, f - s + r],
                          [(0, t.get("F", -1)), (90, t.get("L", -1)), (180, t.get("B", -1)), (-90, t.get("R", -1))])
        pygame.display.flip()
        clock.tick(60)

    stop_drive()
    link.stop()
    if FAKE["robot"]:
        FAKE["robot"].stop()
    if is_bt:
        link.ser.close()
    pygame.quit()
    sys.exit(0)


if __name__ == "__main__":
    main()
