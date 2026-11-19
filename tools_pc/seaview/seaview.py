#!/usr/bin/env python
"""seaview: look at and tune the GE+ sky and sea outside the game, on the PC and on the console.

  python tools_pc/seaview/seaview.py dump              capture one frame of Frigate in the Switch emulator (data/sea_dump.bin)
  python tools_pc/seaview/seaview.py pc [--hp]         build and open the PC viewer (--hp: ask for the discrete GPU)
  python tools_pc/seaview/seaview.py nx-build          build the console viewer (nx/seaview-nx.nro)
  python tools_pc/seaview/seaview.py nx-send [--ini F] build if needed, send to the console with the settings of a viewer .ini
  python tools_pc/seaview/seaview.py ini2args F        print the console arguments an .ini turns into

Machine settings come from <repo>/.env (see .env.example). The capture holds GE's cloud texture (ROM-derived):
it stays in tools_pc/seaview/data/, which git ignores.
"""
import argparse, math, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(HERE, "data")
NXDIR = os.path.join(HERE, "nx")
DUMP = os.path.join(DATA, "sea_dump.bin")
FRIGATE = ["-level_26", "-hard0", "NX_TEST_SECS=3600", "GE_TELEPORT=595.3,521.9,690.4,296"]


def load_env():
    env = {}
    p = os.path.join(REPO, ".env")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip().strip('"')
    for k in ("SWITCH_IP", "SWITCH_EMU_EXE", "SWITCH_EMU_SD", "DOCKER_IMAGE", "SEAVIEW_GXX"):
        if os.environ.get(k):
            env[k] = os.environ[k]
    env.setdefault("DOCKER_IMAGE", "local/project-dam-build:latest")
    return env


def need(env, key):
    if not env.get(key):
        sys.exit(f"seaview: set {key} in {os.path.join(REPO, '.env')} (see .env.example)")
    return env[key]


def docker(env, script):
    """run a shell script in the build image, the repo mounted at /src"""
    cmd = ["docker", "run", "--rm", "-v", REPO.replace("\\", "/") + ":/src", "-v", "project-dam-ccache:/ccache", "-w", "/src", env["DOCKER_IMAGE"], "sh", "-c", script]
    e = dict(os.environ, MSYS_NO_PATHCONV="1")
    r = subprocess.run(cmd, env=e)
    if r.returncode != 0:
        sys.exit(f"seaview: the build in {env['DOCKER_IMAGE']} failed ({r.returncode})")


# ---------------------------------------------------------------- dump
def cmd_dump(env, a):
    emu = need(env, "SWITCH_EMU_EXE")
    sd = need(env, "SWITCH_EMU_SD")
    print("building the game (build-switch-args)...")
    docker(env, "cmake --build build-switch-args 2>&1 | grep -iE 'error|failed' ; true")
    nro = os.path.join(REPO, "build-switch-args", "ge007.aarch64.nro")
    gamedir = os.path.join(sd, "switch", "project-dam")
    argsf = os.path.join(gamedir, "data", "ge007.args")
    out = os.path.join(gamedir, "data", "sea_dump.bin")
    os.makedirs(os.path.dirname(argsf), exist_ok=True)
    saved = open(argsf, "rb").read() if os.path.exists(argsf) else None
    try:
        open(argsf, "w").write(" ".join(FRIGATE + ["GE_SEADUMP=1"]))
        shutil.copy(nro, os.path.join(gamedir, "project-dam.nro"))
        if os.path.exists(out):
            os.remove(out)
        print("launching the emulator on Frigate; the capture is written after ~400 frames")
        p = subprocess.Popen([emu, os.path.join(gamedir, "project-dam.nro")])
        t0 = time.time()
        while time.time() - t0 < 240 and not (os.path.exists(out) and os.path.getsize(out) == 69888):
            time.sleep(2)
        if not os.path.exists(out):
            sys.exit("seaview: no capture after 4 minutes (is the game reaching Frigate?)")
        time.sleep(1)
        os.makedirs(DATA, exist_ok=True)
        shutil.copy(out, DUMP)
        print("captured:", DUMP)
        if a.close:
            p.terminate()
    finally:
        if saved is not None:
            open(argsf, "wb").write(saved)


