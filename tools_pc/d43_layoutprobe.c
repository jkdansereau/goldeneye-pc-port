/* D43 Plan B: compiler-verified PC layout probe for model-file records.
 *
 * Prints sizeof/offsetof for every struct the offline emit pass (d43_emit.py)
 * must reproduce byte-for-byte, compiled with the SAME toolchain + includes as
 * the game build (x86_64-w64-mingw32-gcc, port/shim first). Run:
 *   x86_64-w64-mingw32-gcc -DPORT -Iport/shim -I. -Iinclude -Iinclude/PR \
 *       -Isrc -Isrc/game -Isrc/libultra -Isrc/libultra/audio -Iport/include \
 *       tools_pc/d43_layoutprobe.c -o build-pc/d43_layoutprobe.exe
 */
#include <stdio.h>
#include <ultra64.h>
#include <bondtypes.h>

/* The decomp's include/stddef.h is an empty guard (no offsetof); use the GCC
 * builtin so this probe works in a C TU with the project include chain. */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define P_SZ(t)  printf("SZ  %-40s %lu\n", #t, (unsigned long)sizeof(t))
#define P_OFF(t, f) printf("OFF %-32s %-12s %lu\n", #t, #f, (unsigned long)offsetof(t, f))

int main(void)
{
    P_SZ(ModelNode);
    P_OFF(ModelNode, Opcode); P_OFF(ModelNode, Data); P_OFF(ModelNode, Parent);
    P_OFF(ModelNode, Next); P_OFF(ModelNode, Prev); P_OFF(ModelNode, Child);

    P_SZ(Vertex);
    P_OFF(Vertex, coord); P_OFF(Vertex, index); P_OFF(Vertex, s);
    P_OFF(Vertex, CollisionRelatedIndex); P_OFF(Vertex, CollisionReserved);

    P_SZ(ModelFileHeader);
    P_OFF(ModelFileHeader, RootNode); P_OFF(ModelFileHeader, Switches);
    P_OFF(ModelFileHeader, numSwitches); P_OFF(ModelFileHeader, numtextures);
    P_OFF(ModelFileHeader, Textures);

    P_SZ(ModelFileTextures);
    P_OFF(ModelFileTextures, TextureID); P_OFF(ModelFileTextures, Width);
    P_OFF(ModelFileTextures, RenderDepth); P_OFF(ModelFileTextures, sflags);

    P_SZ(ModelRoData_HeaderRecord);
    P_OFF(ModelRoData_HeaderRecord, AnimPart); P_OFF(ModelRoData_HeaderRecord, MatrixIndex);
    P_OFF(ModelRoData_HeaderRecord, FirstGroup); P_OFF(ModelRoData_HeaderRecord, Group1);
    P_OFF(ModelRoData_HeaderRecord, RwDataIndex);

    P_SZ(ModelRoData_GroupRecord);
    P_OFF(ModelRoData_GroupRecord, Origin); P_OFF(ModelRoData_GroupRecord, JointID);
    P_OFF(ModelRoData_GroupRecord, MatrixID0); P_OFF(ModelRoData_GroupRecord, ChildGroup);
    P_OFF(ModelRoData_GroupRecord, BoundingVolumeRadius);

    P_SZ(ModelRoData_DisplayListRecord);
    P_OFF(ModelRoData_DisplayListRecord, Primary); P_OFF(ModelRoData_DisplayListRecord, Secondary);
    P_OFF(ModelRoData_DisplayListRecord, BaseAddr); P_OFF(ModelRoData_DisplayListRecord, Vertices);
    P_OFF(ModelRoData_DisplayListRecord, numVertices); P_OFF(ModelRoData_DisplayListRecord, ModelType);

    P_SZ(ModelRoData_LODRecord);
    P_OFF(ModelRoData_LODRecord, MinDistance); P_OFF(ModelRoData_LODRecord, MaxDistance);
    P_OFF(ModelRoData_LODRecord, Affects); P_OFF(ModelRoData_LODRecord, RwDataIndex);

    P_SZ(ModelRoData_BSPRecord);
    P_OFF(ModelRoData_BSPRecord, Point); P_OFF(ModelRoData_BSPRecord, Vector);
    P_OFF(ModelRoData_BSPRecord, leftChild); P_OFF(ModelRoData_BSPRecord, rightChild);
    P_OFF(ModelRoData_BSPRecord, RwDataIndex);

    P_SZ(ModelRoData_BoundingBoxRecord);
    P_OFF(ModelRoData_BoundingBoxRecord, ModelNumber); P_OFF(ModelRoData_BoundingBoxRecord, Bounds);

    P_SZ(ModelRoData_GunfireRecord);
    P_OFF(ModelRoData_GunfireRecord, Offset); P_OFF(ModelRoData_GunfireRecord, Size);
    P_OFF(ModelRoData_GunfireRecord, Image); P_OFF(ModelRoData_GunfireRecord, Scale);
    P_OFF(ModelRoData_GunfireRecord, RwDataIndex);

    P_SZ(ModelRoData_ShadowRecord);
    P_OFF(ModelRoData_ShadowRecord, pos); P_OFF(ModelRoData_ShadowRecord, size);
    P_OFF(ModelRoData_ShadowRecord, image); P_OFF(ModelRoData_ShadowRecord, Header);
    P_OFF(ModelRoData_ShadowRecord, Scale);

    P_SZ(ModelRoData_InterlinkageRecord);
    P_OFF(ModelRoData_InterlinkageRecord, pos); P_OFF(ModelRoData_InterlinkageRecord, pos2);
    P_OFF(ModelRoData_InterlinkageRecord, Scale);

    P_SZ(ModelRoData_SwitchRecord);
    P_OFF(ModelRoData_SwitchRecord, Controls); P_OFF(ModelRoData_SwitchRecord, RwDataIndex);

    P_SZ(ModelRoData_GroupSimpleRecord);
    P_OFF(ModelRoData_GroupSimpleRecord, Origin); P_OFF(ModelRoData_GroupSimpleRecord, Group1);
    P_OFF(ModelRoData_GroupSimpleRecord, Group2); P_OFF(ModelRoData_GroupSimpleRecord, BoundingVolumeRadius);

    P_SZ(ModelRoData_DisplayListPrimaryRecord);
    P_OFF(ModelRoData_DisplayListPrimaryRecord, numVertices);
    P_OFF(ModelRoData_DisplayListPrimaryRecord, Vertices);
    P_OFF(ModelRoData_DisplayListPrimaryRecord, Primary);
    P_OFF(ModelRoData_DisplayListPrimaryRecord, BaseAddr);

    P_SZ(ModelRoData_HeadPlaceholderRecord);
    P_OFF(ModelRoData_HeadPlaceholderRecord, RwDataIndex);

    P_SZ(ModelRoData_DisplayList_CollisionRecord);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, Primary);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, Secondary);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, Vertices);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, numVertices);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, numCollisionVertices);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, CollisionVertices);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, PointUsage);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, ModelType);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, RwDataIndex);
    P_OFF(ModelRoData_DisplayList_CollisionRecord, BaseAddr);

    return 0;
}
