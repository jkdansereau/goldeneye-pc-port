#!/usr/bin/env python3
"""A/B fidelity harness: one millisecond timeline drives 1964 and the PC port.

1964 gets the input from a ViGEmBus virtual Xbox 360 pad (vgamepad), read by
NRage 2.3c in XInput mode. The harness reads NRage's own mapping file
(`plugin/XInput Controller 1 Config.xcc`) and presses whatever Xbox input
that profile maps to each N64 input, so any profile works. The port does
NOT read that pad: its default Jinx 1.1 pad layout is context-dependent (in a
level Y = N64 A, pad A = N64 B; in menus A/X = N64 A), so no single NRage
mapping could line up with it. The same timeline is converted to the port's
own GE_INPUTSCRIPT (exact N64 bits, focus-independent) and the port is
launched with it.

Timeline: `<ms>:<tok>[,<tok>...];...` (also one entry per line; `#` comments).
Tokens are the subset of GE_INPUTSCRIPT that 1964 can reproduce:
  buttons (pulsed 50 ms = the port's INPUTSCRIPT_PULSE_MS):
    A B Z START L R UP DOWN LEFT RIGHT CUP CDOWN CLEFT CRIGHT
  analog stick (sustained until changed): SUP SDOWN SLEFT SRIGHT SNONE
  holds: ZHOLD ZREL (fire), RHOLD LHOLD RREL (one aim hold: R or L),
         CUPHOLD CDOWNHOLD CLEFTHOLD CRIGHTHOLD (combinable) / CNONE (C buttons)
Port-only tokens (MDX/MDY, CHOLD/CREL, UHOLD/UREL, RELOADHOLD/RELOADREL)
are rejected: 1964 has no equivalent.

Examples:
    python tools_pc/abpad/abpad.py t.txt --print-script
    python tools_pc/abpad/abpad.py t.txt --quitframe 3600
    python tools_pc/abpad/abpad.py "2000:START;4000:A" --no-port
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_NRAGE = ROOT.parent / "ge-port-reference" / "1964_GEPD_Edition" / "1964" / "plugin"

PULSE_MS = 50            # port: INPUTSCRIPT_PULSE_MS (GE_INPUTSCRIPT_MS mode)
PORT_SCRIPT_MAX = 64     # port: INPUTSCRIPT_MAX
TICK_HZ = 240            # pad update loop rate

BUTTONS = ("A", "B", "Z", "START", "L", "R", "UP", "DOWN", "LEFT", "RIGHT",
           "CUP", "CDOWN", "CLEFT", "CRIGHT")
STICK = {"SUP": (0, 1), "SDOWN": (0, -1), "SLEFT": (-1, 0), "SRIGHT": (1, 0)}
HOLDS = ("ZHOLD", "ZREL", "RHOLD", "LHOLD", "RREL")
# C-button holds (the port forces 1.2 SOLITARE: C-up/down walk, C-left/right strafe)
CHOLDS = {"CUPHOLD": "CUP", "CDOWNHOLD": "CDOWN", "CLEFTHOLD": "CLEFT", "CRIGHTHOLD": "CRIGHT"}
PORT_ONLY = ("CHOLD", "CREL", "UHOLD", "UREL", "RELOADHOLD", "RELOADREL")


@dataclass
class Entry:
    ms: int
    toks: list[str]
    buttons: set[str] = field(default_factory=set)
    stick: tuple[int, int] | None = None   # (x, y) in -1..1, None = unchanged
    zhold: bool | None = None
    aim: int | None = None                 # 0 none, 1 R, 2 L
    chold: frozenset | None = None         # held C buttons, None = unchanged


def parse_timeline(text: str) -> list[Entry]:
    entries: list[Entry] = []
    raw = []
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        raw.extend(p for p in line.split(";") if p.strip())
    for part in raw:
        if ":" not in part:
            raise SystemExit(f"abpad: entry without ':' -> {part.strip()!r}")
        ms_s, toks_s = part.split(":", 1)
        try:
            ms = int(ms_s.strip())
        except ValueError:
            raise SystemExit(f"abpad: bad ms {ms_s.strip()!r}")
        toks = [t.strip().upper() for t in toks_s.split(",") if t.strip()]
        e = Entry(ms, toks)
        sx = sy = 0
        has_stick = False
        for t in toks:
            if t in BUTTONS:
                e.buttons.add(t)
            elif t in STICK:
                dx, dy = STICK[t]
                sx, sy = sx or dx, sy or dy
                has_stick = True
            elif t == "SNONE":
                has_stick = True
            elif t == "ZHOLD":
                e.zhold = True
            elif t == "ZREL":
                e.zhold = False
            elif t in CHOLDS:
                e.chold = (e.chold or frozenset()) | {CHOLDS[t]}
            elif t == "CNONE":
                e.chold = frozenset()
            elif t in ("RHOLD", "LHOLD", "RREL"):
                e.aim = {"RHOLD": 1, "LHOLD": 2, "RREL": 0}[t]
            elif t in PORT_ONLY or t.startswith(("MDX", "MDY")):
                raise SystemExit(f"abpad: {t} is port-only (no 1964 equivalent)")
            else:
                raise SystemExit(f"abpad: unknown token {t!r}")
        if has_stick:
            e.stick = (sx, sy)
        entries.append(e)
    entries.sort(key=lambda e: e.ms)
    return entries


def to_inputscript(entries: list[Entry], reads_per_ms: float | None, offset_ms: int) -> str:
    """reads_per_ms None = GE_INPUTSCRIPT_MS mode (times stay in ms)."""
    if len(entries) > PORT_SCRIPT_MAX:
        raise SystemExit(f"abpad: {len(entries)} entries > port limit {PORT_SCRIPT_MAX}")
    out, last = [], -1
    for e in entries:
        t = e.ms + offset_ms
        frame = max(0, round(t if reads_per_ms is None else t * reads_per_ms))
        if frame <= last:          # two entries collapsed onto one read
            frame = last + 1
        last = frame
        out.append(f"{frame}:{','.join(e.toks)}")
    return ";".join(out)


def pad_state(entries: list[Entry], t_ms: float):
    """(buttons, stick, zhold, aim, chold) at t_ms, mirroring the port's script semantics."""
    buttons: set[str] = set()
    stick, zhold, aim, chold = (0, 0), False, 0, frozenset()
    for e in entries:
        if e.ms > t_ms:
            break
        if t_ms < e.ms + PULSE_MS:
            buttons |= e.buttons
        if e.stick is not None:
            stick = e.stick
        if e.zhold is not None:
            zhold = e.zhold
        if e.aim is not None:
            aim = e.aim
        if e.chold is not None:
            chold = e.chold
    return buttons, stick, zhold, aim, chold


