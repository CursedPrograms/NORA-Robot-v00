"""fake_nora.py - a pretend NORA on this PC, to try the controller with no robot (EMULATE).

Answers the same web calls as her esp32.ino on :5002 (/sensors, /fw ... /turnR, /drive, /stop, the
modes, UV, music, speed, the wheel lock, /unlock, /mind, /ida, /link, /getcal, /serial) and her
fleet list on :5000 (/robots), and acts them out: she drives about a pretend room, her four sonars
change as she goes, she won't drive into a wall closer than 15 cm (her MANUAL_STOP_CM), and she
boots with her wheels locked, as she does. Every command she gets is printed.

    python scripts/fake_nora.py         then the controller: python scripts/controller.py --emulate
                                        (or pick EMULATE on its connection screen: it starts one itself)
"""

import math
import random
import time

from controller_kit import FakeRobot

MOTOR_PASSWORD = "1234"   # secrets.h's (a safety catch, not security)
MODES = ["Manual", "Auto", "Line", "IR", "Mind"]
STOP_CM = 15              # MANUAL_STOP_CM
ROOM = (300.0, 240.0)     # cm, the pretend room; she starts in the middle facing +x
DRIVE_TIMEOUT_S = 0.5     # DRIVE_MIX_TIMEOUT_MS
MIND_STEP_S = 0.4