# ---------------------------------------------------------------- PC viewer
def cmd_pc(env, a):
    gxx = need(env, "SEAVIEW_GXX")
    if not os.path.exists(DUMP):
        sys.exit("seaview: no capture yet, run: seaview.py dump")
    os.makedirs(os.path.join(HERE, "build"), exist_ok=True)
    exe = os.path.join(HERE, "build", "seaview_hp.exe" if a.hp else "seaview.exe")
    cmd = [gxx, "-O2", "-o", exe, os.path.join(HERE, "pc", "seaview.cpp"), "-lopengl32", "-lgdi32", "-luser32", "-lcomctl32", "-lcomdlg32", "-static"]
    if a.hp:
        cmd.insert(1, "-DSEAVIEW_HP")
    e = dict(os.environ, PATH=os.path.dirname(gxx) + os.pathsep + os.environ.get("PATH", ""))
    r = subprocess.run(cmd, env=e)
    if r.returncode != 0:
        sys.exit("seaview: the PC viewer did not build")
    shaders = os.path.join(REPO, "port", "switch", "shaders") + os.sep
    detached = 0x00000008 | 0x00000200 if os.name == "nt" else 0   # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP: outlive this script
    subprocess.Popen([exe, "--shaders", shaders, "--dump", DUMP, "--sway", "1"], cwd=DATA, creationflags=detached)
    print("PC viewer open; its settings are saved in", DATA)


# ---------------------------------------------------------------- console viewer
NX_HEADER = ("#define VIEWER\n"
             "layout(std140, binding = 1) uniform Viewer { ivec4 uVarv; vec4 uPrv[6]; };\n"
             "#define uSeaVar uVarv.x\n#define uSkyVar uVarv.y\n")


PRM_DEFAULTS = [100000.0, 5.0, 0.9, 0.08, 0.5, 0.35, 0.35, 1.45, 1.0, 600.0, 6000.0, 5.0, 0.006, 0.82, 0.3, 0.8]   # the PRM(i, d) defaults


def baked_header(ini):
    """the constants of a viewer .ini as a shader header: compiled like the game's own build, no uniform block, no dead variants"""
    pr, sea, sky = list(PRM_DEFAULTS), 0, 0
    if ini:
        t = par_table()
        for line in open(ini, encoding='utf-8'):
            if '=' not in line:
                continue
            k, v = line.rstrip('\n').split('=', 1)
            if k == '__sea': sea = int(float(v))
            elif k == '__sky': sky = int(float(v))
            elif k in t and t[k][0] == 0 and t[k][1] < len(pr): pr[t[k][1]] = float(v)
    vals = ', '.join(f'{x:.9g}' if '.' in f'{x:.9g}' or 'e' in f'{x:.9g}' else f'{x:.9g}.0' for x in pr)
    return ('#define VIEWER_CONST\n'
            f'#define SV(n) ((n) == {sea})\n#define SK(n) ((n) == {sky})\n'
            f'const float kPrc[{len(pr)}] = float[{len(pr)}]({vals});\n#define PRM(i, d) kPrc[i]\n')


def cmd_nx_build(env, a):
    if not os.path.exists(DUMP):
        sys.exit("seaview: no capture yet, run: seaview.py dump")
    romfs = os.path.join(NXDIR, "romfs")
    gen = os.path.join(NXDIR, "build", "glsl")
    os.makedirs(romfs, exist_ok=True)
    os.makedirs(gen, exist_ok=True)
    shutil.copy(DUMP, os.path.join(romfs, "sea_dump.bin"))
    sh = os.path.join(REPO, "port", "switch", "shaders")
    common = open(os.path.join(sh, "skycommon.glsl"), encoding="utf-8").read()
    for name in ("skyplus", "skylut"):
        src = open(os.path.join(sh, name + ".frag.glsl"), encoding="utf-8").read().replace("#pragma skycommon", common)
        nl = src.index("\n")
        header = NX_HEADER if getattr(a, 'live', False) else baked_header(getattr(a, 'ini', None))
        open(os.path.join(gen, name + ".frag.glsl"), "w", encoding="utf-8", newline="\n").write(src[:nl + 1] + header + src[nl + 1:])
    rel = "tools_pc/seaview/nx"
    script = " && ".join([
        "set -e",
        "export PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH",
        f"uam -s vert -o {rel}/romfs/fullscreen.vert.dksh port/switch/shaders/fullscreen.vert.glsl",
        f"uam -s frag -o {rel}/romfs/skyplus.frag.dksh {rel}/build/glsl/skyplus.frag.glsl",
        f"uam -s frag -o {rel}/romfs/skylut.frag.dksh {rel}/build/glsl/skylut.frag.glsl",
        f"uam -s frag -o {rel}/romfs/overlay.frag.dksh {rel}/shaders/overlay.frag.glsl",
        "aarch64-none-elf-g++ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -O2 -std=gnu++17 -D__SWITCH__ "
        f"-I$DEVKITPRO/libnx/include -specs=$DEVKITPRO/libnx/switch.specs {rel}/source/main.cpp "
        f"-L$DEVKITPRO/libnx/lib -ldeko3d -lnx -lm -o {rel}/build/seaview-nx.elf",
        f"nacptool --create seaview project-dam 1.0 {rel}/build/seaview-nx.nacp",
        f"elf2nro {rel}/build/seaview-nx.elf {rel}/seaview-nx.nro --nacp={rel}/build/seaview-nx.nacp --romfsdir={rel}/romfs",
    ])
    docker(env, script)
    print("built:", os.path.join(NXDIR, "seaview-nx.nro"))