# NRage 2.3c XInputController.h N64_BUTTONS codes (libertyernie/nrage-input).
N64_CODE = {"A": 0x0080, "B": 0x0040, "Z": 0x0020, "R": 0x1000, "L": 0x2000,
            "START": 0x0010, "UP": 0x0008, "DOWN": 0x0004, "LEFT": 0x0002,
            "RIGHT": 0x0001, "CUP": 0x0800, "CDOWN": 0x0400, "CLEFT": 0x0200,
            "CRIGHT": 0x0100}
N64_XAXIS, N64_YAXIS = 0x4000, 0x8000
XCC_BUTTON_KEY = {"A": "A", "B": "B", "Z": "Z", "R": "R", "L": "L", "START": "Start",
                  "UP": "DUp", "DOWN": "DDown", "LEFT": "DLeft", "RIGHT": "DRight",
                  "CUP": "CUp", "CDOWN": "CDown", "CLEFT": "CLeft", "CRIGHT": "CRight"}
XCC_AXES = {"LeftXAxis": ("L", 0), "LeftYAxis": ("L", 1),
            "RightXAxis": ("R", 0), "RightYAxis": ("R", 1)}


def load_nrage(plugin_dir: Path):
    """Invert NRage's XInput mapping: N64 input -> the Xbox input that makes it.

    Returns (sources, stick_axes, deadzone_pct). sources[name] is one of
    ("btn", xusb_bit), ("trig", "L"|"R"), ("axis", "L"|"R", axis, +1|-1).
    stick_axes = {0: (stick, axis) driving N64 X, 1: ... N64 Y}.
    """
    xcc = plugin_dir / "XInput Controller 1 Config.xcc"
    if not xcc.is_file():
        raise SystemExit(f"abpad: no NRage XInput config at {xcc} (--nrage-dir)")
    cfg = {}
    for line in xcc.read_text(encoding="ascii", errors="replace").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            cfg[k.strip()] = int(v.strip() or 0)
    sources, stick_axes = {}, {}
    for name, code in N64_CODE.items():
        mask = cfg.get(XCC_BUTTON_KEY[name], 0)
        if mask:
            sources[name] = ("btn", mask & -mask)          # lowest bound Xbox button
        elif cfg.get("LeftTrigger") == code:
            sources[name] = ("trig", "L")
        elif cfg.get("RightTrigger") == code:
            sources[name] = ("trig", "R")
        else:
            for key, (stick, axis) in XCC_AXES.items():
                v = cfg.get(key, 0)
                if v in (N64_XAXIS, N64_YAXIS):
                    continue
                if (v >> 16) & code:                       # positive deflection
                    sources[name] = ("axis", stick, axis, +1)
                    break
                if v & 0xFFFF & code:                      # negative deflection
                    sources[name] = ("axis", stick, axis, -1)
                    break
    for key, (stick, axis) in XCC_AXES.items():
        if cfg.get(key) == N64_XAXIS:
            stick_axes.setdefault(0, (stick, axis))
        elif cfg.get(key) == N64_YAXIS:
            stick_axes.setdefault(1, (stick, axis))
    deadzone = 0
    ini = plugin_dir / "NRage.ini"
    if ini.is_file():
        sect = None
        for line in ini.read_text(encoding="ascii", errors="replace").splitlines():
            line = line.strip()
            if line.startswith("["):
                sect = line
            elif sect == "[Controller 1]" and line.startswith("PadDeadZone="):
                deadzone = int(line.split("=", 1)[1])
    return sources, stick_axes, deadzone