class FakeNora(FakeRobot):
    NAME = "NORA"
    PORTS = (5002, 5000)

    def __init__(self, host="127.0.0.1"):
        self.mode, self.locked, self.speed, self.uv = 0, True, 100, 0
        self.track, self.music = 100, 0
        self.x, self.y, self.heading = ROOM[0] / 2, ROOM[1] / 2, 0.0
        self.motion = (0.0, 0.0, 0.0)      # forward, left, turn: what her wheels do now (-1..1)
        self.drive_at = 0.0                # last /drive (it times out); /fw etc. hold until /stop
        self.blended = False
        self.mind_until, self.mind_host_seen, self.blocked = 0.0, 0.0, False
        self.auto_turn = 0.0
        super().__init__(host)

    # ---- her pretend room ----
    def _distance(self, angle):
        """cm from her to the wall in this direction (her heading + angle)."""
        a = self.heading + angle
        dx, dy = math.cos(a), math.sin(a)
        best = 400.0
        if dx > 1e-6: best = min(best, (ROOM[0] - self.x) / dx)
        if dx < -1e-6: best = min(best, -self.x / dx)
        if dy > 1e-6: best = min(best, (ROOM[1] - self.y) / dy)
        if dy < -1e-6: best = min(best, -self.y / dy)
        return round(max(2.0, best), 1)

    def distances(self):
        return {"F": self._distance(0), "L": self._distance(math.pi / 2), "B": self._distance(math.pi),
                "R": self._distance(-math.pi / 2)}

    def _blocked(self, f, s):
        d = self.distances()
        return (f > 0 and d["F"] < STOP_CM) or (f < 0 and d["B"] < STOP_CM) or \
               (s > 0 and d["L"] < STOP_CM) or (s < 0 and d["R"] < STOP_CM)

    def step(self, dt):
        now = time.monotonic()
        f, s, t = self.motion
        if self.blended and now - self.drive_at > DRIVE_TIMEOUT_S:   # /drive went quiet: she lets go
            self._set_motion(0, 0, 0, "the controller went quiet: she stops")
            f = s = t = 0
        if self.mode == 4 and self.mind_until and now > self.mind_until:
            self.mind_until = 0
            self._set_motion(0, 0, 0)
            f = s = t = 0
        if self.mode == 1:   # AUTO: drive, turn away from walls
            d = self.distances()
            f, s, t = (0, 0, 1) if d["F"] < 30 else (1, 0, 0)
        if self.locked:
            f = s = t = 0
        if self.mode in (0, 3, 4):
            d = self.distances()
            if f > 0 and d["F"] < STOP_CM: f = 0
            if f < 0 and d["B"] < STOP_CM: f = 0
            if s > 0 and d["L"] < STOP_CM: s = 0
            if s < 0 and d["R"] < STOP_CM: s = 0
        v = 40 * self.speed / 100   # cm/s at full
        c, sn = math.cos(self.heading), math.sin(self.heading)
        self.x = min(ROOM[0] - 1, max(1, self.x + (f * c - s * sn) * v * dt))
        self.y = min(ROOM[1] - 1, max(1, self.y + (f * sn + s * c) * v * dt))
        self.heading += t * 1.5 * dt

    def _set_motion(self, f, s, t, why=None):
        if (f, s, t) != self.motion:
            names = []
            if f: names.append("forward" if f > 0 else "backward")
            if s: names.append("slide left" if s > 0 else "slide right")
            if t: names.append("turn left" if t > 0 else "turn right")
            self.log(why or ("drives " + " + ".join(names) if names else "stops"))
        self.motion = (f, s, t)

    def status(self):
        now = time.monotonic()
        d = self.distances()
        moving = any(self.motion) and not self.locked
        return {"mindHost": int(now - self.mind_host_seen < 5), "blocked": int(self.blocked),
                "mindSteps": int(self.mind_until > now) and max(1, int((self.mind_until - now) / MIND_STEP_S + 0.99)),
                **d, "lfl": 0, "lfm": 1, "lfr": 0, "mode": self.mode, "dir": 0, "uv": self.uv,
                "mt": self.track, "ms": self.music, "lt": 62, "snd": 0, "lock": int(self.locked),
                "spd": self.speed, "temp": 21.5, "hum": 40.0, "msrc": "web", "sd": 1, "nt": 24,
                "moving": int(moving)}

    # ---- her web calls ----
    def route(self, port, path, q):
        if port == 5000:
            if path == "/robots":
                return 200, {"authority": "NORA", "robots": [
                    {"name": "NORA", "type": "mecanum", "ip": "127.0.0.1"},
                    {"name": "IDA (pretend)", "type": "tracked", "ip": "-"}]}
            return 404, "not found"
        now = time.monotonic()
        if path == "/sensors":
            if q.get("ctl") == "1" and self.mind_host_seen:
                self.mind_host_seen = now
            return 200, self.status()
        if path == "/getcal":
            return 200, {"m1": 255, "m2": 255, "m3": 255, "m4": 255, "spd": self.speed}
        moves = {"/fw": (1, 0, 0), "/bw": (-1, 0, 0), "/left": (0, 1, 0), "/right": (0, -1, 0),
                 "/turnL": (0, 0, 1), "/turnR": (0, 0, -1)}
        if path in moves:
            if self.mode == 0:
                f, s, t = moves[path]
                if self._blocked(f, s):
                    self.log(f"won't go {path[1:]}: a wall within {STOP_CM} cm")
                else:
                    self.blended = False
                    self._set_motion(f, s, t)
            return 200, "OK"
        if path == "/drive":
            if self.mode == 0:
                f, s, t = (max(-1.0, min(1.0, float(q.get(k, 0) or 0))) for k in "fst")
                self.blended, self.drive_at = True, now
                self._set_motion(f, s, t)
            return 200, "OK"
        if path == "/stop":
            if self.mode == 0:
                self.blended = False
                self._set_motion(0, 0, 0)
            return 200, "OK"
        if path.startswith("/mode") and path[5:] in MODES:
            self.mode = MODES.index(path[5:])
            self.blended = False
            self.log(f"mode -> {path[5:].upper()}")
            self._set_motion(0, 0, 0)
            return 200, "OK"
        if path in ("/uvOff", "/uvOn", "/uvBlink"):
            self.uv = ["/uvOff", "/uvOn", "/uvBlink"].index(path)
            self.log(["UV off", "UV on", "UV blinking"][self.uv])
            return 200, "OK"
        if path.startswith("/mu"):
            cmd = path[3:]
            if cmd == "Play":
                self.music = 2 if self.music == 1 else 1
            elif cmd in ("Next", "Prev"):
                self.track += 1 if cmd == "Next" else -1
                self.music = 1
            elif cmd == "Stop":
                self.music = 0
            self.log(f"music: {['stopped', 'playing', 'paused'][self.music]} (track {self.track:03d})")
            return 200, "OK"
        if path == "/setspeed":
            self.speed = max(0, min(100, int(float(q.get("v", self.speed)))))
            self.log(f"speed {self.speed}%")
            return 200, "OK"
        if path == "/motorlockOn":
            self.locked = True
            self._set_motion(0, 0, 0, "wheels locked")
            return 200, "OK"
        if path == "/motorlockOff":
            if q.get("pw") != MOTOR_PASSWORD:
                self.log("unlock: wrong password")
                return 403, "bad password"
            self.locked = False
            self.log("wheels free")
            return 200, "OK"
        if path == "/unlock":
            if q.get("pw") != MOTOR_PASSWORD:
                return 403, {"ok": False}
            if q.get("ctl") == "1":
                self.mind_host_seen = now
                self.log("her mind's controller checked in")
            return 200, {"ok": True}
        if path == "/mind":
            if self.mode != 4:
                return 409, {"ok": False, "error": "not in mind mode"}
            act = q.get("act")
            steps = max(1, min(4, int(q.get("steps", 1) or 1)))
            if act == "stop":
                self.mind_until = 0
                self._set_motion(0, 0, 0)
                return 200, {"ok": True}
            if act not in ("fw", "bw", "left", "right", "turnL", "turnR"):
                return 400, {"ok": False, "error": "bad act"}
            f, s, t = moves["/" + act]
            self.blocked = self._blocked(f, s)
            if self.blocked:
                self.log(f"mind: {act} refused, wall in the way")
                return 200, {"ok": False, "blocked": True}
            self.mind_until = now + steps * MIND_STEP_S
            self._set_motion(f, s, t, f"mind: {act} x{steps}")
            return 200, {"ok": True}
        if path == "/serial":
            self.log(f"to her UNO: {q.get('cmd', '')}")
            return 200, "OK"
        if path in ("/ida", "/link"):
            who = q.get("r", "ida")
            self.log(f"IR link -> {who.upper()}: {q.get('c', '')}")
            return 200, "OK"
        return 404, "not found"


def start(quiet=False):
    """A fresh fake NORA on 127.0.0.1:5002 (+ :5000). OSError if a port's taken."""
    robot = FakeNora()
    robot.quiet = quiet
    return robot


if __name__ == "__main__":
    try:
        nora = start()
    except OSError as e:
        raise SystemExit(f"Can't open :5002 / :5000 ({e}). Is another fake NORA (or WHIP's fake fleet) running?")
    print("Fake NORA at http://127.0.0.1:5002 (fleet list :5000). Ctrl+C stops her.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        nora.stop()
