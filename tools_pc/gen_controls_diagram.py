#!/usr/bin/env python3
"""Generate docs/img/controls.svg (+ .png when rsvg-convert/sips is available).

Default bindings mirror kBindDefs / kPadDefs in port/src/input.c. Regenerate
after changing them:  python3 tools_pc/gen_controls_diagram.py
"""
import os, subprocess, shutil

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "img")
os.makedirs(OUT, exist_ok=True)

KEYS = [  # (row, x, label, width, action)
    (0, 0.0, "Esc", 1, "Cancel"), (0, 2.0, "F10", 1, "Options"), (0, 3.5, "F12", 1, "Screenshot"),
    (1, 0.0, "Tab", 1.5, "Start"), (1, 1.5, "Q", 1, "Lean left"), (1, 2.5, "W", 1, "Forward"),
    (1, 3.5, "E", 1, "Action"), (1, 4.5, "R", 1, "Cancel"),
    (2, 0.0, "Shift", 1.75, "Aim"), (2, 1.75, "A", 1, "Strafe L"), (2, 2.75, "S", 1, "Back"),
    (2, 3.75, "D", 1, "Strafe R"), (2, 4.75, "F", 1, "Cancel"),
    (3, 0.0, "Ctrl", 1.5, "Fire"), (3, 1.5, "Z", 1, "Action"), (3, 2.5, "X", 1, "Cancel"),
    (4, 0.0, "Space", 4, "Action"),
    (5, 6.0, "Left", 1, "Turn L"), (5, 7.0, "Down", 1, "Back"), (5, 8.0, "Right", 1, "Turn R"),
    (4, 7.0, "Up", 1, "Forward"),
    (2, 6.0, "Return", 1.5, "Start"),
]
PAD = [  # label, x, y, action, side
    ("LT", 120, 40, "Aim", "l"), ("LB", 120, 90, "Previous weapon", "l"),
    ("RT", 480, 40, "Fire", "r"), ("RB", 480, 90, "Next weapon", "r"),
    ("Left stick", 190, 250, "Move (forward/back/strafe)", "l"),
    ("Right stick", 410, 330, "Look / aim", "r"),
    ("D-pad", 200, 340, "Menu navigation", "l"),
    ("A / X", 470, 240, "Action / use / reload", "r"),
    ("B / Y", 520, 200, "Crouch / cancel", "r"),
    ("Start", 330, 200, "Pause / Start", "r"),
    ("Back / Select", 270, 200, "Open options (F10)", "l"),
]

U = 46
def kb():
    s = ['<g transform="translate(40,90)">']
    for r, x, lab, w, act in KEYS:
        px, py, pw = x * U, r * (U + 26), w * U
        s.append(f'<rect x="{px}" y="{py}" width="{pw-4}" height="{U}" rx="6" fill="#26303f" stroke="#7aa2ff"/>')
        s.append(f'<text x="{px+pw/2-2}" y="{py+22}" text-anchor="middle" font-size="14" fill="#fff" font-weight="bold">{lab}</text>')
        s.append(f'<text x="{px+pw/2-2}" y="{py+U+16}" text-anchor="middle" font-size="11" fill="#ffd479">{act}</text>')
    s.append('</g>')
    return "\n".join(s)

def pad():
    s = ['<g transform="translate(620,80)">',
         '<path d="M120 100 Q100 60 160 60 L440 60 Q500 60 480 100 L520 320 Q540 400 480 380 L400 320 L200 320 L140 380 Q80 400 100 320 Z" fill="#1c2430" stroke="#7aa2ff" stroke-width="2"/>']
    for lab, x, y, act, side in PAD:
        anchor = "end" if side == "l" else "start"
        tx = x - 14 if side == "l" else x + 14
        s.append(f'<circle cx="{x}" cy="{y}" r="6" fill="#ffd479"/>')
        s.append(f'<text x="{tx}" y="{y-2}" text-anchor="{anchor}" font-size="13" fill="#fff" font-weight="bold">{lab}</text>')
        s.append(f'<text x="{tx}" y="{y+13}" text-anchor="{anchor}" font-size="11" fill="#ffd479">{act}</text>')
    s.append('</g>')
    return "\n".join(s)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="824" height="330" viewBox="0 0 1400 560" font-family="Helvetica, Arial, sans-serif">
<rect width="1400" height="560" fill="#0e131b"/>
<text x="40" y="40" font-size="22" fill="#fff" font-weight="bold">GoldenEye 007 PC - default controls</text>
<text x="40" y="66" font-size="13" fill="#9fb0c8">Keyboard + mouse (left), gamepad (right). Mouse: move = look, click = fire, right click = aim, wheel = weapon.</text>
{kb()}
{pad()}
<text x="40" y="540" font-size="12" fill="#9fb0c8">Edit data/ge007.ini ([Input.Bind] and Input.Pad.*) or use F10 -> "Key:" / "Pad:" rows. Source: tools_pc/gen_controls_diagram.py</text>
</svg>'''
p = os.path.join(OUT, "controls.svg")
open(p, "w").write(svg)
print("wrote", p)
for cmd in (["rsvg-convert", "-o", os.path.join(OUT, "controls.png"), p],):
    if shutil.which(cmd[0]):
        subprocess.run(cmd, check=False); print("wrote controls.png")