def nrage_stick_value(n64: int, deadzone_pct: int) -> int:
    """Smallest XInput thumb value NRage turns into n64 (AxisDeadzone, then *127/32767)."""
    if n64 <= 0:
        return 0
    dz = deadzone_pct * 32767 // 100
    rel = 32767.0 / (32767 - dz) if dz else 1.0
    for raw in range(1, 32768):
        v = raw
        if dz:
            v = 0 if raw < dz else min(32767.0, (raw - dz) * rel)
        if int(v) * 127 // 32767 >= n64:
            return raw
    return 32767


def xinput_slot0_rx() -> int | None:
    import ctypes

    class GP(ctypes.Structure):
        _fields_ = [("w", ctypes.c_ushort), ("lt", ctypes.c_ubyte), ("rt", ctypes.c_ubyte),
                    ("lx", ctypes.c_short), ("ly", ctypes.c_short),
                    ("rx", ctypes.c_short), ("ry", ctypes.c_short)]

    class ST(ctypes.Structure):
        _fields_ = [("pkt", ctypes.c_uint), ("gp", GP)]

    st = ST()
    if ctypes.WinDLL("xinput1_4").XInputGetState(0, ctypes.byref(st)) != 0:
        return None
    return st.gp.rx


def run_pad(entries: list[Entry], t0: float, offset_ms: int, linger_ms: int,
            plugin_dir: Path, stick_n64: int) -> None:
    import vgamepad as vg   # imported late so --print-script works without it

    sources, stick_axes, deadzone = load_nrage(plugin_dir)
    used = set()
    for e in entries:
        used |= e.buttons
        if e.zhold:
            used.add("Z")
        if e.aim:
            used.add("R" if e.aim == 1 else "L")
        if e.chold:
            used |= e.chold
    missing = sorted(n for n in used if n not in sources)
    if missing:
        raise SystemExit(f"abpad: the NRage profile maps nothing to {', '.join(missing)}")
    if any(e.stick not in (None, (0, 0)) for e in entries) and len(stick_axes) < 2:
        raise SystemExit("abpad: the NRage profile maps no Xbox stick to the N64 analog stick")
    full = nrage_stick_value(stick_n64, deadzone)

    pad = vg.VX360Gamepad()
    # NRage reads N64 controller 1 from XInput slot 0. Probe with an
    # odd right-stick X value inside NRage's deadzone (no N64 effect) to
    # confirm the virtual pad got slot 0 rather than a real pad.
    probe = 777
    # A new pad takes ~0.3 s to enumerate and drops reports sent before that
    # (it then reads back uninitialised stick values), so keep re-sending.
    deadline = time.perf_counter() + 3.0
    slot0 = None
    while time.perf_counter() < deadline and slot0 != probe:
        pad.right_joystick(x_value=probe, y_value=0)
        pad.update()
        time.sleep(0.05)
        slot0 = xinput_slot0_rx()
    pad.reset()
    pad.update()
    if slot0 != probe:
        del pad
        raise SystemExit("abpad: the virtual pad is not XInput slot 0 (a real pad holds it). "
                         "NRage reads N64 controller 1 from slot 0: disconnect the real pad "
                         "(or start abpad before plugging it in) and retry.")
    end_ms = (entries[-1].ms + PULSE_MS if entries else 0) + linger_ms
    prev, last_send = None, 0.0
    try:
        while True:
            t_ms = (time.perf_counter() - t0) * 1000.0 - offset_ms
            if t_ms > end_ms:
                break
            buttons, stick, zhold, aim, chold = pad_state(entries, t_ms)
            state = (frozenset(buttons), stick, zhold, aim, chold)
            now = time.perf_counter()
            if state != prev or now - last_send > 0.1:   # periodic resend: dropped reports
                held = set(buttons) | chold
                if zhold:
                    held.add("Z")
                if aim:
                    held.add("R" if aim == 1 else "L")
                wbuttons, trig = 0, {"L": 0, "R": 0}
                axes = {("L", 0): 0, ("L", 1): 0, ("R", 0): 0, ("R", 1): 0}
                for i, d in enumerate(stick):
                    if d:
                        axes[stick_axes[i]] = d * full
                for n in held:
                    src = sources[n]
                    if src[0] == "btn":
                        wbuttons |= src[1]
                    elif src[0] == "trig":
                        trig[src[1]] = 255
                    else:
                        axes[(src[1], src[2])] = src[3] * 32767
                pad.report.wButtons = wbuttons
                pad.left_trigger(value=trig["L"])
                pad.right_trigger(value=trig["R"])
                pad.left_joystick(x_value=axes[("L", 0)], y_value=axes[("L", 1)])
                pad.right_joystick(x_value=axes[("R", 0)], y_value=axes[("R", 1)])
                pad.update()
                prev, last_send = state, now
            time.sleep(1.0 / TICK_HZ)
    finally:
        pad.reset()
        pad.update()
        del pad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("timeline", help="timeline file, or an inline timeline string")
    ap.add_argument("--print-script", action="store_true",
                    help="print the port GE_INPUTSCRIPT and exit")
    ap.add_argument("--no-port", action="store_true", help="drive the pad (1964) only")
    ap.add_argument("--no-pad", action="store_true", help="launch the port only")
    ap.add_argument("--port-exe", default=str(ROOT / "build-pc" / "ge007.x86_64.exe"))
    ap.add_argument("--port-cwd", default=None, help="default: the exe's directory")
    ap.add_argument("--port-offset-ms", type=int, default=0,
                    help="shift the port's timeline (e.g. to absorb a boot-time difference)")
    ap.add_argument("--pad-offset-ms", type=int, default=0,
                    help="shift the 1964 pad timeline")
    ap.add_argument("--reads-per-ms", type=float, default=None,
                    help="convert to the port's read-count script instead of GE_INPUTSCRIPT_MS "
                         "(older exes; ~0.12 in a level, lower in the intro)")
    ap.add_argument("--quitframe", type=int, default=None,
                    help="GE_QUITFRAME for the port (always set one for unattended runs)")
    ap.add_argument("--nrage-dir", default=str(DEFAULT_NRAGE),
                    help="1964 plugin dir holding NRage.ini and 'XInput Controller 1 Config.xcc'")
    ap.add_argument("--stick", type=int, default=80,
                    help="N64 stick deflection for SUP/SDOWN/SLEFT/SRIGHT (the port script uses 80)")
    ap.add_argument("--linger-ms", type=int, default=1000,
                    help="keep the virtual pad plugged this long after the last entry")
    args = ap.parse_args()

    p = Path(args.timeline)
    text = p.read_text(encoding="utf-8") if p.is_file() else args.timeline
    entries = parse_timeline(text)
    if not entries:
        raise SystemExit("abpad: empty timeline")
    script = to_inputscript(entries, args.reads_per_ms, args.port_offset_ms)
    if args.print_script:
        print(script)
        return 0

    proc = None
    t0 = time.perf_counter()
    if not args.no_port:
        env = dict(os.environ, GE_INPUTSCRIPT=script)
        if args.reads_per_ms is None:
            env["GE_INPUTSCRIPT_MS"] = "1"   # port times are ms since its SDL init
        if args.quitframe is not None:
            env["GE_QUITFRAME"] = str(args.quitframe)
        exe = Path(args.port_exe)
        proc = subprocess.Popen([str(exe)], cwd=args.port_cwd or str(exe.parent), env=env)
        print(f"abpad: port pid {proc.pid}, GE_INPUTSCRIPT={script}")
    if not args.no_pad:
        print(f"abpad: driving virtual pad, {len(entries)} entries, last at {entries[-1].ms} ms")
        run_pad(entries, t0, args.pad_offset_ms, args.linger_ms,
                Path(args.nrage_dir), args.stick)
        print("abpad: pad timeline done")
    if proc is not None:
        return proc.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