def par_table():
    """the PC viewer's settings table: name -> (kind, index)"""
    src = open(os.path.join(HERE, "pc", "seaview.cpp"), encoding="utf-8").read()
    t = {}
    for m in re.finditer(r'\{ "([^"]+)", (\d), (\d+), [^}]*\}', src):
        t[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return t


def ini2args(path):
    t = par_table()
    vals, out = {}, []
    for line in open(path, encoding="utf-8"):
        if "=" in line:
            k, v = line.rstrip("\n").split("=", 1)
            vals[k] = float(v)
    el = az = None
    for k, v in vals.items():
        if k == "__sea": out.append(f"sea={int(v)}")
        elif k == "__sky": out.append(f"sky={int(v)}")
        elif k == "__tex": out.append(f"tex={int(v)}")
        elif k in t:
            kind, idx = t[k]
            if kind == 0: out.append(f"pr{idx}={v:g}")
            elif kind == 1: out.append(f"u{idx}={v:g}")
            elif idx == 0: el = v
            elif idx == 1: az = v
            else: out.append(f"u25={v:g}")
    if el is not None and az is not None:
        e, z = math.radians(el), math.radians(az)
        out += [f"u44={math.cos(e) * math.sin(z):g}", f"u45={math.sin(e):g}", f"u46={math.cos(e) * math.cos(z):g}"]
    return out


def cmd_ini2args(env, a):
    print(" ".join(ini2args(a.ini)))


def cmd_nx_send(env, a):
    ip = need(env, "SWITCH_IP")
    nro = os.path.join(NXDIR, "seaview-nx.nro")
    cmd_nx_build(env, a)   # always: the constants of the .ini are compiled into the shaders (use --live to skip and change variants on the pad)
    args = ini2args(a.ini) if a.ini else []
    if not a.live:
        args.append('baked=1')
    if a.rep:
        args.append(f"rep={a.rep}")
    log = os.path.join(DATA, "seaview-nx.log")
    print("sending to", ip, "(hbmenu, netloader: Y); pad: + quits")
    subprocess.run([sys.executable, os.path.join(REPO, "tools", "switch", "nxlink.py"), ip, nro, "switch/seaview/seaview-nx.nro", log] + args)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("dump"); p.add_argument("--close", action="store_true", help="close the emulator after the capture"); p.set_defaults(f=cmd_dump)
    p = sub.add_parser("pc"); p.add_argument("--hp", action="store_true"); p.set_defaults(f=cmd_pc)
    p = sub.add_parser("nx-build"); p.add_argument("--ini"); p.add_argument("--live", action="store_true"); p.set_defaults(f=cmd_nx_build)
    p = sub.add_parser("nx-send"); p.add_argument("--ini"); p.add_argument("--rep", type=int); p.add_argument("--live", action="store_true", help="the variants and values stay uniforms: change the variant on the pad (slower shaders)"); p.set_defaults(f=cmd_nx_send)
    p = sub.add_parser("ini2args"); p.add_argument("ini"); p.set_defaults(f=cmd_ini2args)
    a = ap.parse_args()
    a.f(load_env(), a)


if __name__ == "__main__":
    main()
