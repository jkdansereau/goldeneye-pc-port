# gdb command file — dump animation state for the Facility outro hang.
# Usage:  tools_pc/attach_animgen.sh   (game must be running, NOT under another gdb)
set pagination off
python
import gdb

TARGET_FN = "modelSetAnimFrame2WithChrStuff"
found = False
for t in gdb.selected_inferior().threads():
    fr = t.frame
    depth = 0
    while fr is not None and depth < 40:
        if fr.name() == TARGET_FN:
            found = True
            print("== thread %d at %s" % (t.num, TARGET_FN))
            for v in ["framea", "frameb", "curframe", "endframe", "forward",
                      "framenum", "flag", "jointnum", "scale"]:
                try:
                    print("  %s = %s" % (v, fr.read_var(v)))
                except Exception as e:
                    print("  %s = <err %s>" % (v, e))
            for e in [
                "modelptr->animframe1", "modelptr->animframe2", "modelptr->animframe",
                "modelptr->speed", "modelptr->playspeed", "modelptr->newspeed",
                "modelptr->oldspeed", "modelptr->timespeed", "modelptr->elapsespeed",
                "modelptr->unk84", "modelptr->unk88", "modelptr->endframe",
                "modelptr->scale", "modelptr->anim_translation_scale",
                "modelptr->anim", "(int)(modelptr->anim->unk04)",
                "(int)(modelptr->anim->unk07)", "modelptr->anim2",
                "header->unk00", "header->unk01", "header->unk18",
            ]:
                try:
                    print("  %s = %s" % (e, fr.read_var(e)))
                except Exception as ex:
                    print("  %s = <err %s>" % (e, ex))
            cf = fr.older()
            if cf is not None:
                print("== caller: %s" % cf.name())
                for v in ["frame", "frame2", "numticks", "update_chrstuff"]:
                    try:
                        print("  %s = %s" % (v, cf.read_var(v)))
                    except Exception as e2:
                        print("  %s = <err %s>" % (v, e2))
            break
        fr = fr.older()
        depth += 1
if not found:
    print("modelSetAnimFrame2WithChrStuff not on any stack — dumping all bt's instead")
    for t in gdb.selected_inferior().threads():
        print("== thread %d" % t.num)
        fr = t.frame
        depth = 0
        while fr is not None and depth < 12:
            print("   #%d %s" % (depth, fr.name()))
            fr = fr.older()
            depth += 1
print("dump done")
end
