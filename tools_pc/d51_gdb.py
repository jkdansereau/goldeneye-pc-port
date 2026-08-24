# D51 live capture: log every import_texture_i8 hit; on SIGSEGV dump the
# RDP tile state, segment table, and the main DL around the faulting G_TEXRECT.
import gdb
import struct

def log_hit(frame):
    try:
        tile = int(frame.read_var("tile"))
        lt = frame.read_var("loaded_texture")
        addr = lt["addr"]
        size = int(lt["size_bytes"])
        line = int(lt["line_size_bytes"])
        fline = int(lt["full_image_line_size_bytes"])
        t2l = gdb.parse_and_eval("rdp.texture_to_load.addr")
        print("I8HIT tile=%d addr=%s size=%u line=%u full_line=%u to_load=%s" % (
            tile, addr, size, line, fline, t2l))
    except Exception as e:
        print("I8HIT log error: %s" % e)

def dump_crash():
    print("=== CRASH STATE ===")
    frame = gdb.selected_frame()
    frames = []
    while frame is not None:
        frames.append(frame)
        frame = frame.older()
    for i, f in enumerate(frames[:12]):
        fn = f.symtab.filename if f.symtab else "?"
        print("FRAME %d: %s at %s:%d" % (i, f.name(), fn, f.line()))

    try:
        f0 = frames[0]
        lt = f0.read_var("loaded_texture")
        print("f0: tile=%d size_bytes=%u line=%u full_line=%u addr=%s" % (
            int(f0.read_var("tile")), int(lt["size_bytes"]),
            int(lt["line_size_bytes"]), int(lt["full_image_line_size_bytes"]), lt["addr"]))
    except Exception as e:
        print("f0 vars: %s" % e)

    try:
        tt = gdb.parse_and_eval("rdp.texture_tile[0]")
        print("tile0: fmt=%d siz=%d line_size=%u tmem=%d palette=%d uls=%d ult=%d lrs=%d lrt=%d w=%d h=%d" % (
            int(tt["fmt"]), int(tt["siz"]), int(tt["line_size_bytes"]), int(tt["tmem"]),
            int(tt["palette"]), int(tt["uls"]), int(tt["ult"]), int(tt["lrs"]), int(tt["lrt"]),
            int(tt["width"]), int(tt["height"])))
        t2l = gdb.parse_and_eval("rdp.texture_to_load")
        print("to_load: addr=%s siz=%d width=%u" % (t2l["addr"], int(t2l["siz"]), int(t2l["width"])))
    except Exception as e:
        print("rdp state: %s" % e)

    # TEMP D51: font char tables
    for name in ("ptrFontBankGothicChars", "ptrFontZurichBoldChars"):
        try:
            ch = gdb.parse_and_eval(name)
            bad = 0
            for i in range(94):
                v = int(ch[i]["pixeldata"])
                if v >= 0x100000000:
                    print("%s[%d].pixeldata=0x%x (HIGH BYTES SET)" % (name, i, v))
                    bad += 1
            print("%s: %d/94 entries with high bytes set" % (name, bad))
        except Exception as e:
            print(name + ": %s" % e)
    try:
        sp = gdb.parse_and_eval("segmentPointers")
        for i in range(16):
            v = int(sp[i])
            if v:
                print("seg[%d]=0x%x" % (i, v))
    except Exception as e:
        print("segs: %s" % e)

    inf = gdb.selected_inferior()
    for i, f in enumerate(frames):
        if f.name() == "gfx_run_dl":
            try:
                cmd = int(f.read_var("cmd"))
                dls = int(f.read_var("dListStart"))
                print("gfx_run_dl frame=%d cmd=0x%x dListStart=0x%x" % (i, cmd, dls))
                # 8 slots around cmd
                base = cmd - 64
                b = bytes(inf.read_memory(base, 128))
                ws = struct.unpack("<32I", b)
                for j in range(0, 32, 2):
                    w0, w1 = ws[j], ws[j + 1]
                    op = (w0 >> 24) & 0xff
                    tag = ""
                    if op == 0xe4: tag = " <-- G_TEXRECT"
                    elif op == 0xe5: tag = " <-- G_TEXRECTFLIP"
                    elif op == 0xfd: tag = " [SETTIMG]"
                    elif op == 0xf5: tag = " [SETTILE]"
                    elif op == 0xf2: tag = " [SETTILESIZE]"
                    elif op == 0xf3: tag = " [LOADBLOCK]"
                    elif op == 0xf4: tag = " [LOADSYNC]"
                    elif op == 0xe7: tag = " [PIPESYNC]"
                    elif op == 0xbc: tag = " [MOVEWORD]"
                    elif op == 0x06: tag = " [DL]"
                    elif op == 0xb8: tag = " [ENDDL]"
                    mark = " *" if (base + j * 4) == cmd else ""
                    print("  dl[0x%x] w0=0x%08x w1=0x%08x%s%s" % (base + j * 4, w0, w1, tag, mark))
                # walk the main DL from its start; log SETTIMGs and find the G_TEXRECT
                n = 0
                off = 0
                while n < 600:
                    b2 = bytes(inf.read_memory(dls + off, 8))
                    w0, w1 = struct.unpack("<II", b2)
                    op = (w0 >> 24) & 0xff
                    if op == 0xe4 or op == 0xe5:
                        print("MAIN-DL G_TEXRECT at dls+0x%x (slot %d) w0=0x%08x w1=0x%08x" % (off, n, w0, w1))
                        # dump the 10 slots before it
                        for k in range(max(0, n - 10), n):
                            o2 = k * 16
                            a, b3 = struct.unpack("<II", bytes(inf.read_memory(dls + o2, 8)))
                            print("   pre[%d] +0x%x w0=0x%08x w1=0x%08x" % (k, o2, a, b3))
                        break
                    if op == 0xb8:
                        print("MAIN-DL ENDDL at dls+0x%x (slot %d), no G_TEXRECT seen" % (off, n))
                        break
                    n += 1
                    off += 16
            except Exception as e:
                print("gfx_run_dl dump: %s" % e)
            break

def on_stop(signal_name, stop_reason):
    if stop_reason == "breakpoint-hit":
        f = gdb.selected_frame()
        if f.name() == "import_texture_i8":
            log_hit(f)
            gdb.execute("continue")
    elif signal_name == "SIGSEGV":
        dump_crash()

gdb.Breakpoint("import_texture_i8")
gdb.events.stop.connect(on_stop)
print("D51 capture armed")
