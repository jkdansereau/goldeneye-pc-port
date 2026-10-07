# abpad — 1964 vs PC port A/B input harness

One millisecond timeline drives both sides of a faithful-vs-port comparison:

- **1964** reads a ViGEmBus virtual Xbox 360 pad (`vgamepad`) through NRage
  2.3c in XInput mode. abpad reads NRage's own mapping file and presses
  whatever Xbox input your profile maps to each N64 input, so you can keep
  your normal play profile.
- **The port** is launched with the same timeline converted to its own
  `GE_INPUTSCRIPT` (exact N64 bits, works unfocused, reproducible). It does
  not read the virtual pad. Its default pad layout (Jinx 1.1) maps buttons
  by context: in a level Y = N64 A and pad A = N64 B, while in menus A/X =
  N64 A. A single NRage mapping can't follow that switch.

Runs are **not frame-identical**: 1964 has no seed pin or frame dump, and the
port's script counts controller reads, not milliseconds. Use it to compare
timing and feel (fire rates, AI reactions, menus), not pixels.

## One-time setup

1. ViGEmBus 1.22 installed (it is: Sunshine's driver). `pip install vgamepad`
   is done. Note that a plain `pip install vgamepad` runs its bundled
   ViGEmBus 1.17 MSI if it doesn't recognise the installed bus. On
   2026-10-02 that downgraded the bus and had to be reverted.
2. 1964 → Plugins → Input = `NRage_Input_V2.dll` (2.3c), Controller 1 with
   **XInput** ticked. Any button layout works. abpad inverts
   `plugin/XInput Controller 1 Config.xcc` (`--nrage-dir`, default the
   `ge-port-reference` 1964 copy) and errors if a timeline needs an N64
   input that the profile leaves unmapped. File format, from NRage's
   `XInputController.cpp` ([libertyernie/nrage-input](https://github.com/libertyernie/nrage-input)):
   - Button keys (`A=`, `Z=`, `DUp=`, ...) are XInput button bitmasks.
   - `LeftTrigger=`/`RightTrigger=` are N64 button codes (Z = 0x20,
     L = 0x2000, ...).
   - Each `*Axis=` is either the N64 analog axis (0x4000 X, 0x8000 Y) or
     `(positive-direction code << 16) | negative-direction code` for a
     stick used as C or D buttons.
3. **The virtual pad must be XInput slot 0.** NRage reads N64 controller 1
   from XInput slot 0 only, and a real pad that is already connected holds
   it. Switch the real pad off, or unplug it, before a run. abpad probes
   slot 0 at startup and refuses to run otherwise.
4. Stick range is handled automatically. The port's script deflects the
   stick to ±80. abpad computes the thumb value that NRage turns into 80
   after its `PadDeadZone` (21248 at the default 5%); `--stick` changes the
   target.
5. `1964.cfg`: `PauseWhenInactive 0` (set 2026-10-02). Only one window
   has focus, and 1964 pauses when it's in the background.

## Timeline

`<ms>:<tok>[,<tok>...]` entries separated by `;` or newlines; `#` starts a
comment. Times are from harness start.

| Tokens | Meaning |
|---|---|
| `A B Z START L R UP DOWN LEFT RIGHT CUP CDOWN CLEFT CRIGHT` | 50 ms press (the port's `INPUTSCRIPT_PULSE_MS`) |
| `SUP SDOWN SLEFT SRIGHT` (combinable) / `SNONE` | analog stick, held until changed / recentre |
| `ZHOLD` / `ZREL` | hold / release fire |
| `RHOLD` / `LHOLD` / `RREL` | hold R or L (one aim hold) / release |

Port-only `GE_INPUTSCRIPT` tokens (`MDX`/`MDY`, `CHOLD`, `UHOLD`,
`RELOADHOLD`, ...) are rejected. The port caps a script at 64 entries.

## Running

The full side-by-side run (PowerShell; the real pad must be off):

```powershell
tools_pc\abpad\run_ab.ps1 -Timeline "19000:START;25000:SRIGHT;25300:SNONE;27000:SLEFT;27300:SNONE;29000:A" `
    -QuitFrame 2400 -Seconds 36 -Step 0.5
python tools_pc\abpad\sheet.py $env:TEMP\abpad-run 16 36 1   # side-by-side contact sheet
```

`run_ab.ps1` starts abpad (which launches the port), then 1964 with
`-c NRage_Input_V2.dll -g <Rom>` 1.2 s later, and screenshots both windows
every `-Step` seconds on one clock (`capture.ps1`, DPI-aware PrintWindow, so
overlapping windows are fine). 1964's `-g` takes a file name inside its
configured ROM directory and can't contain spaces, so keep a copy such as
`GoldenEyeUSA.n64` there. The port exits by itself through `GE_QUITFRAME`;
the script closes 1964 normally.

abpad on its own:

```sh
python tools_pc/abpad/abpad.py t.txt --print-script   # the port's GE_INPUTSCRIPT
python tools_pc/abpad/abpad.py t.txt --quitframe 2400  # both sides
python tools_pc/abpad/abpad.py t.txt --no-port         # 1964 only
python tools_pc/abpad/abpad.py t.txt --no-pad          # port only
```

- **Port timing:** abpad sets `GE_INPUTSCRIPT_MS=1`, so the port reads entry
  times as milliseconds since its SDL init instead of controller reads. Read
  rates vary through the intro and menus, so read counts can't be lined up
  with 1964. `--reads-per-ms` converts to a read-count script instead, for
  exes that don't have the millisecond mode.
- **1964 lags by ~2 s** (measured 2026-10-02): the 1.2 s launch gap plus
  1964's slower startup. `run_ab.ps1` defaults `-PadOffset 2000`, which
  delivers each input at the same game state on both sides. The screenshots
  then show 1964 about 2 s behind the port throughout.
- **Menus use the analog stick** (`SUP`/`SDOWN`/`SLEFT`/`SRIGHT` + `SNONE`),
  not the D-pad.
- The port is `build-pc/ge007.x86_64.exe`, launched from the repo root, so
  it uses the repo's `data/` (`SkipIntro = 0`). `build-pc/data` has the
  maintainer's play settings and skips the intro.

Verified 2026-10-02: START during the gun-barrel dots skipped to the title,
file select came up, stick right and left moved the folder cursor, and A
opened the folder. Both sides went through the same sequence.

## Checks before trusting a run

1. With `--no-port` and a long `0:ZHOLD` timeline, NRage's XInput dialog
   (or 1964 firing in a level) should show the profile's Z input held.
2. The port side needs no pad check. `GE_INPUTSCRIPT` is the port's sole
   controller-0 source when set, focused or not.
3. The two sides use different save files (the port's `data/ge007.eep`,
   1964's `save/`), so file-select contents can differ.

## Live A/B (one real pad)

`live_ab.ps1` launches 1964 GEPD and the port side by side at 1280x720 each (1964: GLideN64 windowed size; port: `data/ge007.ini` [Window]; the script only moves windows, never resizes), driven by ONE
real gamepad: the port reads it unfocused (`GE_PAD_BACKGROUND=1`), 1964 gets
it on click. 1964 starts ~0.8 s slower, so it launches first and the port
follows after `-PortDelayMs` (tune if the intros drift).

```
powershell -ExecutionPolicy Bypass -File tools_pc\abpad\live_ab.ps1 [-PortDelayMs 800] [-NoArrange]
```

The real pad must be ON (no virtual pad), and the NRage profile should match
the port's pad layout. `-DryRun` prints the launch lines without launching.
