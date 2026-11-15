# Testing on the console over nxlink, with the log coming back by itself

Goal: send a test NRO to the console over the network, let it start, and get its log and crash dump
back on the PC, without copying files off the SD card or pasting reports by hand.

## How nxlink works (read from the sources)

Sources: `switch-tools/src/nxlink.c`, `nx-hbmenu/common/netloader.c`, `libnx/nx/source/runtime/nxlink.c`.

1. The PC connects over TCP to port **28280** on the console (sphaira's or hbmenu's netloader) and
   sends: name length + file name, size, the NRO compressed (zlib, 16 KiB chunks), then the argument
   block (NUL-terminated strings, preceded by their total length). The netloader starts the NRO at once.
2. With `-s`, the PC then listens on port **28771** (no timeout): **the Switch connects to the PC**,
   not the other way round. Whatever the program writes to stdout/stderr arrives there.
3. The console learns the PC's address from the **last `argv` argument**, of the form
   `<PC IPv4 as 8 hex digits>_NXLINK_` (for example `0a00a8c0_NXLINK_` for 192.168.0.10: the address
   bytes read as a little-endian integer). libnx's `nxlinkSetup()` removes it from `argv` and stores it
   in `__nxlink_host`. The official client does not add this marker: the console-side netloader does,
   with the sender's address. Our client adds it itself, just in case (a duplicate is harmless: only the
   last one is read).
4. In the program, **sockets come first**: `socketInitializeDefault()`, then `nxlinkStdio()`. Without
   sockets, `nxlinkStdio()` fails silently and nothing arrives. `nxlinkStdio()` is an `inline` function
   (it calls `nxlinkConnectToHost(true, true)`): it cannot be declared `extern` (link error), go through
   `<switch.h>`.

sphaira opens an nxlink host by itself once the option is on, like hbmenu's netloader: nothing has to
be started by hand on the console before each upload. When the console goes to sleep, uploads get no
answer.

## The test build

`cmake -S . -B build-switch-link -G Ninja -DROMID=ntsc-final -DNX_LINKSTDIO=ON -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake`
(a separate build directory, like `build-switch-args`; the option is off by default and **this NRO is
never shipped**). With `NX_LINKSTDIO`:

- `main()` calls `nxLinkStdio()` (`port/src/switch/nxmem.c`) first: sockets, nxlink stream, and the
  game log (which also goes to stderr) arrives on the PC;
- the crash handler (`nxcrash.c`) also writes the dump (pc, lr, far, registers, backtrace with offsets
  into the NRO) to the stream, besides `ge007-crash.log`;
- a thread asks to quit after 10 s, like "Quit to desktop" (`videoRequestQuit`), so launches follow one
  another with no one at the controller. The game returns to the launcher (see "Clean exit" below) and
  the stream closes;
- before returning, the memory blocks still in a special state are listed on the stream
  (`nxDumpMemState`), then the sockets are closed.

Resolving an offset from the crash dump: `aarch64-none-elf-addr2line -f -C -i -e build-switch-link/ge007.aarch64.elf 0x<nro+offset>`
(in the `compose.yaml` container, `--entrypoint sh`).

## Arguments, settings and run length for a test run

Configure the test build with `-DNX_ARGS=ON` as well as `-DNX_LINKSTDIO=ON`; then everything a run needs
travels on the nxlink command line, with nothing to copy to the SD card:

- `-level_XX`, `-hardN` and the other switches are passed as program arguments, as on the PC.
- `NAME=value` arguments set environment variables before the game starts, so probes and test switches
  work: `GE_PERFSTAT=1`, `GE_COOP=1`, `GE_D176=1`, ... (`data/ge007.args` still works and is read too).
- `NX_TEST_SECS=<n>` sets how long the run lasts before the automatic quit (10 s by default). The watchdog
  reads it 2 s after start, once `main()` has set the environment.
- `GE_CFG="Key=value;Key2=value2"` overrides `ge007.ini` values for this run only (for example
  `GE_CFG=Video.DrawDistance=150;Video.FpsCap=30`). While it is set the ini is never written back, so a
  test never changes the user's settings.

Example, a 40 s performance run of the Jungle in co-op with a 150% draw distance:

```sh
NXLINK_RETRIES=40 python tools/switch/nxlink.py <ip> build-switch-link/ge007.aarch64.nro \
    switch/project-dam/project-dam-test.nro run.log \
    -level_37 -hard0 GE_COOP=1 GE_PERFSTAT=1 NX_TEST_SECS=40 "GE_CFG=Video.DrawDistance=150;Video.LodDistance=150"
```

If the console is asleep the upload gets no answer: the client gives up after `NXLINK_RETRIES` attempts.

## The PC client

`python tools/switch/nxlink.py <console ip> <file.nro> <path on the SD> <log out> [args...]`

It connects (retrying every 3 s while the netloader does not listen; `NXLINK_RETRIES` attempts, 100 by
default), sends the NRO, opens port 28771 **before** the upload, writes the stream to the log file and
stops when the game quits.

Pitfalls:
- one client at a time (the netloader takes a single connection); do not probe the port;
- the `nxlink` inside the Docker container cannot reach the local network (Docker Desktop NAT): use
  the Python client on Windows;
- the client waits only 120 s for the game to connect after the upload; past that, the log is lost;
- if nothing arrives: the game may have crashed before `main()`, or has no host address (marker
  missing) or no sockets.

## Clean exit and the test loop

Since D958 the game returns to the launcher (sphaira, hbmenu) instead of ending the whole application:
`portAtExit` stops the threads (`portThreadsStop`, the `g_portQuit` flag tested in
`osRecvMesg`/`osSendMesg`/`osYieldThread`), joins them (libnx keeps a thread's stack mapped until it is
joined; a stack still mapped made the loader abort with `0xd401` when it unloaded the executable),
closes audio, `SDL_Quit()`, releases the memory windows, then `exit()` carries on. If a thread has not
stopped after 4 s: the old exit (`nxExitProcess`). In the memory dump, the locked heap block the size of
the executable is the loader's buffer behind our code: normal, the loader frees it itself.

Since the launcher is still there after every launch, a test loop runs unattended. Test one change at a
time: one launch, then a second upload that only checks the launcher survived the return.

```sh
NXLINK_RETRIES=20 python tools/switch/nxlink.py <ip> build-switch-link/ge007.aarch64.nro \
    switch/project-dam/project-dam-test.nro run-1.log
NXLINK_RETRIES=10 python tools/switch/nxlink.py <ip> build-switch-link/ge007.aarch64.nro \
    switch/project-dam/project-dam-test.nro run-2.log   # connects only if the launcher is alive
```

`NXLINK_RETRIES` (3 s per attempt) makes an upload fail fast when the launcher no longer answers (it
crashed on the return). The logs give the memory slot chosen and the end of each launch.
