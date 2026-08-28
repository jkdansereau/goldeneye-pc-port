/* D88.4: compiler-verified PC layout probe for propDef record types.
 *
 * Prints sizeof (and key offsets) for every PROPDEF_* record struct the
 * offline converter (d88_emit.py) must reproduce byte-for-byte, compiled with
 * the SAME toolchain + include chain + flags as the PC game build. Run:
 *
 *   export PATH="/c/msys64/mingw64/bin:$PATH"
 *   x86_64-w64-mingw32-gcc -std=gnu11 -DPORT=1 -DPLATFORM_64BIT=1 \
 *     -D_LANGUAGE_C=1 -DVERSION_US -DLANG_US -DREFRESH_NTSC \
 *     -Iport/shim -I. -Iinclude -Iinclude/PR -Isrc -Isrc/game -Isrc/libultra \
 *     -Isrc/libultra/audio -Iport/include -Ibuild-pc/port/include \
 *     -include versioninfo.h \
 *     tools_pc/d88_layoutprobe.c -o build-pc/d88_layoutprobe.exe
 *   ./build-pc/d88_layoutprobe.exe
 */
#include <stdio.h>
#include <ultra64.h>
#include <bondtypes.h>

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define P_SZ(t)      printf("SZ  %-34s %3lu bytes  %2lu words\n", #t, \
                            (unsigned long)sizeof(t), (unsigned long)sizeof(t) / 4)
#define P_OFF(t, f)  printf("OFF %-28s %-16s %3lu\n", #t, #f, \
                            (unsigned long)offsetof(t, f))

int main(void)
{
    /* --- the base header every record embeds --- */
    P_SZ(PropDefHeaderRecord);
    P_OFF(PropDefHeaderRecord, extrascale);
    P_OFF(PropDefHeaderRecord, state);
    P_OFF(PropDefHeaderRecord, type);

    /* --- ObjectRecord family (PROP/GLASS/ALARM/RACK/HAT/SAFE/GAS/KEY) --- */
    P_SZ(ObjectRecord);
    P_OFF(ObjectRecord, obj);
    P_OFF(ObjectRecord, pad);
    P_OFF(ObjectRecord, flags);
    P_OFF(ObjectRecord, flags2);
    P_OFF(ObjectRecord, prop);
    P_OFF(ObjectRecord, model);
    P_OFF(ObjectRecord, mtx);
    P_OFF(ObjectRecord, runtime_pos);
    P_OFF(ObjectRecord, runtime_bitflags);
    P_OFF(ObjectRecord, ptr_allocated_collisiondata_block);
    P_OFF(ObjectRecord, projectile);
    P_OFF(ObjectRecord, maxdamage);
    P_OFF(ObjectRecord, damage);
    P_OFF(ObjectRecord, shadecol);
    P_OFF(ObjectRecord, nextcol);

    P_SZ(DoorRecord);
    P_OFF(DoorRecord, linkedDoorOffset);
    P_OFF(DoorRecord, maxFrac);
    P_OFF(DoorRecord, doorFlags);
    P_OFF(DoorRecord, doorType);
    P_OFF(DoorRecord, keyflags);
    P_OFF(DoorRecord, autoCloseFrames);
    P_OFF(DoorRecord, doorOpenSound);
    P_OFF(DoorRecord, frac);
    P_OFF(DoorRecord, openstate);
    P_OFF(DoorRecord, calculatedopacity);
    P_OFF(DoorRecord, TintDist);
    P_OFF(DoorRecord, CullDist);
    P_OFF(DoorRecord, soundType);
    P_OFF(DoorRecord, linkedDoor);
    P_OFF(DoorRecord, unkcc);
    P_OFF(DoorRecord, bbox);
    P_OFF(DoorRecord, openedTime);
    P_OFF(DoorRecord, portalNumber);
    P_OFF(DoorRecord, openSoundState);
    P_OFF(DoorRecord, closeSoundState);

    P_SZ(GuardRecord);
    P_OFF(GuardRecord, chrnum);
    P_OFF(GuardRecord, PadID);
    P_OFF(GuardRecord, bitflags);
    P_OFF(GuardRecord, HeadID);
    P_OFF(GuardRecord, Data);

    P_SZ(GlobalDoorScaleRecord);
    P_SZ(KeyRecord);
    P_OFF(KeyRecord, keyflags);
    P_SZ(TintedGlassRecord);
    P_SZ(TagObjectRecord);
    P_OFF(TagObjectRecord, NextTag);
    P_OFF(TagObjectRecord, TaggedObject);

    /* --- records whose sizepropdef() arm is a hardcoded literal --- */
    /* Wrapped so the probe still builds if a type is missing; each line that
     * fails to compile tells us that type has no PC struct yet. */
#define TRY_SZ(t) P_SZ(t)
    TRY_SZ(CutsceneRecord);
    TRY_SZ(RenameObjectRecord);
    TRY_SZ(LockDoorRecord);
    TRY_SZ(SafeObjectRecord);
    TRY_SZ(PhotographObjectRecord);
    TRY_SZ(DestroyObjectRecord);
    TRY_SZ(CollectObjectRecord);
    TRY_SZ(DepositObjectRecord);
    TRY_SZ(NULLObjectRecord);
    TRY_SZ(CoopyObjectRecord);

    return 0;
}
