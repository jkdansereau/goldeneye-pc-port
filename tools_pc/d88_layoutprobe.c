/* D88.4: compiler-verified PC layout probe for propDef record types.
 *
 * Prints sizeof + pointer-member offsets for every PROPDEF_* record struct the
 * offline converter (d88_emit.py) must reproduce, compiled with the SAME
 * toolchain + include chain + flags as the PC game build. Machine-readable:
 *   SZ  <StructName> <bytes>
 *   PTR <StructName> <member> <offset>
 * Run:
 *   export PATH="/c/msys64/mingw64/bin:$PATH"
 *   x86_64-w64-mingw32-gcc -std=gnu11 -DPORT=1 -DPLATFORM_64BIT=1 \
 *     -D_LANGUAGE_C=1 -DVERSION_US -DLANG_US -DREFRESH_NTSC \
 *     -Iport/shim -I. -Iinclude -Iinclude/PR -Isrc -Isrc/game -Isrc/libultra \
 *     -Isrc/libultra/audio -Iport/include -Ibuild-pc/port/include \
 *     -include versioninfo.h \
 *     tools_pc/d88_layoutprobe.c -o build-pc/d88_layoutprobe.exe
 */
#include <stdio.h>
#include <ultra64.h>
#include <bondtypes.h>

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define SZ(t)       printf("SZ  %-26s %lu\n", #t, (unsigned long)sizeof(t))
#define PTR(t, f)   printf("PTR %-26s %-18s %lu\n", #t, #f, (unsigned long)offsetof(t, f))

int main(void)
{
    SZ(PropDefHeaderRecord);

    SZ(ObjectRecord);
    PTR(ObjectRecord, prop);
    PTR(ObjectRecord, model);
    PTR(ObjectRecord, ptr_allocated_collisiondata_block);
    PTR(ObjectRecord, projectile);
    PTR(ObjectRecord, mtx);
    PTR(ObjectRecord, maxdamage);
    PTR(ObjectRecord, shadecol);

    SZ(DoorRecord);
    PTR(DoorRecord, prop);
    PTR(DoorRecord, model);
    PTR(DoorRecord, ptr_allocated_collisiondata_block);
    PTR(DoorRecord, projectile);
    PTR(DoorRecord, linkedDoorOffset);
    PTR(DoorRecord, linkedDoor);
    PTR(DoorRecord, unkcc);
    PTR(DoorRecord, bbox);
    PTR(DoorRecord, openedTime);
    PTR(DoorRecord, openSoundState);
    PTR(DoorRecord, closeSoundState);

    SZ(GuardRecord);
    PTR(GuardRecord, Data);

    SZ(GlobalDoorScaleRecord);
    SZ(KeyRecord);
    PTR(KeyRecord, keyflags);
    SZ(TintedGlassRecord);

    SZ(TagObjectRecord);
    PTR(TagObjectRecord, ID);
    PTR(TagObjectRecord, NextTag);
    PTR(TagObjectRecord, TaggedObject);

    SZ(CCTVRecord);
    PTR(CCTVRecord, unk84);
    PTR(CCTVRecord, unkC4);
    PTR(CCTVRecord, unkF8);

    SZ(MonitorRecord);
    SZ(MonitorObjRecord);
    PTR(MonitorObjRecord, Monitor);
    PTR(MonitorObjRecord, OwnerOffset);
    PTR(MonitorObjRecord, ImageNum);

    SZ(MultiMonitorObjRecord);
    PTR(MultiMonitorObjRecord, Monitor);
    PTR(MultiMonitorObjRecord, ImageNums);

    SZ(WeaponObjRecord);
    PTR(WeaponObjRecord, weaponnum);
    PTR(WeaponObjRecord, dualweapon);

    SZ(AmmoCrateRecord);
    PTR(AmmoCrateRecord, ammoType);

    SZ(MultiAmmoCrateRecord);
    PTR(MultiAmmoCrateRecord, slots);

    SZ(BodyArmourRecord);
    PTR(BodyArmourRecord, initialamount);

    SZ(HatRecord);
    SZ(AutogunRecord);

    SZ(GuardAttributeRecord);
    PTR(GuardAttributeRecord, chrnum);

    SZ(WatchMenuObjectiveTextRecord);
    SZ(MissionObjectiveRecord);
    PTR(MissionObjectiveRecord, ObjRefID);
    PTR(MissionObjectiveRecord, nextentry);
    SZ(DestroyObjectRecord);
    SZ(CompleteConditionRecord);
    SZ(FailConditionRecord);
    SZ(CollectObjectRecord);
    SZ(DepositObjectRecord);
    SZ(DepositObjectInRoomRecord);
    SZ(NULLObjectRecord);
    SZ(CoopyObjectRecord);
    SZ(GasReleasingRecord);
    SZ(EnterRoomRecord);
    PTR(EnterRoomRecord, unk8);

    SZ(PhotographObjectRecord);
    PTR(PhotographObjectRecord, unk8);
    PTR(PhotographObjectRecord, lastprop);

    SZ(RenameObjectRecord);
    PTR(RenameObjectRecord, TagID);
    PTR(RenameObjectRecord, renobj);

    SZ(LockDoorRecord);
    PTR(LockDoorRecord, next);

    SZ(CutsceneRecord);

    return 0;
}
