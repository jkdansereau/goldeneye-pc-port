#include <ultra64.h>
#include "include/limits.h"
#include <bondconstants.h>
#include <bondtypes.h>
#include <bondgame.h>
#include <music.h>
#include <snd.h>
#include "bondview.h"
#include "bondinv.h"
#include "gun.h"
#include "chrobjdata.h"
#include "game/propobj.h"
#include "game/objective_status.h"
#include "quaternion.h"
#include "image_bank.h"
#include "bondwalk2.h"
#include "othermodemicrocode.h"
#include "player.h"
#include "lv.h"
#include "random.h"
#include "math_asinfacosf.h"
#include "loadobjectmodel.h"
#include "objecthandler.h"
#include "image.h"
#include "tex.h"
#include "debugmenu_handler.h"
#include "fr.h"
#include "assets/obseg/text/LgunE.h"
#include "textrelated.h"
#include "chrai.h"
#include "model.h"
#include "options.h"
#include "mpmenu.h"
#include "joy.h"
#include "matrixmath.h"
#include "bondinv.h"


// bss
s32 dword_CODE_bss_80075DB0;
s32 dword_CODE_bss_80075DB4;
ALSoundState* dword_CODE_bss_80075DB8[4];

CasingRecord g_Casings[20];
s32 dword_CODE_bss_80076A48;


#ifdef REFRESH_PAL
    /* PAL */
    #define THROWN_ITEM_REFRESH_RATE                   50
    #define THROWN_ITEM_TIMER_SOLO                     250
    #define THROWN_ITEM_TIMER_MULTI                    150
    #define THROWN_ITEM_TIMER_DEFAULT                  200
    #define DUAL_WIELD_TRIGGER_SWAP_TICKS 24
    #define DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS 36
    #define WATCH_SOUND_DURATION_TICKS 250
#else
    /* NTSC */
    #define THROWN_ITEM_REFRESH_RATE                   60
    #define THROWN_ITEM_TIMER_SOLO                     300
    #define THROWN_ITEM_TIMER_MULTI                    180
    #define THROWN_ITEM_TIMER_DEFAULT                  240
    #define DUAL_WIELD_TRIGGER_SWAP_TICKS 20
    #define DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS 30
    #define WATCH_SOUND_DURATION_TICKS 300
#endif




// data
////D:80032440
//rgba_u8 D_80032440[] = {
//	{0x96, 0x96, 0x96, 0},
//	{0x96, 0x96, 0x96, 0}
//};
//
////D:80032448
//rgba_u8 D_80032448[] = {
//	{0xFF, 0xFF, 0xFF, 0},
//	{0xFF, 0xFF, 0xFF, 0},
//	{0xB2, 0x4D, 0x2E, 0}
//};
/**
 * Controls the lighting on environment mapped weapons such as the Cougar Magnum and Golden Gun.
 */
Lights1 g_WeaponEnvmapLight = gdSPDefLights1(
    0x96, 0x96, 0x96,   // ambient RGB
    0xff, 0xff, 0xff,   // diffuse RGB
    0xb2, 0x4d, 0x2e);  // direction
//D:80032454
//u32 D_80032454 = 0;

//D:80032458
u32 D_80032458 = 0;

//D:8003245C
u32 size_item_buffer[] = {0x14820, 0x14820};

//D:80032464
u32 D_80032464[] ={0x7530, 0x7530};



//D:8003246C
CartridgeModelFileRecord ejected_cartridge[] = {
	{&cartridge_header, "GcartridgeZ"},
	{&cartrifle_header, "GcartrifleZ"},
	{&cartblue_header, "GcartblueZ"},
	{&cartshell_header, "GcartshellZ"},
	{0, ""}
};

#include <assets/obseg/gun/gunWeaponStats.inc.c>

//D:80033924
#include <assets/obseg/gun/gunModelFileRecord.inc.c>

//D:80034C9C
u32 cartridges_eject = 0;
//D:80034CA0
u32 D_80034CA0 = 0;

//D:80034CA4
u32 D_80034CA4[] = {
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41000000,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	0x3F000000,    0x41000000,           0x0,    0x40C00000,
	0xBFC00000,           0x0,    0x40B487B1,    0x3E70C0AD,
	0x3E0AE536,    0x3F000000,    0x41000000,           0x0,
	0x41480000,    0xC0600000,           0x0,    0x40C159EC,
	0x3D374BC7,    0x3F0E4378,    0x3F000000,    0x41000000,
	       0x0,    0xC1200000,    0xC1300000,           0x0,
	0x3F9ED962,    0x3EA24C40,    0x3F8B0DF1,    0x3F000000,
	0x41000000,           0x0,    0xC1600000,    0xC1700000,
	       0x0,    0x3FEA4780,    0x40C498E3,    0x3FA316D3,
	0x3F000000,    0x41200000,           0x0,    0xBF800000,
	0xC1100000,           0x0,    0x3EC4BBA1,    0x3EB87C42,
	0x3DD75968,    0x3F000000,    0x41200000,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,    0x3F000000,    0x41A00000,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41A00000,           0x1,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0,           0
};

u32 D_80034E0C[] = {
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41000000,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	0x3F000000,    0x41000000,           0x0,    0xC1080000,
	0xC0C00000,           0x0,    0x40AF7506,    0x40BAB4B9,
	0x40C2A5C2,    0x3F000000,    0x41000000,           0x0,
	0xC0400000,    0xC0600000,           0x0,    0x3ECE08F2,
	0x40B75721,    0x40B62409,    0x3F000000,    0x41000000,
	       0x0,    0xBF000000,    0xC1080000,           0x0,
	0x3F9DFD7A,    0x40B768CD,    0x40B37BDF,    0x3F000000,
	0x41000000,           0x0,    0x40E00000,    0xC1E40000,
	0xBFC00000,    0x3FA74949,    0x40B63EBC,    0x40B6443D,
	0x3F000000,    0x41200000,           0x0,    0xBFC00000,
	0xC1100000,           0x0,    0x3D8ADEEC,    0x40C84E72,
	0x3E506749,    0x3F000000,    0x41200000,           0x0,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,    0x3F000000,    0x41A00000,
	       0x0,           0x0,           0x0,           0x0,
	       0x0,           0x0,           0x0,    0x3F000000,
	0x41A00000,           0x1,           0x0,           0x0,
	       0x0,           0x0,           0x0,           0x0,
           0x0,           0x0
};

/**
 * Throwing Knife animation for when Z is pressed/held down.
 */
struct Weapon1PTransformKeyframe throwKnifeDrawBackKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Throwing Knife animation for when Z is released.
 */
struct Weapon1PTransformKeyframe throwKnifeReleaseKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Grenade which is strange since you cannot see it on screen. Perhaps the developers once intended to have a proper first person Grenade throwing animation?
 */
struct Weapon1PTransformKeyframe grenadeThrowKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 10.0f, 12.5f, 17.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Timed Mine, but changing the durations has no effect.
 */
struct Weapon1PTransformKeyframe timedMineThrowKeyframes[6] = {
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 34.5f, 25.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 10.0f, 12.5f, 17.5f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Proximity Mine. Changing the durations does effect the time it takes to throw the mine, although nothing is seen on screen.
 */
struct Weapon1PTransformKeyframe proxMineThrowKeyframes[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 4.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Keyframes for the Remote Mine. Changing the durations does not effect the time it takes to throw them,
 * but it does change the time it takes before you can throw another.
 */
struct Weapon1PTransformKeyframe remoteMineThrowKeyframes[7] = {
    { 0, { 0.0f, 0.0f, 4.5f}, { 5.576369f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 20.5f}, { 5.26209f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 3.0f, 5.5f}, { 0.031375f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, -20.0f, 18.0f}, { 0.785458f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Slapper attack when the hand starts at the right of the screen then chops downward and to the left.
 */
Weapon1PTransformKeyframe fistMeleeKeyframes1[10] = {
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 10.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 10.0f },
    { 0, {   6.0f, 23.0f, 0.0f }, {  5.91572f, 0.085832f, 0.219482f }, 0.5f, 10.0f },
    { 0, {  18.0f, 35.0f, 9.5f }, { 4.998193f, 0.084203f, 0.268954f }, 0.5f, 10.0f },
    { 0, { -20.0f, 25.5f, 4.0f }, { 0.126148f, 0.304284f, 0.548047f }, 0.5f, 10.0 },
    { 0, { -28.0f, -4.0f, 2.0f }, { 0.506821f,  0.51473f, 0.484098f }, 0.5f,  1.0f },
    { 0, { -28.0f, -4.0f, 2.0f }, { 0.506821f,  0.51473f, 0.484098f }, 0.5f,  1.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 20.0f },
    { 0, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.5f, 20.0f },
    { 1, {   0.0f,  0.0f, 0.0f }, {      0.0f,      0.0f,      0.0f }, 0.0f,  0.0f }
};

/**
 * Slapper attack when the hand moves to the left of the screen then chops downward and to the right.
 */
Weapon1PTransformKeyframe fistMeleeKeyframes2[10] = {
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 10.0f},
       { 0, { -6.0f, 23.0f, 0.0f}, { 5.08683f, 6.131295f, 5.534376f}, 0.5f, 10.0f},
       { 0, { -18.0f, 35.0f, 9.5f}, { 4.880698f, 0.070396f, 5.53615f}, 0.5f, 10.0f},
       { 0, { 8.0f, 25.5f, 4.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 10.0f},
       { 0, { 28.0f, -4.0f, 2.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 1.0f},
       { 0, { 28.0f, -4.0f, 2.0f}, { 0.107213f, 6.062361f, 5.404225f}, 0.5f, 1.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
       { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
       { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Sniper swing right to left.
 */
Weapon1PTransformKeyframe sniperMeleeKeyframes1[11] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 9.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, {9.5f, -0.5f, 3.5f}, { 0.291053f, 5.584375f, 6.212358f}, 0.5f, 8.0f},
    { 0, {18.0f, 7.5f, 3.5f}, { 0.439372f, 5.945201f, 5.993666f}, 0.5f, 8.0f},
    { 0, {-9.0f, 8.5f, 5.5f}, { 0.704803f, 0.194459f, 6.168447f}, 0.5f, 7.0f},
    { 0, {-29.0f, -5.5f, 5.5f}, { 2.281831f, 1.106353f, 1.489998f}, 0.5f, 7.0f},
    { 0, {-57.5f, -27.5f, 5.5f}, { 2.281831f, 1.106353f, 1.489998f}, 0.5f, 7.0f},
    { 0, {-19.5f, -20.0f, 5.5f}, { 1.22519f, 0.726087f, 1.210713f}, 0.5f, 15.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 20.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Sniper swing left to right.
 */
Weapon1PTransformKeyframe sniperMeleeKeyframes2[11] = {
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f,  9.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f,  8.0f },
    { 0, { -15.5f,  0.5f, 15.0f }, {  0.9344959855079651f,  0.6256099939346313f,  0.2237969934940338f }, 0.5f,  8.0f },
    { 0, { -23.0f,  2.0f, 12.0f }, {  1.8016400337219238f,  0.9494050145149231f,  0.6307389736175537f }, 0.5f,  8.0f },
    { 0, { -18.0f, -0.5f,  4.0f }, {  0.8478249907493591f,  0.9247649908065796f, 0.07744300365447998f }, 0.5f,  7.0f },
    { 0, {  10.5f,  5.0f,  2.5f }, { 0.22940599918365479f, 0.24570399522781372f, 0.09906300157308578f }, 0.5f,  7.0f },
    { 0, {  18.0f,  5.0f,  2.5f }, { 0.03281300142407417f,    6.20933723449707f,  0.1350640058517456f }, 0.5f,  7.0f },
    { 0, {   9.5f,  3.5f, -1.5f }, {   6.273238182067871f,   6.005795001983643f, 0.08971499651670456f }, 0.5f,  7.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f, 20.0f },
    { 0, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.5f, 20.0f },
    { 1, {   0.0f,  0.0f,  0.0f }, {                 0.0f,                 0.0f,                 0.0f }, 0.0f,  0.0f },
};

/**
 * Animation when the Taser is lowering to fire position.
 */
Weapon1PTransformKeyframe taserFireKeyFrames[6] = {
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    { 0, { 0.5f, -6.0f, -8.0f}, { 0.439468f, 0.278829f, 0.195178f}, 0.5f, 8.0f},
    { 0, { -2.0f, -8.0f, -10.0f}, { 1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    { 0, { -2.0f, -8.0f, -10.0f}, { 1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    { 1, { 0.0f, 0.0f, 0.0f}, { 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};

/**
 * Animation once the Taser has fired and goes back to idle position.
 */
Weapon1PTransformKeyframe taserRaiseKeyframes[6] = {
    {0, { -2.0f, -8.0f, -10.0f}, {1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    {0, { -2.0f, -8.0f, -10.0f}, {1.101655f, 0.460753f, 0.570961f}, 0.5f, 8.0f},
    {0, { 0.5f, -6.0f, -8.0f}, {0.439468f, 0.278829f, 0.195178f}, 0.5f, 8.0f},
    {0, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    {0, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.5f, 8.0f},
    {1, { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}
};


//D:80035C40
u32 D_80035C40 = 0;
//D:80035C44
u32 D_80035C44 = 0;
//D:80035C48
u32 D_80035C48 = 0;
//D:80035C4C
u32 D_80035C4C = 0;
//D:80035C50
u32 D_80035C50 = 0;
//D:80035C54
u32 D_80035C54 = 0;
//D:80035C58
u32 D_80035C58 = 0;
//D:80035C5C
u32 D_80035C5C = 0;

//D:80035C60
f32 D_80035C60 = -1.0;
//D:80035C64
f32 D_80035C64 = 0.0;
//D:80035C68
f32 D_80035C68 = 1.0;
//D:80035C6C
f32 D_80035C6C = 0.0;
//D:80035C70
f32 D_80035C70 = 6.2536321;
//D:80035C74
f32 D_80035C74 = 6.2592888;
//D:80035C78
f32 D_80035C78 = 0.204238;
//D:80035C7C
f32 D_80035C7C = 0.25044999;
//D:80035C80
f32 D_80035C80 = 0.90482301;
//D:80035C84
f32 D_80035C84 = 0.28716999;
//D:80035C88
f32 D_80035C88 = 1.715736;
//D:80035C8C
f32 D_80035C8C = 0.37460899;
//D:80035C90
f32 D_80035C90 = 0.92193699;

//D:80035C94
f32 D_80035C94 = 0;


//D:80035C98
u32 D_80035C98 = 0;
//D:80035C9C
u32 D_80035C9C = 0;
//D:80035CA0
u32 D_80035CA0 = 0;
//D:80035CA4
s32 D_80035CA4 = 0xFFFFFFFF;
//D:80035CA8
u32 D_80035CA8 = 0;
//D:80035CAC
u32 D_80035CAC = 0;
//D:80035CB0
u32 D_80035CB0 = 0;
//D:80035CB4
u32 D_80035CB4 = 0;
//D:80035CB8
u32 D_80035CB8 = 0;
//D:80035CBC
u32 D_80035CBC = 0;
//D:80035CC0
u32 D_80035CC0 = 0;



//D:80035CC4
u32 D_80035CC4[] =                      { 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,           0,  0};
/* ModelRenderData D_8002CCBC = {NULL,
                                TRUE,
                                0x00000003,
                                NULL,
                                NULL,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                0,
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                CULLMODE_BOTH};
*/
//D:80035D00
u32 D_80035D00 = 0;
//D:80035D04
u32 D_80035D04[] = {1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80035D44
u32 D_80035D44[] = {
	1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

//D:0x80035E04
struct RicochetSoundsSmall ricochet_sounds_small = {
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_8_AFDM_A_SFX, RICO_8_AFDM_B_SFX, RICO_8_AFDM_C_SFX, RICO_8_AFDM_D_SFX,
    RICO_5_A_SFX,      RICO_5_B_SFX,      RICO_5_C_SFX,      RICO_5_D_SFX,
    RICO_6_HBBA_A_SFX, RICO_6_HBBA_B_SFX, RICO_6_HBBA_C_SFX, RICO_6_HBBA_D_SFX
};

//D:80035E2C
struct PunchSounds punch_sounds = {
    PUNCH1_SFX,
    PUNCH2_SFX,
    PUNCH3_SFX
};

//D:80035E34
struct BulletFleshSounds bullet_flesh_sounds = {
    HIT_BULLET_FLESH_SFX,
    HIT_BULLET_FLESH_SFX
};

struct LaserRichochetSounds laser_ricochet_sounds = {
    RICO_LASER2_SFX,
    RICO_LASER3_SFX
};

struct RicochetSoundsLarge ricochet_sounds_large = {
	RICO_12_GBU_A_SFX, RICO_12_GBU_B_SFX, RICO_12_GBU_C_SFX, RICO_12_GBU_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_6_TAJ_A_SFX,  RICO_6_TAJ_B_SFX,  RICO_6_TAJ_C_SFX,  RICO_6_TAJ_D_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_4_A_SFX,      RICO_4_B_SFX,      RICO_4_B_SFX,      RICO_4_C_SFX,
    RICO_5_A_SFX,      RICO_5_B_SFX,      RICO_5_C_SFX,      RICO_5_D_SFX,
    RICO_6_HBBA_A_SFX, RICO_6_HBBA_B_SFX, RICO_6_HBBA_C_SFX, RICO_6_HBBA_D_SFX
};

//D:80035E84
struct EarWhistleSounds ear_whistle_sounds = {
    RICO_EAR_WHISTLE1_SFX,
    RICO_EAR_WHISTLE2_SFX,
    RICO_EAR_WHISTLE3_SFX,
    RICO_EAR_WHISTLE4_SFX,
    RICO_EAR_WHISTLE5_SFX
};

//D:80035E90
struct sfx2 D_80035E90 = { RICO_LASER2_SFX, RICO_LASER3_SFX };
//D:80035E94
struct sfx3 D_80035E94 = { KNIFE_THROW1_SFX, KNIFE_THROW2_SFX, KNIFE_THROW3_SFX };
//D:80035E9C
struct gun_trigger_state g_ZeroTriggerState = { 0, 0 };
//D:80035EA0
//u32 D_80035EA0 = 0;
//D:80035EA4
u32 D_80035EA4 = 0;
//D:80035EA8
u32 D_80035EA8 = 0;
//D:80035EAC
u32 D_80035EAC = 0;
//D:80035EB0
u32 D_80035EB0[] = {0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//D:80035EEC
u32 dword_D_80035EEC = 0;

//D:80035EF0
#define AMMO_RELATED_MAX 30
AmmoStats ammo_related[AMMO_RELATED_MAX]  = {
    {    0x0,       0x0,       0x0,       0x0,       0x0, },
    {  0x320,     0x200,     0xC84,       0x0,       0x0, },
    {   0xC8,       0x0,       0x0,       0x0,       0x0, },
    {  0x190,     0x200,     0xC90,    0xC000,       0x0, },
    {   0x64,     0x200,     0xC9C,       0x0,       0x0, },
    {    0xC,     0x200,     0xCD8,       0x0,       0x0, },
    {    0x3,     0x200,     0xCC0,    0xC000,       0x0, },
    {    0xA,     0x200,     0xCFC,    0x3F80,       0x0, },
    {    0xA,     0x200,     0xD14,    0x3F80,       0x0, },
    {    0xA,     0x200,     0xD08,    0x3F80,       0x0, },
    {    0xA,     0x200,     0xCA8,       0x0,       0x0, },
    {    0xC,     0x200,     0xCB4,       0x0,       0x0, },
    {   0xC8,     0x200,     0xCE4,       0x0,       0x0, },
    {   0x64,     0x200,     0xCF0,       0x0,       0x0, },
    {   0x32,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {    0x2,       0x0,       0x0,       0x0,       0x0, },
    {    0x8,       0x0,       0x0,       0x0,       0x0, },
    {    0x6,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {    0x1,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {  0x3E8,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {    0xA,       0x0,       0x0,       0x0,       0x0, },
    {   0x32,     0x200,     0xD20,    0xBF80,       0x0, },
    {    0x1,       0x0,       0x0,       0x0,       0x0, },
};

//was previously attached to ammo_related[] (array at D:80035EF0)
//D:80036058
u16 D_80036058[] = { 0, 0, 0, 0, };


//i may belong to objecthandler.c
// D:80036060 canonically freedist
u32 D_80036060 = 0;




// forward declarations

void bullet_path_from_screen_center(coord3d* arg0, coord3d* arg1, enum GUNHAND arg2);
void sub_GAME_7F05EC1C(struct WeaponObjectRecord *arg0, struct coord3d *arg1, Mtxf *arg2, struct coord3d *arg3, s32 *arg4);
s32 sub_GAME_7F05C6FC(struct Weapon1PTransformKeyframe *, f32, Mtxf *, enum GUNHAND);
void analyzeGEKey(void);
void give_weapon_case_items(void);
struct ModelFileHeader * get_ptr_weapon_model_header_line(ITEM_IDS weapon);
s32 get_ammo_in_hands_weapon(enum GUNHAND hand);
s32 get_ammo_type_for_weapon(ITEM_IDS weapon);
f32 gunSetHorizontalOffset(GUNHAND hand);
void give_weapon_case_items(void);
void sub_GAME_7F05DA8C(GUNHAND hand, ITEM_IDS weaponnum_watchmenu);
void sub_GAME_7F05E808(GUNHAND hand);
void sub_GAME_7F0649D8(enum GUNHAND hand);

// end forward declarations




void set_cartridges_eject(u32 uParm1) {
    cartridges_eject = uParm1;
}

u32 get_cartridges_eject(void) {
    return cartridges_eject;
}


// Unreferenced
void nullsub_73(void)
{
#ifdef DEBUG
    osSyncPrintf("\t{");
    osSyncPrintf("0");
    osSyncPrintf(",{%ff,%ff,%ff}", sniperMeleeKeyframes2[D_80034CA0].pos.x, sniperMeleeKeyframes2[D_80034CA0].pos.y, sniperMeleeKeyframes2[D_80034CA0].pos.z);
    osSyncPrintf(",{%ff,%ff,%ff}", sniperMeleeKeyframes2[D_80034CA0].rot.x, sniperMeleeKeyframes2[D_80034CA0].rot.y, sniperMeleeKeyframes2[D_80034CA0].rot.z);
    osSyncPrintf(",0.5f,20.0f");
    osSyncPrintf("},\n");
#endif
    return;
}


// Unreferenced
void sub_GAME_7F05C540(coord3d* pos)
{
    Weapon1PTransformKeyframe* temp_v0;

    temp_v0 = &sniperMeleeKeyframes2[D_80034CA0];
    temp_v0->pos.x += pos->x;
    temp_v0->pos.y += pos->y;
    temp_v0->pos.z += pos->z;
}


// Unreferenced
void sub_GAME_7F05C594(Mtxf* mtxf)
{
    Mtxf sp18;
    matrix_4x4_set_rotation_around_xyz(&sniperMeleeKeyframes2[D_80034CA0].rot, &sp18);
    matrix_4x4_multiply_in_place(mtxf, &sp18);
    matrix_4x4_get_rotation_around_xyz(&sp18, &sniperMeleeKeyframes2[D_80034CA0].rot);
}


void sub_GAME_7F05C614(void)
{
    if (!cartridges_eject) { return; }

    g_CurrentPlayer->hands[0].field_92C = 1;
    matrix_4x4_set_rotation_around_xyz(&sniperMeleeKeyframes2[D_80034CA0].rot, (Mtxf* ) &g_CurrentPlayer->hands[0].field_8EC);
    matrix_4x4_set_position(&sniperMeleeKeyframes2[D_80034CA0].pos, (Mtxf* ) &g_CurrentPlayer->hands[0].field_8EC);
    cartridges_eject = 0;
}


// Unreferenced
void sub_GAME_7F05C6B8(void)
{
    D_80034CA0++;
    if (sniperMeleeKeyframes2[D_80034CA0].isFinalKey & 1)
    {
        D_80034CA0 = 0;
    }
}



#ifdef NONMATCHING
void sub_GAME_7F05C6FC(void) {
    fStack0000001c = param_1;
    local_90       = 1;
    do
    {
        if (fStack0000001c < *(param_2 + local_90 * 0x24 + 0x20)) break;
        fStack0000001c = fStack0000001c - *(param_2 + local_90 * 0x24 + 0x20);
        iVar6          = local_90 + 1;
        iVar2          = local_90 + 3;
        local_90       = iVar6;
    } while ((*(param_2 + iVar2 * 0x24) & 1) == 0);
    iStack00000014  = param_2;
    pfStack00000024 = param_4;
    iStack0000002c  = param_5;
    if ((*(param_2 + (local_90 + 2) * 0x24) & 1) == 0)
    {
        fVar3 = fStack0000001c / *(param_2 + local_90 * 0x24 + 0x20);
        fVar1 = *(param_2 + local_90 * 0x24 + 0x1c);
        Function_8237C1C0(param_2 + (local_90 + -1) * 0x24 + 0x10, afStack_30);
        Function_8237C1C0(iStack00000014 + local_90 * 0x24 + 0x10, afStack_50);
        Function_8237C1C0(iStack00000014 + (local_90 + 1) * 0x24 + 0x10, afStack_80);
        Function_8237C1C0(iStack00000014 + (local_90 + 2) * 0x24 + 0x10, afStack_40);
        Function_8237D298(afStack_50, afStack_80);
        Function_8237D298(afStack_80, afStack_40);
        Function_8237D298(afStack_50, afStack_30);
        lVar5 = ZEXT48(&stack0x00000000) - 0x70;
        Function_8237DA60(fVar3, afStack_30, afStack_50, afStack_80, afStack_40, param_6, lVar5);
        Function_82376408(fVar3, fVar1, iStack00000014 + (local_90 + -1) * 0x24 + 4, iStack00000014 + local_90 * 0x24 + 4, iStack00000014 + (local_90 + 1) * 0x24 + 4, iStack00000014 + (local_90 + 2) * 0x24 + 4, param_6, lVar5, local_18);
        if (iStack0000002c == 1)
        {
            local_18[0] = -local_18[0];
        }
        Function_8237C548(&local_70, pfStack00000024);
        matrix_4x4_set_position(local_18, pfStack00000024);
        uVar4 = 1;
    }
    else
    {
        matrix_4x4_set_rotation_around_xyz(param_2 + local_90 * 0x24 + 0x10, param_4);
        matrix_4x4_set_position(iStack00000014 + local_90 * 0x24 + 4, pfStack00000024);
        uVar4 = 0;
    }
    return uVar4;
}
#else
GLOBAL_ASM(
.text
glabel sub_GAME_7F05C6FC
/* 09122C 7F05C6FC 27BDFF60 */  addiu $sp, $sp, -0xa0
/* 091230 7F05C700 AFBF002C */  sw    $ra, 0x2c($sp)
/* 091234 7F05C704 AFB00028 */  sw    $s0, 0x28($sp)
/* 091238 7F05C708 AFA600A8 */  sw    $a2, 0xa8($sp)
/* 09123C 7F05C70C AFA700AC */  sw    $a3, 0xac($sp)
/* 091240 7F05C710 44856000 */  mtc1  $a1, $f12
/* 091244 7F05C714 C4840044 */  lwc1  $f4, 0x44($a0)
/* 091248 7F05C718 24020001 */  li    $v0, 1
/* 09124C 7F05C71C 24900024 */  addiu $s0, $a0, 0x24
/* 091250 7F05C720 460C203E */  c.le.s $f4, $f12
/* 091254 7F05C724 00000000 */  nop
/* 091258 7F05C728 4502000E */  bc1fl .L7F05C764
/* 09125C 7F05C72C 0002C0C0 */   sll   $t8, $v0, 3
/* 091260 7F05C730 C6000020 */  lwc1  $f0, 0x20($s0)
/* 091264 7F05C734 8E0E006C */  lw    $t6, 0x6c($s0)
.L7F05C738:
/* 091268 7F05C738 46006301 */  sub.s $f12, $f12, $f0
/* 09126C 7F05C73C 24420001 */  addiu $v0, $v0, 1
/* 091270 7F05C740 31CF0001 */  andi  $t7, $t6, 1
/* 091274 7F05C744 15E00006 */  bnez  $t7, .L7F05C760
/* 091278 7F05C748 26100024 */   addiu $s0, $s0, 0x24
/* 09127C 7F05C74C C6000020 */  lwc1  $f0, 0x20($s0)
/* 091280 7F05C750 460C003E */  c.le.s $f0, $f12
/* 091284 7F05C754 00000000 */  nop
/* 091288 7F05C758 4503FFF7 */  bc1tl .L7F05C738
/* 09128C 7F05C75C 8E0E006C */   lw    $t6, 0x6c($s0)
.L7F05C760:
/* 091290 7F05C760 0002C0C0 */  sll   $t8, $v0, 3
.L7F05C764:
/* 091294 7F05C764 0302C021 */  addu  $t8, $t8, $v0
/* 091298 7F05C768 0018C080 */  sll   $t8, $t8, 2
/* 09129C 7F05C76C 00988021 */  addu  $s0, $a0, $t8
/* 0912A0 7F05C770 8E190048 */  lw    $t9, 0x48($s0)
/* 0912A4 7F05C774 33280001 */  andi  $t0, $t9, 1
/* 0912A8 7F05C778 11000008 */  beqz  $t0, .L7F05C79C
/* 0912AC 7F05C77C 26040010 */   addiu $a0, $s0, 0x10
/* 0912B0 7F05C780 0FC161C5 */  jal   matrix_4x4_set_rotation_around_xyz
/* 0912B4 7F05C784 8FA500A8 */   lw    $a1, 0xa8($sp)
/* 0912B8 7F05C788 26040004 */  addiu $a0, $s0, 4
/* 0912BC 7F05C78C 0FC16266 */  jal   matrix_4x4_set_position
/* 0912C0 7F05C790 8FA500A8 */   lw    $a1, 0xa8($sp)
/* 0912C4 7F05C794 10000042 */  b     .L7F05C8A0
/* 0912C8 7F05C798 00001025 */   move  $v0, $zero
.L7F05C79C:
/* 0912CC 7F05C79C C6060020 */  lwc1  $f6, 0x20($s0)
/* 0912D0 7F05C7A0 2604FFEC */  addiu $a0, $s0, -0x14
/* 0912D4 7F05C7A4 27A50068 */  addiu $a1, $sp, 0x68
/* 0912D8 7F05C7A8 46066203 */  div.s $f8, $f12, $f6
/* 0912DC 7F05C7AC E7A80098 */  swc1  $f8, 0x98($sp)
/* 0912E0 7F05C7B0 C60A001C */  lwc1  $f10, 0x1c($s0)
/* 0912E4 7F05C7B4 0FC16CFD */  jal   quaternion_set_rotation_around_xyzf
/* 0912E8 7F05C7B8 E7AA0094 */   swc1  $f10, 0x94($sp)
/* 0912EC 7F05C7BC 26040010 */  addiu $a0, $s0, 0x10
/* 0912F0 7F05C7C0 0FC16CFD */  jal   quaternion_set_rotation_around_xyzf
/* 0912F4 7F05C7C4 27A50058 */   addiu $a1, $sp, 0x58
/* 0912F8 7F05C7C8 26040034 */  addiu $a0, $s0, 0x34
/* 0912FC 7F05C7CC 0FC16CFD */  jal   quaternion_set_rotation_around_xyzf
/* 091300 7F05C7D0 27A50048 */   addiu $a1, $sp, 0x48
/* 091304 7F05C7D4 26040058 */  addiu $a0, $s0, 0x58
/* 091308 7F05C7D8 0FC16CFD */  jal   quaternion_set_rotation_around_xyzf
/* 09130C 7F05C7DC 27A50038 */   addiu $a1, $sp, 0x38
/* 091310 7F05C7E0 27A40058 */  addiu $a0, $sp, 0x58
/* 091314 7F05C7E4 0FC16F84 */  jal   quaternion_ensure_shortest_path
/* 091318 7F05C7E8 27A50048 */   addiu $a1, $sp, 0x48
/* 09131C 7F05C7EC 27A40048 */  addiu $a0, $sp, 0x48
/* 091320 7F05C7F0 0FC16F84 */  jal   quaternion_ensure_shortest_path
/* 091324 7F05C7F4 27A50038 */   addiu $a1, $sp, 0x38
/* 091328 7F05C7F8 27A40058 */  addiu $a0, $sp, 0x58
/* 09132C 7F05C7FC 0FC16F84 */  jal   quaternion_ensure_shortest_path
/* 091330 7F05C800 27A50068 */   addiu $a1, $sp, 0x68
/* 091334 7F05C804 C7B00098 */  lwc1  $f16, 0x98($sp)
/* 091338 7F05C808 27A90078 */  addiu $t1, $sp, 0x78
/* 09133C 7F05C80C AFA90014 */  sw    $t1, 0x14($sp)
/* 091340 7F05C810 27A40068 */  addiu $a0, $sp, 0x68
/* 091344 7F05C814 27A50058 */  addiu $a1, $sp, 0x58
/* 091348 7F05C818 27A60048 */  addiu $a2, $sp, 0x48
/* 09134C 7F05C81C 27A70038 */  addiu $a3, $sp, 0x38
/* 091350 7F05C820 0FC170BC */  jal   quaternion_7F05C2F0
/* 091354 7F05C824 E7B00010 */   swc1  $f16, 0x10($sp)
/* 091358 7F05C828 C7B20098 */  lwc1  $f18, 0x98($sp)
/* 09135C 7F05C82C C7A40094 */  lwc1  $f4, 0x94($sp)
/* 091360 7F05C830 27AA0088 */  addiu $t2, $sp, 0x88
/* 091364 7F05C834 AFAA0018 */  sw    $t2, 0x18($sp)
/* 091368 7F05C838 2604FFE0 */  addiu $a0, $s0, -0x20
/* 09136C 7F05C83C 26050004 */  addiu $a1, $s0, 4
/* 091370 7F05C840 26060028 */  addiu $a2, $s0, 0x28
/* 091374 7F05C844 2607004C */  addiu $a3, $s0, 0x4c
/* 091378 7F05C848 E7B20010 */  swc1  $f18, 0x10($sp)
/* 09137C 7F05C84C 0FC16C09 */  jal   sub_GAME_7F05B024
/* 091380 7F05C850 E7A40014 */   swc1  $f4, 0x14($sp)
/* 091384 7F05C854 8FAB00AC */  lw    $t3, 0xac($sp)
/* 091388 7F05C858 24010001 */  li    $at, 1
/* 09138C 7F05C85C C7A60088 */  lwc1  $f6, 0x88($sp)
/* 091390 7F05C860 15610009 */  bne   $t3, $at, .L7F05C888
/* 091394 7F05C864 27A40078 */   addiu $a0, $sp, 0x78
/* 091398 7F05C868 C7AA0078 */  lwc1  $f10, 0x78($sp)
/* 09139C 7F05C86C C7B2007C */  lwc1  $f18, 0x7c($sp)
/* 0913A0 7F05C870 46003207 */  neg.s $f8, $f6
/* 0913A4 7F05C874 46005407 */  neg.s $f16, $f10
/* 0913A8 7F05C878 46009107 */  neg.s $f4, $f18
/* 0913AC 7F05C87C E7A80088 */  swc1  $f8, 0x88($sp)
/* 0913B0 7F05C880 E7B00078 */  swc1  $f16, 0x78($sp)
/* 0913B4 7F05C884 E7A4007C */  swc1  $f4, 0x7c($sp)
.L7F05C888:
/* 0913B8 7F05C888 0FC16D8A */  jal   quaternion_to_matrix
/* 0913BC 7F05C88C 8FA500A8 */   lw    $a1, 0xa8($sp)
/* 0913C0 7F05C890 27A40088 */  addiu $a0, $sp, 0x88
/* 0913C4 7F05C894 0FC16266 */  jal   matrix_4x4_set_position
/* 0913C8 7F05C898 8FA500A8 */   lw    $a1, 0xa8($sp)
/* 0913CC 7F05C89C 24020001 */  li    $v0, 1
.L7F05C8A0:
/* 0913D0 7F05C8A0 8FBF002C */  lw    $ra, 0x2c($sp)
/* 0913D4 7F05C8A4 8FB00028 */  lw    $s0, 0x28($sp)
/* 0913D8 7F05C8A8 27BD00A0 */  addiu $sp, $sp, 0xa0
/* 0913DC 7F05C8AC 03E00008 */  jr    $ra
/* 0913E0 7F05C8B0 00000000 */   nop
)
#endif





WeaponStats *get_ptr_item_statistics(ITEM_IDS item) {
    if (gitem_structs[item].has_no_model == 0) { /* weapon has model, return stats struct */
        return gitem_structs[item].item_weapon_stats;
    }
    return &default_weaponstats; /* no model, return defaults */
}




void copy_item_in_hand(coord3d *pos)
{
    ITEM_IDS item;
    WeaponStats *stats;

    item = getCurrentPlayerWeaponId(0);
    stats = get_ptr_item_statistics(item);

    pos->x = stats->PosX;
    pos->y = stats->PosY;
    pos->z = stats->PosZ;
}


void copy_item_in_hand_to_main_list(coord3d *pos) {

    WeaponStats *stats;
    ITEM_IDS item;

    item = getCurrentPlayerWeaponId(0);
    stats = get_ptr_item_statistics(item);

    stats->PosX = pos->x;
    stats->PosY = pos->y;
    stats->PosZ = pos->z;
}


void bgunCalculateBlend(enum GUNHAND handnum)
{
    s32 sp60[2];
    s32 sp58[2];
    f32 mult = get_ptr_item_statistics(getCurrentPlayerWeaponId(handnum))->Sway;

    sp60[handnum] = (g_CurrentPlayer->hands[handnum].curblendpos + 2) % 4;
    sp58[handnum] = (g_CurrentPlayer->hands[handnum].curblendpos + 1) % 4;
    g_CurrentPlayer->hands[handnum].curblendpos = sp58[handnum];

    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.08f * mult;
    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].y = (RANDOMFRAC() - 0.5f) * 0.1f * mult;
    g_CurrentPlayer->hands[handnum].blendlook[sp60[handnum]].z = -1;

    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.1f * mult;
    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].y = 1;
    g_CurrentPlayer->hands[handnum].blendup[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 0.1f * mult;

    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].x = (RANDOMFRAC() * 0.75f) + 1.5f;
    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].y = (2 + RANDOMFRAC()) * g_CurrentPlayer->hands[handnum].blendscale1;
    g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 2.5f;

    if (g_CurrentPlayer->hands[handnum].sideflag < 0)
    {
        g_CurrentPlayer->hands[handnum].blendpos[sp60[handnum]].x *= -1;

        if (g_CurrentPlayer->hands[handnum].sideflag == -2)
        {
            g_CurrentPlayer->hands[handnum].sideflag = 1;
        }
        else
        {
            g_CurrentPlayer->hands[handnum].sideflag = -2;
        }
    }
    else
    {
        if (g_CurrentPlayer->hands[handnum].sideflag == 2)
        {
            g_CurrentPlayer->hands[handnum].sideflag = -1;
        }
        else
        {
            g_CurrentPlayer->hands[handnum].sideflag = 2;
        }
    }

    g_CurrentPlayer->hands[handnum].blendscale1 = -g_CurrentPlayer->hands[handnum].blendscale1;
}


s32 Gun_hand_without_item(enum GUNHAND arg0)
{
    return g_CurrentPlayer->hand_invisible[arg0] > 0
        || (g_CurrentPlayer->hand_item[arg0] == 0 && g_CurrentPlayer->field_2A44[arg0] < 0);
}


s32 get_itemtype_in_hand(GUNHAND hand)
{
    return g_CurrentPlayer->hand_item[hand];
}


ModelFileHeader *get_ptr_itemheader_in_hand(GUNHAND hand)
{
    return &g_CurrentPlayer->copy_of_body_obj_header[hand];
}


u8 * getPlayerWeaponBufferForHand(GUNHAND hand)
{
    return g_CurrentPlayer->ptr_hand_weapon_buffer[hand];
}


u32 getSizeBufferWeaponInHand(int hand)
{
    return size_item_buffer[hand];
}


void remove_item_in_hand(GUNHAND hand)
{
  g_CurrentPlayer->hand_invisible[hand] = 0;
  g_CurrentPlayer->hand_item[hand] = ITEM_UNARMED;
  g_CurrentPlayer->field_2A44[hand] = -1;
  g_CurrentPlayer->lock_hand_model[hand] = 1;
  return;
}


void place_item_in_hand_swap_and_make_visible(GUNHAND hand, ITEM_IDS item)
{
    if (g_CurrentPlayer->lock_hand_model[hand]) { return; }

    if (g_CurrentPlayer->hand_invisible[hand] >= 0)
    {
        if (item != g_CurrentPlayer->hand_item[hand])
        {
            g_CurrentPlayer->hand_invisible[hand] = -1;
            g_CurrentPlayer->field_2A44[hand] = item;
        }
        return;
    }

    if (item != g_CurrentPlayer->hand_item[hand])
    {
        g_CurrentPlayer->field_2A44[hand] = item;
        return;
    }

    g_CurrentPlayer->hand_invisible[hand] = 1;
}


char * get_ptr_item_text_call_line(ITEM_IDS item)
{
  if (item == ITEM_FIST) {
    item = g_CurrentPlayer->cur_item_weapon_getname;
  }
  return gitem_structs[item].item_file_name;
}


struct ModelFileHeader * get_ptr_weapon_model_header_line(ITEM_IDS weapon)
{
    if (weapon == ITEM_FIST) {
        weapon = g_CurrentPlayer->cur_item_weapon_getname;
    }
    return gitem_structs[weapon].item_header;
}


int getCurrentWeaponOrItem(void)
{
    return g_CurrentPlayer->cur_item_weapon_getname;
}


void used_to_load_1st_person_model_on_demand(enum GUNHAND hand)
{
    u32 size_buffer_weapon;
    s8* ptr_item_text;
    ModelFileHeader* ptr_weapon_model;
    u8* buffer_weapon;
    enum ITEM_IDS field_2a44;

    if ((g_CurrentPlayer->hand_invisible[hand] < 0) && (g_CurrentPlayer->lock_hand_model[hand] == 0))
    {
        if ((g_CurrentPlayer->hand_invisible[hand] < -2) || (g_CurrentPlayer->hand_item[hand] == ITEM_UNARMED))
        {
            field_2a44 = g_CurrentPlayer->field_2A44[hand];
            ptr_item_text = (s8*)get_ptr_item_text_call_line(field_2a44);
            ptr_weapon_model = get_ptr_weapon_model_header_line(field_2a44);

            if ((ptr_item_text != NULL) && (ptr_weapon_model != NULL))
            {
                buffer_weapon = getPlayerWeaponBufferForHand(hand);
                size_buffer_weapon = getSizeBufferWeaponInHand(hand);

                g_CurrentPlayer->copy_of_body_obj_header[hand] = *ptr_weapon_model;

                if (field_2a44 == ITEM_SUIT_LF_HAND)
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0xBD70, size_buffer_weapon + 0xFFFF4290);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8* ) ptr_item_text, buffer_weapon, 0xBD70, &g_CurrentPlayer->item_related[hand]);
                }
                else if ((field_2a44 == ITEM_TRIGGER) || (field_2a44 == ITEM_WATCHLASER))
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], buffer_weapon + 0xAFD0, size_buffer_weapon + 0xFFFF5030);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8* ) ptr_item_text, buffer_weapon, 0xAFD0, &g_CurrentPlayer->item_related[hand]);
                }
                else
                {
                    texInitPool(&g_CurrentPlayer->item_related[hand], &buffer_weapon[D_80032464[hand]], size_buffer_weapon - D_80032464[hand]);
                    load_object_fill_header(&g_CurrentPlayer->copy_of_body_obj_header[hand], (u8* ) ptr_item_text, buffer_weapon, D_80032464[hand], &g_CurrentPlayer->item_related[hand]);
                }
            }

            g_CurrentPlayer->hand_invisible[hand] = 1;
            g_CurrentPlayer->hand_item[hand] = field_2a44;
            g_CurrentPlayer->field_2A44[hand] = -1;

        }
        else
        {
            g_CurrentPlayer->hand_invisible[hand]--;
        }
    }
}


// Called by unused functions.
ITEM_IDS sub_GAME_7F05D334(ITEM_IDS item, s32 arg1)
{
    while (arg1 > 0)
    {
        do
        {
            item = (item + 1) % ITEM_BOMBCASE;
        } while (bondinvItemAvailable(item) == 0);
        arg1--;
    }

    while (arg1 < 0)
    {
        do
        {
            item--;
            if (item < 0)
            {
                item = 0x20 - (-(item + 1) % ITEM_BOMBCASE);
            }
        } while (bondinvItemAvailable(item) == 0);
        arg1++;
    }

    return item;
}


ITEM_IDS get_next_weapon_in_cycle_for_hand(GUNHAND hand, s32 direction)
{
	if (g_CurrentPlayer->hands[hand].when_detonating_mines_is_0 == 5) {
		if (
			(direction < 0 && (g_CurrentPlayer->hands[hand].field_8B8 > 0)) ||
			(direction > 0 && (g_CurrentPlayer->hands[hand].field_8B8 < 0)) ) {
			return getCurrentPlayerWeaponId(hand);
		}
		else {
			return g_CurrentPlayer->hands[hand].weapon_next_weapon;
		}

    }
    if (g_CurrentPlayer->hands[hand].when_detonating_mines_is_0 == 6) {
        return g_CurrentPlayer->hands[hand].weapon_next_weapon;
    }
    return getCurrentPlayerWeaponId(hand);
}


void likely_change_weapon_in_hand(enum GUNHAND hand, s32 arg1, s32 arg2)
{
    if ((g_CurrentPlayer->hands[hand].when_detonating_mines_is_0 == 5) || (g_CurrentPlayer->hands[hand].when_detonating_mines_is_0 == 6))
    {
        g_CurrentPlayer->hands[hand].field_8B0 = g_CurrentPlayer->hands[hand].field_890;

#ifdef VERSION_EU
        if (getPlayerCount() == 1) {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xE;
        } else {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xA;
        }
#else
        if (getPlayerCount() == 1) {
            g_CurrentPlayer->hands[hand].field_8B0 += 0x11;
        } else {
            g_CurrentPlayer->hands[hand].field_8B0 += 0xD;
        }
#endif

    }

    if (get_next_weapon_in_cycle_for_hand(hand, 0) != arg1)
    {
        if ((g_CurrentPlayer->hands[hand].when_detonating_mines_is_0 != 5) && (g_CurrentPlayer->hands[hand].when_detonating_mines_is_0 != 6))
        {
            g_CurrentPlayer->hands[hand].weapon_current_animation = 5;
        }
        g_CurrentPlayer->hands[hand].weapon_next_weapon = arg1;
        g_CurrentPlayer->hands[hand].weapon_animation_trigger = 1;
        g_CurrentPlayer->hands[hand].field_8B8 = arg2;
    }
}


// Unused
void sub_GAME_7F05D610(GUNHAND hand)
{
    likely_change_weapon_in_hand(hand, sub_GAME_7F05D334(get_next_weapon_in_cycle_for_hand(hand, 0), 1), 0);
}


// Unused
void sub_GAME_7F05D650(GUNHAND hand)
{
    likely_change_weapon_in_hand(hand, sub_GAME_7F05D334(get_next_weapon_in_cycle_for_hand(hand, 0), -1), 0);
}


void sub_GAME_7F05D690(void)
{
    currentPlayerEquipWeaponWrapper(GUNRIGHT, g_CurrentPlayer->hands[GUNRIGHT].previous_weapon);
    currentPlayerEquipWeaponWrapper(GUNLEFT, g_CurrentPlayer->hands[GUNLEFT].previous_weapon);
}


void advance_through_inventory(void)
{
    ITEM_IDS nextright;
    ITEM_IDS nextleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, 1);

    if ((nextright >= ITEM_BOMBCASE) || (nextleft >= ITEM_BOMBCASE))
    {
        nextright = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        nextleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else
    {
        bondinvCycleForward(&nextright, &nextleft, FALSE);
    }

    likely_change_weapon_in_hand(GUNRIGHT, nextright, 1);
    likely_change_weapon_in_hand(GUNLEFT, nextleft, 1);
}


void backstep_through_inventory(void)
{
    ITEM_IDS nextright;
    ITEM_IDS nextleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, -1);
    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, -1);

    if ((nextright >= ITEM_BOMBCASE) || (nextleft >= ITEM_BOMBCASE))
    {
        nextright = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        nextleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else
    {
        bondinvCycleBackward(&nextright, &nextleft, FALSE);
    }

    likely_change_weapon_in_hand(GUNRIGHT, nextright, -1);
    likely_change_weapon_in_hand(GUNLEFT, nextleft, -1);
}

void autoadvance_on_deplete_all_ammo(void)
{
	ITEM_IDS nextright;
	ITEM_IDS nextleft;
	ITEM_IDS duperight;
	ITEM_IDS dupeleft;

    nextright = get_next_weapon_in_cycle_for_hand(GUNRIGHT, 1);
    duperight = nextright;

    nextleft = get_next_weapon_in_cycle_for_hand(GUNLEFT, 1);
    dupeleft = nextleft;

    if ((duperight >= ITEM_BOMBCASE) || (dupeleft >= ITEM_BOMBCASE))
    {
        duperight = g_CurrentPlayer->hands[GUNRIGHT].previous_weapon;
        dupeleft = g_CurrentPlayer->hands[GUNLEFT].previous_weapon;
    }
    else if ((duperight == ITEM_REMOTEMINE) && ((bondinvItemAvailable(ITEM_TRIGGER))))
    {
        duperight = ITEM_TRIGGER;
        dupeleft = ITEM_UNARMED;
    }
    else
    {
        bondinvCycleForward(&duperight, &dupeleft, TRUE);

        if ((duperight < nextright) || ((duperight == nextright) && (nextleft >= dupeleft)))
        {
			duperight = nextright;
			dupeleft = nextleft;
			bondinvCycleBackward(&duperight, &dupeleft, TRUE);
        }
    }

    likely_change_weapon_in_hand(GUNRIGHT, duperight, 1);
    likely_change_weapon_in_hand(GUNLEFT, dupeleft, 1);
}

s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon) {
    g_CurrentPlayer->hands[hand].weapon_current_animation = 5;
    g_CurrentPlayer->hands[hand].weapon_next_weapon = next_weapon;
    g_CurrentPlayer->hands[hand].weapon_animation_trigger = 0;
}

void attempt_reload_item_in_hand(GUNHAND hand) {
    s32 ammo_type = get_ammo_type_for_weapon(getCurrentPlayerWeaponId(hand));
    if (ammo_type != 0) {
        if (g_CurrentPlayer->hands[hand].weapon_current_animation == 0) {
            g_CurrentPlayer->hands[hand].weapon_current_animation = 9;
        }
    }
}

ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weaponnum;
}

void draw_item_in_hand(GUNHAND hand, s32 next_weapon) {
	g_CurrentPlayer->hands[hand].weapon_current_animation = 0xE;
	g_CurrentPlayer->hands[hand].weapon_next_weapon = next_weapon;
}

ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) {
	if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu >= 0) {
		return g_CurrentPlayer->hands[hand].weaponnum_watchmenu;
	} else {
		return g_CurrentPlayer->hands[hand].weaponnum;
	}
}

void sub_GAME_7F05DA8C(GUNHAND hand, ITEM_IDS weaponnum_watchmenu) {
    place_item_in_hand_swap_and_make_visible(hand, weaponnum_watchmenu);
	g_CurrentPlayer->hands[hand].weaponnum_watchmenu = weaponnum_watchmenu;
}

void sub_GAME_7F05DAE4(GUNHAND hand) {
    if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu >= 0) {
        place_item_in_hand_swap_and_make_visible(hand, g_CurrentPlayer->hands[hand].weaponnum);
		g_CurrentPlayer->hands[hand].weaponnum_watchmenu = -1;
    }
}


void currentPlayerUnEquipWeaponWrapper(enum GUNHAND hand, enum ITEM_IDS weapid)
{
    enum ITEM_IDS weapon_num;
    s32 ammo_type;

    weapon_num = g_CurrentPlayer->hands[hand].weaponnum;
    ammo_type = get_ammo_type_for_weapon(weapon_num);

    if (g_CurrentPlayer->hands[hand].weaponnum_watchmenu < 0)
    {
        place_item_in_hand_swap_and_make_visible(hand, weapid);
    }

    if (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0)
    {
        g_CurrentPlayer->ammoheldarr[ammo_type] += g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine;
    }

    if (weapon_num < ITEM_BOMBCASE)
    {
        g_CurrentPlayer->hands[hand].previous_weapon = weapon_num;
    }

    if (getPlayerCount() >= 2)
    {
        sub_GAME_7F09B368(hand);
    }

    sub_GAME_7F05FB00(hand);
    g_CurrentPlayer->hands[hand].weaponnum = weapid;
    g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine = 0;
    g_CurrentPlayer->hands[hand].field_A4C = 0;
    g_CurrentPlayer->hands[hand].field_A50 = 0;
    bondinvDetermineEquippedItem();
}


s8 get_hands_firing_status(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weapon_firing_status;
}

f32 sub_GAME_7F05DCB8(GUNHAND hand) {
	return g_CurrentPlayer->hands[hand].field_A34;
}

/**
 * Positions the gun to the right or left side of the screen depending on which hand is holding it.
 */
f32 gunSetHorizontalOffset(GUNHAND hand)
{
	f32 offset;

	if (hand == GUNRIGHT)
	{
		offset = get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNRIGHT))->PosX;
	}
	else
	{
		offset = -get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNLEFT))->PosX;
	}

	return offset;
}

f32 get_item_in_hand_zoom(void) {
    if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
        return g_CurrentPlayer->sniper_zoom;
    }
    if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
        return g_CurrentPlayer->camera_zoom;
    }
    return get_ptr_item_statistics(get_item_in_hand_or_watch_menu(GUNRIGHT))->Zoom;
}

void camera_sniper_zoom_out(f32 zoom)
{
	if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
		g_CurrentPlayer->sniper_zoom *= (1.0f + (zoom * 0.1f));
		if (g_CurrentPlayer->sniper_zoom > 60.0f) {
			g_CurrentPlayer->sniper_zoom = 60.0f;
		}
	}
	else
	{
		if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
			g_CurrentPlayer->camera_zoom *= (1.0f + (zoom * 0.1f));
			if (g_CurrentPlayer->camera_zoom > 60.0f) {
				g_CurrentPlayer->camera_zoom = 60.0f;
			}
		}
	}
}

void camera_sniper_zoom_in(f32 zoom)
{
	if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_SNIPERRIFLE) {
		g_CurrentPlayer->sniper_zoom /= (1.0f + (zoom * 0.1f));
		if (g_CurrentPlayer->sniper_zoom < 7.0f) {
			g_CurrentPlayer->sniper_zoom = 7.0f;
		}
	}
	else
	{
		if (get_item_in_hand_or_watch_menu(GUNRIGHT) == ITEM_CAMERA) {
			g_CurrentPlayer->camera_zoom /= (1.0f + (zoom * 0.1f));
			if (g_CurrentPlayer->camera_zoom < 7.0f) {
				g_CurrentPlayer->camera_zoom = 7.0f;
			}
		}
	}
}

f32 bondwalkItemGetDestructionAmount(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->DestructionAmount;
}


f32 bondwalkItemGetForceOfImpact(ITEM_IDS item)
{
	return get_ptr_item_statistics(item)->ForceOfImpact;
}

/**
 * Address 0x7F05DFCC
 */
s8 bondwalkItemGetAutomaticFiringRate(ITEM_IDS item) {
    return get_ptr_item_statistics(item)->AutomaticFiringRate;
}


u8 bondwalkItemGetSoundTriggerRate(ITEM_IDS item) {
    return get_ptr_item_statistics(item)->SoundTriggerRate;
}


u16 bondwalkItemGetSound(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->Sound;
}


u8 bondwalkItemGetObjectsShootThrough(ITEM_IDS item)
{
  return get_ptr_item_statistics(item)->ObjectsShootThrough;
}


s32 bondwalkItemHasAmmo(ITEM_IDS item)
{
    if (bondwalkItemCheckBitflags(item, WEAPONSTATBITFLAG_HAS_AMMO) != 0)
    {
        if ((get_ammo_type_for_weapon(item) == 0) || (get_ammo_count_for_weapon(item) > 0))
        {
            return 1;
        }
    }
    return 0;
}


u32 bondwalkItemCheckBitflags(ITEM_IDS item, u32 mask)
{
  return ((get_ptr_item_statistics(item)->BitFlags & mask) != 0);
}


void gunSetBondWeaponSway(f32 breathing, f32 arg1, f32 arg2, f32 arg3)
{
    f32 dampt[2];
    s32 i;
    u32 unused[2];
    f32 sp50 = arg2;
    f32 sp4c;
    u32 stack2;
    f32 minbreathing;

    if (sp50 < 0.0f) { sp50 = -sp50; }

    if (arg1 > 0.8f)
    {
        g_CurrentPlayer->gunposamplitude = 1.0f;
    }
    else
    {
        if (arg1 > 0.1f)
        {
            f32 tmp = (1.0f - cosf((arg1 - 0.1f) * M_TAU_F / 2.8f));
            g_CurrentPlayer->gunposamplitude = 0.8f * tmp + 0.2f;
        }
        else
        {
            g_CurrentPlayer->gunposamplitude = 0.1f;
        }
    }

    if (g_CurrentPlayer->gunposamplitude < (bondviewGetBondBreathing() * 0.3f))
    {
        g_CurrentPlayer->gunposamplitude = bondviewGetBondBreathing() * 0.3f;
    }

    if (g_CurrentPlayer->gunposamplitude < 0.5f * sp50)
    {
        g_CurrentPlayer->gunposamplitude = 0.5f * sp50;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_1080 = (g_CurrentPlayer->field_1080 * (PAL ? 0.9403f : 0.95f)) + g_CurrentPlayer->gunposamplitude;
    }

    g_CurrentPlayer->gunposamplitude = g_CurrentPlayer->field_1080 * (PAL ? 0.059700012f : 0.050000012f);

    minbreathing = 0.016666668f * sp50;
    if (breathing < minbreathing)
    {
        breathing = minbreathing;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->field_107C = (g_CurrentPlayer->field_107C * (PAL ? 0.9403f : 0.95f)) + breathing;
    }

    breathing = g_CurrentPlayer->field_107C * (PAL ? 0.059700012f : 0.050000012f);

    sp4c = breathing * g_GlobalTimerDelta;
    dampt[0] = g_CurrentPlayer->hands[0].dampt + sp4c;

    while (dampt[0] >= 1.0f)
    {
        bgunCalculateBlend(GUNRIGHT);
        dampt[0] -= 1.0f;
        g_CurrentPlayer->syncoffset++;
    }

    g_CurrentPlayer->synccount += g_GlobalTimerDelta;

    if (g_CurrentPlayer->synccount > 60.0f)
    {
        g_CurrentPlayer->synccount = 0.0f;
        g_CurrentPlayer->syncchange = (RANDOMFRAC() - 0.5f) * 0.2f / 60.0f;
    }

    if (g_CurrentPlayer->syncchange + sp4c > 0.0f)
    {
        g_CurrentPlayer->gunsync += g_CurrentPlayer->syncchange;
    }

    if (g_CurrentPlayer->gunsync > 0.5f)
    {
        g_CurrentPlayer->gunsync = 0.5f;
    }
    else if (g_CurrentPlayer->gunsync < -0.5f)
    {
        g_CurrentPlayer->gunsync = -0.5f;
    }
    else if (g_CurrentPlayer->gunsync < 0.1f && g_CurrentPlayer->gunsync > -0.1f)
    {
        if (g_CurrentPlayer->gunsync > 0.0f)
        {
            g_CurrentPlayer->gunsync = -0.1f;
        }
        else
        {
            g_CurrentPlayer->gunsync = 0.1f;
        }
    }

    dampt[1] = dampt[0] + g_CurrentPlayer->syncoffset + g_CurrentPlayer->gunsync;

    while (dampt[1] >= 1.0f)
    {
        bgunCalculateBlend(GUNLEFT);
        dampt[1] -= 1.0f;
        g_CurrentPlayer->syncoffset--;
    }

    for (i = 0; i < 2; i++)
    {
        g_CurrentPlayer->hands[i].dampt = dampt[i];
        g_CurrentPlayer->hands[i].weapon_theta_displacement = -1.75f * arg3;
        g_CurrentPlayer->hands[i].weapon_verta_displacement = -2.0f * arg2;
    }
}


void gunSetOffsetRelated(f32 param_1)
{
    g_CurrentPlayer->hands[GUNRIGHT].field_A30 = (1.0f - cosf(param_1)) * 5.0f;
    g_CurrentPlayer->hands[GUNLEFT].field_A30 = (1.0f - cosf(param_1)) * 5.0f;
}


f32 get_value_if_watch_is_on_hand_or_not(GUNHAND hand)
{
  if ((getCurrentPlayerWeaponId(hand) == ITEM_TRIGGER) || (getCurrentPlayerWeaponId(hand) == ITEM_WATCHLASER))
  {
    return 0.08726647f;
  }
  else
  {
    return 0.17453294f;
  }
}


void sub_GAME_7F05E6B4(enum GUNHAND hand, s32 arg1)
{
    if (arg1 != 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A84 < get_value_if_watch_is_on_hand_or_not(hand))
        {
            g_CurrentPlayer->hands[hand].field_A84 += (0.029088823f * g_GlobalTimerDelta);
        }
        if (g_CurrentPlayer->hands[hand].field_A84 > get_value_if_watch_is_on_hand_or_not(hand)) {
            g_CurrentPlayer->hands[hand].field_A84 = get_value_if_watch_is_on_hand_or_not(hand);
        }
    }
    else
    {
        if (g_CurrentPlayer->hands[hand].field_A84 > 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A84 -= (0.017453294f * g_GlobalTimerDelta);
        }
        if (g_CurrentPlayer->hands[hand].field_A84 < 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A84 = 0.0f;
        }
    }
}


void sub_GAME_7F05E808(GUNHAND hand) {
	g_CurrentPlayer->hands[hand].field_A8C = 1;
}


void sub_GAME_7F05E83C(GUNHAND hand)
{
    f32 recoil_back;

    recoil_back = get_ptr_item_statistics(get_item_in_hand_or_watch_menu(hand))->BoltRecoilBack;

    if (g_CurrentPlayer->hands[hand].field_A8C != 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A88 < recoil_back)
        {
            g_CurrentPlayer->hands[hand].field_A88 = (g_CurrentPlayer->hands[hand].field_A88 + (recoil_back * 0.25f * g_GlobalTimerDelta));

        }
        if (recoil_back <= g_CurrentPlayer->hands[hand].field_A88) {
            g_CurrentPlayer->hands[hand].field_A88 = recoil_back;
            g_CurrentPlayer->hands[hand].field_A8C = 0;
        }
    }
    else if (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0)
    {
        if (g_CurrentPlayer->hands[hand].field_A88 > 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A88 = (g_CurrentPlayer->hands[hand].field_A88 - (recoil_back * 0.16666667f * g_GlobalTimerDelta));

        }
        if (g_CurrentPlayer->hands[hand].field_A88 < 0.0f)
        {
            g_CurrentPlayer->hands[hand].field_A88 = 0.0f;
        }
    }
}


void sub_GAME_7F05E978(Model* model, s32 val)
{
    if (model->obj->Switches[8] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[8])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[9] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[9])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[10] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[10])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[11] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[11])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[12] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[12])->DisplayList.unk00 = val;
    }

    if (model->obj->Switches[13] != NULL)
    {
        modelGetNodeRwData(model, model->obj->Switches[13])->DisplayList.unk00 = val;
    }

    if (model->obj->numSwitches >= 0x24)
    {
        if (model->obj->Switches[35] != NULL)
        {
            modelGetNodeRwData(model, model->obj->Switches[35])->DisplayList.unk00 = val;
        }
    }
}


void sub_GAME_7F05EA94(Model* model, s32 val)
{
    ModelNode* switch_14;
    ModelNode* switch_15;

    if (model->obj->numSwitches >= 0x10)
    {
        switch_14 = model->obj->Switches[14];
        if (switch_14 != NULL)
        {
            // Guessing DisplayList here
            modelGetNodeRwData(model, switch_14)->DisplayList.unk00 = val;
        }

        switch_15 = model->obj->Switches[15];
        if (switch_15 != NULL)
        {
            // Guessing DisplayList here
            modelGetNodeRwData(model, switch_15)->DisplayList.unk00 = val;
        }
    }
}


/**
 * Address 0x7F05EB0C.
*/
void sub_GAME_7F05EB0C(ObjectRecord *obj, coord3d *pos, StandTile *stan, Mtxf *matrix, coord3d *arg4, Mtxf *arg5, PropRecord *arg6)
{
    PropRecord *temp_s1;
    Projectile *temp_v0;

    temp_s1 = obj->prop;

    if (temp_s1 != NULL)
    {
        chrpropActivate(temp_s1);
        chrpropEnable(temp_s1);
        matrix_scalar_multiply(obj->model->scale, matrix);
        objChangeShading(obj, pos, matrix, stan);

        // loadobjectmodel.c
        setupUpdateObjectRoomPosition(obj);

        chrobjCollisionRelated(obj);
        sub_GAME_7F03FDA8(temp_s1);

        if (obj->runtime_bitflags & RUNTIMEBITFLAG_DEPOSIT)
        {
            temp_v0 = obj->projectile;
            temp_v0->flags |= 0x41;
            obj->projectile->ownerprop = arg6;
            projectileSetSticky(temp_s1);
            matrix_4x4_copy(arg5, &obj->projectile->mtx);
            obj->projectile->speed.f[0] = arg4->f[0];
            obj->projectile->speed.f[1] = arg4->f[1];
            obj->projectile->speed.f[2] = arg4->f[2];
            obj->projectile->obj = obj;
            obj->projectile->unkE8 = D_80048380;
        }
    }
}





#ifdef NONMATCHING
void sub_GAME_7F05EC1C(void)
{
    f32 spD0;
    f32 spCC;
    f32 spC8;
    s32 spC4;
    f32 spB8;
    f32 spB4;
    s32 spB0;
    s8  spA9;
    s8  spA8;
    ? sp54;
    ? sp50;
    f32   temp_f0;
    f32   temp_f14;
    void *temp_s0;
    f32   phi_f16;
    f32   phi_f14;
    s32   phi_return;

    phi_return = arg0->unk10;
    if (arg0->unk10 != 0)
    {
        temp_s0 = get_curplayer_positiondata();
        temp_f0 = sub_GAME_7F089778(pPlayer);
        spB0    = 0;
        if (arg1->unk4 < temp_s0->unkC)
        {
            phi_f16 = arg1->unk4 - temp_f0;
            phi_f14 = temp_s0->unkC - temp_f0;
        }
        else
        {
            phi_f16 = temp_s0->unkC - temp_f0;
            phi_f14 = arg1->unk4 - temp_f0;
        }
        spB4 = phi_f16;
        spB8 = phi_f14;
        spC4 = temp_s0->unk14;
        bondviewUpdateGuardTankFlagsRelated(temp_s0->unkC, phi_f14, temp_s0, 0);
        temp_f14 = phi_f14;
        if (stanTestLineUnobstructed(temp_f14,
                              &spC4,
                              temp_s0->unk8,
                              temp_s0->unk10,
                              arg1->unk0,
                              arg1->unk8,
                              CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER,
                              temp_f14,
                              phi_f16,
                              0.0f,
                              1.0f) != 0)
        {
            spC8 = (bitwise f32)arg1->unk0;
            spCC = arg1->unk4;
            spD0 = arg1->unk8;
        }
        else
        {
            spC4 = temp_s0->unk14;
            spC8 = (bitwise f32)temp_s0->unk8;
            spCC = temp_s0->unkC;
            spB0 = 1;
            spD0 = (bitwise f32)temp_s0->unk10;
        }
        bondviewUpdateGuardTankFlagsRelated(temp_s0, 1);
        phi_return =
            sub_GAME_7F05EB0C(arg0, &spC8, spC4, arg2, arg3, arg4, temp_s0);
        if ((arg0->unk64 & 0x80) != 0)
        {
            if (spB0 != 0)
            {
                arg0->unk6C->unk0  = (s32)(arg0->unk6C->unk0 | 0x100);
                arg0->unk6C->unkD4 = (bitwise f32)arg1->unk0;
                arg0->unk6C->unkD8 = (f32)arg1->unk4;
                arg0->unk6C->unkDC = (f32)arg1->unk8;
            }
            spA8       = get_cur_players_room();
            spA9       = (u8)0xFF;
            phi_return = sub_GAME_7F0B4AB4(get_BONDdata_position3(),
                                           &spC8,
                                           &spA8,
                                           arg0->unk6C + 0xCC,
                                           &sp54,
                                           &sp50,
                                           0x14);
        }
    }
    return phi_return;
}
#else
GLOBAL_ASM(
.text
glabel sub_GAME_7F05EC1C
/* 09374C 7F05EC1C 27BDFF28 */  addiu $sp, $sp, -0xd8
/* 093750 7F05EC20 AFBF003C */  sw    $ra, 0x3c($sp)
/* 093754 7F05EC24 AFB20038 */  sw    $s2, 0x38($sp)
/* 093758 7F05EC28 AFB10034 */  sw    $s1, 0x34($sp)
/* 09375C 7F05EC2C AFB00030 */  sw    $s0, 0x30($sp)
/* 093760 7F05EC30 AFA600E0 */  sw    $a2, 0xe0($sp)
/* 093764 7F05EC34 AFA700E4 */  sw    $a3, 0xe4($sp)
/* 093768 7F05EC38 8C820010 */  lw    $v0, 0x10($a0)
/* 09376C 7F05EC3C 00A08825 */  move  $s1, $a1
/* 093770 7F05EC40 00809025 */  move  $s2, $a0
/* 093774 7F05EC44 50400072 */  beql  $v0, $zero, .L7F05EE10
/* 093778 7F05EC48 8FBF003C */   lw    $ra, 0x3c($sp)
/* 09377C 7F05EC4C 0FC225E6 */  jal   get_curplayer_positiondata
/* 093780 7F05EC50 00000000 */   nop
/* 093784 7F05EC54 3C048008 */  lui   $a0, %hi(g_CurrentPlayer)
/* 093788 7F05EC58 00408025 */  move  $s0, $v0
/* 09378C 7F05EC5C 0FC225DE */  jal   bondviewGetPlayerStanHeight
/* 093790 7F05EC60 8C84A0B0 */   lw    $a0, %lo(g_CurrentPlayer)($a0)
/* 093794 7F05EC64 AFA000B0 */  sw    $zero, 0xb0($sp)
/* 093798 7F05EC68 C60C000C */  lwc1  $f12, 0xc($s0)
/* 09379C 7F05EC6C C6220004 */  lwc1  $f2, 4($s1)
/* 0937A0 7F05EC70 02002025 */  move  $a0, $s0
/* 0937A4 7F05EC74 00002825 */  move  $a1, $zero
/* 0937A8 7F05EC78 460C103C */  c.lt.s $f2, $f12
/* 0937AC 7F05EC7C 00000000 */  nop
/* 0937B0 7F05EC80 45020005 */  bc1fl .L7F05EC98
/* 0937B4 7F05EC84 46001381 */   sub.s $f14, $f2, $f0
/* 0937B8 7F05EC88 46006381 */  sub.s $f14, $f12, $f0
/* 0937BC 7F05EC8C 10000003 */  b     .L7F05EC9C
/* 0937C0 7F05EC90 46001401 */   sub.s $f16, $f2, $f0
/* 0937C4 7F05EC94 46001381 */  sub.s $f14, $f2, $f0
.L7F05EC98:
/* 0937C8 7F05EC98 46006401 */  sub.s $f16, $f12, $f0
.L7F05EC9C:
/* 0937CC 7F05EC9C 8E0E0014 */  lw    $t6, 0x14($s0)
/* 0937D0 7F05ECA0 E7B000B4 */  swc1  $f16, 0xb4($sp)
/* 0937D4 7F05ECA4 E7AE00B8 */  swc1  $f14, 0xb8($sp)
/* 0937D8 7F05ECA8 0FC2280F */  jal   bondviewUpdateGuardTankFlagsRelated
/* 0937DC 7F05ECAC AFAE00C4 */   sw    $t6, 0xc4($sp)
/* 0937E0 7F05ECB0 C6240008 */  lwc1  $f4, 8($s1)
/* 0937E4 7F05ECB4 8E050008 */  lw    $a1, 8($s0)
/* 0937E8 7F05ECB8 8E060010 */  lw    $a2, 0x10($s0)
/* 0937EC 7F05ECBC 8E270000 */  lw    $a3, ($s1)
/* 0937F0 7F05ECC0 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 0937F4 7F05ECC4 C7AE00B8 */  lwc1  $f14, 0xb8($sp)
/* 0937F8 7F05ECC8 C7B000B4 */  lwc1  $f16, 0xb4($sp)
/* 0937FC 7F05ECCC 44814000 */  mtc1  $at, $f8
/* 093800 7F05ECD0 44803000 */  mtc1  $zero, $f6
/* 093804 7F05ECD4 240F001F */  li    $t7, 31
/* 093808 7F05ECD8 AFAF0014 */  sw    $t7, 0x14($sp)
/* 09380C 7F05ECDC 27A400C4 */  addiu $a0, $sp, 0xc4
/* 093810 7F05ECE0 E7A40010 */  swc1  $f4, 0x10($sp)
/* 093814 7F05ECE4 E7AE0018 */  swc1  $f14, 0x18($sp)
/* 093818 7F05ECE8 E7B0001C */  swc1  $f16, 0x1c($sp)
/* 09381C 7F05ECEC E7A80024 */  swc1  $f8, 0x24($sp)
/* 093820 7F05ECF0 0FC2C389 */  jal   stanTestLineUnobstructed
/* 093824 7F05ECF4 E7A60020 */   swc1  $f6, 0x20($sp)
/* 093828 7F05ECF8 10400008 */  beqz  $v0, .L7F05ED1C
/* 09382C 7F05ECFC 02002025 */   move  $a0, $s0
/* 093830 7F05ED00 C62A0000 */  lwc1  $f10, ($s1)
/* 093834 7F05ED04 E7AA00C8 */  swc1  $f10, 0xc8($sp)
/* 093838 7F05ED08 C6320004 */  lwc1  $f18, 4($s1)
/* 09383C 7F05ED0C E7B200CC */  swc1  $f18, 0xcc($sp)
/* 093840 7F05ED10 C6240008 */  lwc1  $f4, 8($s1)
/* 093844 7F05ED14 1000000B */  b     .L7F05ED44
/* 093848 7F05ED18 E7A400D0 */   swc1  $f4, 0xd0($sp)
.L7F05ED1C:
/* 09384C 7F05ED1C 8E180014 */  lw    $t8, 0x14($s0)
/* 093850 7F05ED20 24190001 */  li    $t9, 1
/* 093854 7F05ED24 AFB800C4 */  sw    $t8, 0xc4($sp)
/* 093858 7F05ED28 C6060008 */  lwc1  $f6, 8($s0)
/* 09385C 7F05ED2C E7A600C8 */  swc1  $f6, 0xc8($sp)
/* 093860 7F05ED30 C608000C */  lwc1  $f8, 0xc($s0)
/* 093864 7F05ED34 E7A800CC */  swc1  $f8, 0xcc($sp)
/* 093868 7F05ED38 C60A0010 */  lwc1  $f10, 0x10($s0)
/* 09386C 7F05ED3C AFB900B0 */  sw    $t9, 0xb0($sp)
/* 093870 7F05ED40 E7AA00D0 */  swc1  $f10, 0xd0($sp)
.L7F05ED44:
/* 093874 7F05ED44 0FC2280F */  jal   bondviewUpdateGuardTankFlagsRelated
/* 093878 7F05ED48 24050001 */   li    $a1, 1
/* 09387C 7F05ED4C 8FA800E4 */  lw    $t0, 0xe4($sp)
/* 093880 7F05ED50 8FA900E8 */  lw    $t1, 0xe8($sp)
/* 093884 7F05ED54 02402025 */  move  $a0, $s2
/* 093888 7F05ED58 27A500C8 */  addiu $a1, $sp, 0xc8
/* 09388C 7F05ED5C 8FA600C4 */  lw    $a2, 0xc4($sp)
/* 093890 7F05ED60 8FA700E0 */  lw    $a3, 0xe0($sp)
/* 093894 7F05ED64 AFB00018 */  sw    $s0, 0x18($sp)
/* 093898 7F05ED68 AFA80010 */  sw    $t0, 0x10($sp)
/* 09389C 7F05ED6C 0FC17AC3 */  jal   sub_GAME_7F05EB0C
/* 0938A0 7F05ED70 AFA90014 */   sw    $t1, 0x14($sp)
/* 0938A4 7F05ED74 8E4A0064 */  lw    $t2, 0x64($s2)
/* 0938A8 7F05ED78 8FAC00B0 */  lw    $t4, 0xb0($sp)
/* 0938AC 7F05ED7C 314B0080 */  andi  $t3, $t2, 0x80
/* 0938B0 7F05ED80 51600023 */  beql  $t3, $zero, .L7F05EE10
/* 0938B4 7F05ED84 8FBF003C */   lw    $ra, 0x3c($sp)
/* 0938B8 7F05ED88 1180000E */  beqz  $t4, .L7F05EDC4
/* 0938BC 7F05ED8C 00000000 */   nop
/* 0938C0 7F05ED90 8E42006C */  lw    $v0, 0x6c($s2)
/* 0938C4 7F05ED94 8C4D0000 */  lw    $t5, ($v0)
/* 0938C8 7F05ED98 35AE0100 */  ori   $t6, $t5, 0x100
/* 0938CC 7F05ED9C AC4E0000 */  sw    $t6, ($v0)
/* 0938D0 7F05EDA0 8E4F006C */  lw    $t7, 0x6c($s2)
/* 0938D4 7F05EDA4 C6320000 */  lwc1  $f18, ($s1)
/* 0938D8 7F05EDA8 E5F200D4 */  swc1  $f18, 0xd4($t7)
/* 0938DC 7F05EDAC 8E58006C */  lw    $t8, 0x6c($s2)
/* 0938E0 7F05EDB0 C6240004 */  lwc1  $f4, 4($s1)
/* 0938E4 7F05EDB4 E70400D8 */  swc1  $f4, 0xd8($t8)
/* 0938E8 7F05EDB8 8E59006C */  lw    $t9, 0x6c($s2)
/* 0938EC 7F05EDBC C6260008 */  lwc1  $f6, 8($s1)
/* 0938F0 7F05EDC0 E72600DC */  swc1  $f6, 0xdc($t9)
.L7F05EDC4:
/* 0938F4 7F05EDC4 0FC227E6 */  jal   bondviewGetCurrentPlayersRoom
/* 0938F8 7F05EDC8 00000000 */   nop
/* 0938FC 7F05EDCC 240800FF */  li    $t0, 255
/* 093900 7F05EDD0 A3A200A8 */  sb    $v0, 0xa8($sp)
/* 093904 7F05EDD4 0FC22800 */  jal   bondviewGetCurrentPlayersPosition3
/* 093908 7F05EDD8 A3A800A9 */   sb    $t0, 0xa9($sp)
/* 09390C 7F05EDDC 8E47006C */  lw    $a3, 0x6c($s2)
/* 093910 7F05EDE0 27A90054 */  addiu $t1, $sp, 0x54
/* 093914 7F05EDE4 27AA0050 */  addiu $t2, $sp, 0x50
/* 093918 7F05EDE8 240B0014 */  li    $t3, 20
/* 09391C 7F05EDEC AFAB0018 */  sw    $t3, 0x18($sp)
/* 093920 7F05EDF0 AFAA0014 */  sw    $t2, 0x14($sp)
/* 093924 7F05EDF4 AFA90010 */  sw    $t1, 0x10($sp)
/* 093928 7F05EDF8 00402025 */  move  $a0, $v0
/* 09392C 7F05EDFC 27A500C8 */  addiu $a1, $sp, 0xc8
/* 093930 7F05EE00 27A600A8 */  addiu $a2, $sp, 0xa8
/* 093934 7F05EE04 0FC2D2AD */  jal   sub_GAME_7F0B4AB4
/* 093938 7F05EE08 24E700CC */   addiu $a3, $a3, 0xcc
/* 09393C 7F05EE0C 8FBF003C */  lw    $ra, 0x3c($sp)
.L7F05EE10:
/* 093940 7F05EE10 8FB00030 */  lw    $s0, 0x30($sp)
/* 093944 7F05EE14 8FB10034 */  lw    $s1, 0x34($sp)
/* 093948 7F05EE18 8FB20038 */  lw    $s2, 0x38($sp)
/* 09394C 7F05EE1C 03E00008 */  jr    $ra
/* 093950 7F05EE20 27BD00D8 */   addiu $sp, $sp, 0xd8
)
#endif




/**
 * Address 0x7F05EE24 (NTSC)
 * Address 0x7F05F2DC (PAL)
*/
void generate_player_thrown_grenade(s32 hand)
{
    s32 padding;
    Mtxf spFC;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    struct WeaponObjRecord *wor;
    s32 new_prop_type;
    s32 sp94; // sp148
    struct coord3d base_speed_vec; // sp136
    struct PropRecord* player_prop; // sp132
    struct coord3d *bondprevpos;  // sp128
    Mtxf sp40_f;
    ALSoundState *sfx_state;
    s32 current_weapon;
    s32 unused;

    wor = NULL;
    base_velocity = 16.666666f;

    player_prop = get_curplayer_positiondata();
    bondprevpos = get_BONDdata_field408();
    current_weapon = getCurrentPlayerWeaponId(hand);

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetMatrix10D4(), (f32*)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];

    matrix_4x4_set_identity(&spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);
    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

    wor = create_new_item_instance_of_model(PROP_CHRGRENADE, current_weapon);

    if (wor != NULL)
    {
        wor->timer = THROWN_ITEM_TIMER_DEFAULT - g_CurrentPlayer->last_z_trigger_timer;

        if (wor->timer < 0)
        {
            wor->timer = 0;
        }

        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
        wor->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

        sub_GAME_7F05EC1C(wor, &spE0, &spA0_a, &throw_speed_vec, &spFC);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_DEPOSIT) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.3f;
            wor->projectile->unk94 = 0.13333333f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GRENADE_THROW_SFX, NULL);

            if (sfx_state != NULL)
            {
                chrobjSndCreatePostEventDefault(sfx_state, (struct coord3d *) &wor->runtime_pos);
            }
        }
    }
}


/**
 * Address 0x7F05F09C (NTSC)
 * Address 0x7F05F554 (PAL)
*/
void generate_player_thrown_knife(s32 hand)
{
    struct WeaponObjRecord *wor;
    Mtxf spFC;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    s32 padding;
    s32 new_prop_type;
    s32 sp94;
    struct coord3d base_speed_vec;
    Mtxf sp40_f;
    struct PropRecord* player_prop;
    struct coord3d *bondprevpos;

    wor = NULL;
    base_velocity = 25.0f;

    player_prop = get_curplayer_positiondata();
    bondprevpos = get_BONDdata_field408();

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetMatrix10D4(), (f32*)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];

    matrix_4x4_set_rotation_around_z(4.712389f, &spA0_a);
    matrix_4x4_set_rotation_around_x(M_PI_F, &sp40_f);
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);

    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

    guRotateF(&spFC, 360.0f / ((randomGetNext() * (0.5f / (f32)INT_MAX)) + 12.1f), spA0_a.m[1][0], spA0_a.m[1][1], spA0_a.m[1][2]);

    wor = create_new_item_instance_of_model(PROP_CHRKNIFE, ITEM_THROWKNIFE);

    if (wor != NULL)
    {
        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
        wor->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

        sub_GAME_7F05EC1C(wor, &spE0, &spA0_a, &throw_speed_vec, &spFC);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_DEPOSIT) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.1f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            wor->runtime_bitflags |= RUNTIMEBITFLAG_THROWING_KNIFE_RELATED;
        }

        sub_GAME_7F043650(wor);
    }
}





/**
 * Address 0x7F05F358 (NTSC)
 * Address 0x7F05F810 (PAL)
*/
void generate_player_thrown_object(s32 hand)
{
/*
    else {
        assertPrint_8291E690(".\\ported\\gun.cpp",0x8df,"throwmineremote - Not a mine!");
    }
*/

    s32 padding;
    Mtxf unk_mtxf;
    struct coord3d throw_speed_vec;
    f32 base_velocity;
    struct coord3d spE0;
    Mtxf spA0_a;
    struct WeaponObjRecord *wor;
    s32 new_prop_type;
    s32 sp94; // sp148
    struct coord3d base_speed_vec; // sp136
    struct PropRecord* player_prop; // sp132
    struct coord3d *bondprevpos;  // sp128
    Mtxf sp40_f;
    ALSoundState *sfx_state;
    s32 current_weapon;
    s32 unused;

    wor = NULL;
    base_velocity = 16.666666f;

    player_prop = get_curplayer_positiondata();
    bondprevpos = get_BONDdata_field408();
    current_weapon = getCurrentPlayerWeaponId(hand);

    if (current_weapon == ITEM_GOLDENEYEKEY)
    {
        base_velocity = 6.6666665f;
    }

    sub_GAME_7F057C14(&throw_speed_vec, &unk_mtxf);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetMatrix10D4(), (f32*)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity) + 5.0f;
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    spE0.f[0] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][0];
    spE0.f[1] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][1];
    spE0.f[2] = g_CurrentPlayer->hands[hand].throw_item_pos_related.m[3][2];

    matrix_4x4_set_identity(&spA0_a);
    matrix_4x4_copy(&g_CurrentPlayer->hands[hand].throw_item_pos_related, &sp40_f);
    sp40_f.m[3][0] = 0.0f;
    sp40_f.m[3][1] = 0.0f;
    sp40_f.m[3][2] = 0.0f;
    matrix_4x4_multiply_in_place(&sp40_f, &spA0_a);

    if (current_weapon == ITEM_GOLDENEYEKEY)
    {
        wor = bondinvRemovePropWeaponByID(current_weapon);
        bondinvRemoveItemByID(current_weapon);

        if (wor != NULL)
        {
            objDetach(wor->prop);
        }

        sub_GAME_7F05D690();
    }

    if (wor == NULL)
    {
        new_prop_type = PROP_CHRREMOTEMINE;

        switch (current_weapon)
        {
        case ITEM_PROXIMITYMINE:
            new_prop_type = PROP_CHRPROXIMITYMINE;
            break;
        case ITEM_TIMEDMINE:
            new_prop_type = PROP_CHRTIMEDMINE;
            break;
        case ITEM_BOMBCASE:
            new_prop_type = PROP_CHRBOMBCASE;
            break;
        case ITEM_BUG:
            new_prop_type = PROP_CHRBUG;
            break;
        case ITEM_MICROCAMERA:
            new_prop_type = PROP_CHRMICROCAMERA;
            break;
        case ITEM_GOLDENEYEKEY:
            new_prop_type = PROP_CHRGOLDENEYEKEY;
            break;
        case ITEM_PLASTIQUE:
            new_prop_type = PROP_CHRPLASTIQUE;
            break;
#ifdef DEBUG
        default:
            assertmsg2(current_weapon = PROP_CHRREMOTEMINE, "throwmineremote - Not a mine!");
#endif

        }

        wor = create_new_item_instance_of_model(new_prop_type, current_weapon);
    }

    if (wor != NULL)
    {
        switch (current_weapon)
        {
            case ITEM_REMOTEMINE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_PROXIMITYMINE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_TIMEDMINE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_BOMBCASE:
            if (getPlayerCount() == 1)
            {
                wor->timer = THROWN_ITEM_TIMER_SOLO;
            }
            else
            {
                wor->timer = THROWN_ITEM_TIMER_MULTI;
            }
            break;

            case ITEM_PLASTIQUE:
            case ITEM_BUG:
            case ITEM_MICROCAMERA:
            case ITEM_GOLDENEYEKEY:
                wor->timer = 1;
            break;

            default:
                wor->timer = THROWN_ITEM_TIMER_DEFAULT;
            break;
        }

        wor->runtime_bitflags &= ~(RUNTIMEBITFLAG_OWNER);
        wor->runtime_bitflags |= get_cur_playernum() << RUNTIMEBITSHIFT_OWNER;

        sub_GAME_7F05EC1C(wor, &spE0, &spA0_a, &throw_speed_vec, &unk_mtxf);

        if ((wor->runtime_bitflags & RUNTIMEBITFLAG_DEPOSIT) != 0)
        {
            wor->projectile->flags = (s32) (wor->projectile->flags | 2);

            wor->projectile->unk8C = 0.1f;
            wor->projectile->refreshrate = THROWN_ITEM_REFRESH_RATE;

            sfx_state = sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GRENADE_THROW_SFX, NULL);

            if (sfx_state != NULL)
            {
                chrobjSndCreatePostEventDefault(sfx_state, (struct coord3d *) &wor->runtime_pos);
            }
        }
    }
}




#ifdef NONMATCHING
void sub_GAME_7F05F73C(void) {
    struct WeaponObjRecord *wor;
    f32                     base_velocity;
    base_velocity = 33.333332f;
    wor           = NULL;

    player_prop = get_curplayer_positiondata();
    bondprevpos = get_BONDdata_field408();

    sub_GAME_7F057C14(&throw_speed_vec, &spFC);
    bullet_path_from_screen_center(&sp94, &base_speed_vec, hand);
    mtx4RotateVecInPlace(currentPlayerGetMatrix10D4(), (f32 *)&base_speed_vec);

    throw_speed_vec.f[0] = (base_speed_vec.f[0] * base_velocity);
    throw_speed_vec.f[1] = (base_speed_vec.f[1] * base_velocity);
    throw_speed_vec.f[2] = (base_speed_vec.f[2] * base_velocity);

    if (g_ClockTimer > 0)
    {
        throw_speed_vec.f[0] = ((player_prop->pos.f[0] - bondprevpos->f[0]) / g_GlobalTimerDelta) + throw_speed_vec.f[0];
        throw_speed_vec.f[1] = ((player_prop->pos.f[1] - bondprevpos->f[1]) / g_GlobalTimerDelta) + throw_speed_vec.f[1];
        throw_speed_vec.f[2] = ((player_prop->pos.f[2] - bondprevpos->f[2]) / g_GlobalTimerDelta) + throw_speed_vec.f[2];
    }

    matrix_4x4_copy(g_CurrentPlayer + sp28 + 0xAD8, &sp50);
    sp80 = 0.0f;
    sp84 = 0.0f;
    sp88 = 0.0f;
    wor  = create_new_item_instance_of_model(PROP_CHRREMOTEMINE, ITEM_REMOTEMINE);
    if (wor != NULL)
    {
        wor->timer = 0x4B0;
        wor->base.runtime_bitflags &= 0xFFF9FFFF;
        spE4 = wor;
        spE4->base.runtime_bitflags |= get_cur_playernum() << 0x11;
        sub_GAME_7F05EC1C(spE4, spE0 + 0x2E8, &sp50, &sp94, &spA0);
        if (spE4->base.runtime_bitflags & 0x80)
        {
            spE4->base.unk6C->unk8c       = 0.3f;
            spE4->base.unk6C->unk94       = 0.13333333f;
            spE4->base.unk6C->refreshrate = 0x3C;
        }
    }
}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.late_rodata
glabel D_80053DCC
.word 0x42055555 /*33.333332*/
glabel D_80053DD0
.word 0x3e99999a /*0.30000001*/
glabel D_80053DD4
.word 0x3e088888 /*0.13333333*/
.text
glabel sub_GAME_7F05F73C
/* 09426C 7F05F73C 000470C0 */  sll   $t6, $a0, 3
/* 094270 7F05F740 01C47023 */  subu  $t6, $t6, $a0
/* 094274 7F05F744 000E7080 */  sll   $t6, $t6, 2
/* 094278 7F05F748 01C47021 */  addu  $t6, $t6, $a0
/* 09427C 7F05F74C 3C0F8008 */  lui   $t7, %hi(g_CurrentPlayer)
/* 094280 7F05F750 8DEFA0B0 */  lw    $t7, %lo(g_CurrentPlayer)($t7)
/* 094284 7F05F754 000E7080 */  sll   $t6, $t6, 2
/* 094288 7F05F758 01C47021 */  addu  $t6, $t6, $a0
/* 09428C 7F05F75C 27BDFF18 */  addiu $sp, $sp, -0xe8
/* 094290 7F05F760 000E70C0 */  sll   $t6, $t6, 3
/* 094294 7F05F764 01EEC021 */  addu  $t8, $t7, $t6
/* 094298 7F05F768 AFBF001C */  sw    $ra, 0x1c($sp)
/* 09429C 7F05F76C 27190870 */  addiu $t9, $t8, 0x870
/* 0942A0 7F05F770 AFA400E8 */  sw    $a0, 0xe8($sp)
/* 0942A4 7F05F774 AFB900E0 */  sw    $t9, 0xe0($sp)
/* 0942A8 7F05F778 0FC225E6 */  jal   get_curplayer_positiondata
/* 0942AC 7F05F77C AFAE0028 */   sw    $t6, 0x28($sp)
/* 0942B0 7F05F780 0FC2280B */  jal   get_BONDdata_field408
/* 0942B4 7F05F784 AFA20034 */   sw    $v0, 0x34($sp)
/* 0942B8 7F05F788 AFA20030 */  sw    $v0, 0x30($sp)
/* 0942BC 7F05F78C 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 0942C0 7F05F790 27A400A0 */   addiu $a0, $sp, 0xa0
/* 0942C4 7F05F794 27A40044 */  addiu $a0, $sp, 0x44
/* 0942C8 7F05F798 27A50038 */  addiu $a1, $sp, 0x38
/* 0942CC 7F05F79C 0FC1A073 */  jal   bullet_path_from_screen_center
/* 0942D0 7F05F7A0 8FA600E8 */   lw    $a2, 0xe8($sp)
/* 0942D4 7F05F7A4 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 0942D8 7F05F7A8 00000000 */   nop
/* 0942DC 7F05F7AC 00402025 */  move  $a0, $v0
/* 0942E0 7F05F7B0 0FC160F6 */  jal   mtx4RotateVecInPlace
/* 0942E4 7F05F7B4 27A50038 */   addiu $a1, $sp, 0x38
/* 0942E8 7F05F7B8 3C018005 */  lui   $at, %hi(D_80053DCC)
/* 0942EC 7F05F7BC C4203DCC */  lwc1  $f0, %lo(D_80053DCC)($at)
/* 0942F0 7F05F7C0 C7A40038 */  lwc1  $f4, 0x38($sp)
/* 0942F4 7F05F7C4 C7A8003C */  lwc1  $f8, 0x3c($sp)
/* 0942F8 7F05F7C8 C7B00040 */  lwc1  $f16, 0x40($sp)
/* 0942FC 7F05F7CC 46002182 */  mul.s $f6, $f4, $f0
/* 094300 7F05F7D0 3C088005 */  lui   $t0, %hi(g_ClockTimer)
/* 094304 7F05F7D4 8D088374 */  lw    $t0, %lo(g_ClockTimer)($t0)
/* 094308 7F05F7D8 46004282 */  mul.s $f10, $f8, $f0
/* 09430C 7F05F7DC 8FA20030 */  lw    $v0, 0x30($sp)
/* 094310 7F05F7E0 8FA30034 */  lw    $v1, 0x34($sp)
/* 094314 7F05F7E4 46008482 */  mul.s $f18, $f16, $f0
/* 094318 7F05F7E8 E7A60094 */  swc1  $f6, 0x94($sp)
/* 09431C 7F05F7EC 3C098008 */  lui   $t1, %hi(g_CurrentPlayer)
/* 094320 7F05F7F0 E7AA0098 */  swc1  $f10, 0x98($sp)
/* 094324 7F05F7F4 19000015 */  blez  $t0, .L7F05F84C
/* 094328 7F05F7F8 E7B2009C */   swc1  $f18, 0x9c($sp)
/* 09432C 7F05F7FC C4640008 */  lwc1  $f4, 8($v1)
/* 094330 7F05F800 C4480000 */  lwc1  $f8, ($v0)
/* 094334 7F05F804 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta)
/* 094338 7F05F808 C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 09433C 7F05F80C 46082401 */  sub.s $f16, $f4, $f8
/* 094340 7F05F810 46008103 */  div.s $f4, $f16, $f0
/* 094344 7F05F814 46043200 */  add.s $f8, $f6, $f4
/* 094348 7F05F818 E7A80094 */  swc1  $f8, 0x94($sp)
/* 09434C 7F05F81C C4460004 */  lwc1  $f6, 4($v0)
/* 094350 7F05F820 C470000C */  lwc1  $f16, 0xc($v1)
/* 094354 7F05F824 46068101 */  sub.s $f4, $f16, $f6
/* 094358 7F05F828 46002203 */  div.s $f8, $f4, $f0
/* 09435C 7F05F82C 46085400 */  add.s $f16, $f10, $f8
/* 094360 7F05F830 E7B00098 */  swc1  $f16, 0x98($sp)
/* 094364 7F05F834 C4440008 */  lwc1  $f4, 8($v0)
/* 094368 7F05F838 C4660010 */  lwc1  $f6, 0x10($v1)
/* 09436C 7F05F83C 46043281 */  sub.s $f10, $f6, $f4
/* 094370 7F05F840 46005203 */  div.s $f8, $f10, $f0
/* 094374 7F05F844 46089400 */  add.s $f16, $f18, $f8
/* 094378 7F05F848 E7B0009C */  swc1  $f16, 0x9c($sp)
.L7F05F84C:
/* 09437C 7F05F84C 8D29A0B0 */  lw    $t1, %lo(g_CurrentPlayer)($t1)
/* 094380 7F05F850 8FAA0028 */  lw    $t2, 0x28($sp)
/* 094384 7F05F854 27A50050 */  addiu $a1, $sp, 0x50
/* 094388 7F05F858 012A2021 */  addu  $a0, $t1, $t2
/* 09438C 7F05F85C 0FC16008 */  jal   matrix_4x4_copy
/* 094390 7F05F860 24840AD8 */   addiu $a0, $a0, 0xad8
/* 094394 7F05F864 44800000 */  mtc1  $zero, $f0
/* 094398 7F05F868 240400CB */  li    $a0, 203
/* 09439C 7F05F86C 24050057 */  li    $a1, 87
/* 0943A0 7F05F870 E7A00080 */  swc1  $f0, 0x80($sp)
/* 0943A4 7F05F874 E7A00084 */  swc1  $f0, 0x84($sp)
/* 0943A8 7F05F878 0FC1481B */  jal   create_new_item_instance_of_model
/* 0943AC 7F05F87C E7A00088 */   swc1  $f0, 0x88($sp)
/* 0943B0 7F05F880 10400025 */  beqz  $v0, .L7F05F918
/* 0943B4 7F05F884 240B04B0 */   li    $t3, 1200
/* 0943B8 7F05F888 8C4C0064 */  lw    $t4, 0x64($v0)
/* 0943BC 7F05F88C 3C01FFF9 */  lui   $at, (0xFFF9FFFF >> 16) # lui $at, 0xfff9
/* 0943C0 7F05F890 3421FFFF */  ori   $at, (0xFFF9FFFF & 0xFFFF) # ori $at, $at, 0xffff
/* 0943C4 7F05F894 01816824 */  and   $t5, $t4, $at
/* 0943C8 7F05F898 A44B0082 */  sh    $t3, 0x82($v0)
/* 0943CC 7F05F89C AC4D0064 */  sw    $t5, 0x64($v0)
/* 0943D0 7F05F8A0 0FC26C54 */  jal   get_cur_playernum
/* 0943D4 7F05F8A4 AFA200E4 */   sw    $v0, 0xe4($sp)
/* 0943D8 7F05F8A8 8FA400E4 */  lw    $a0, 0xe4($sp)
/* 0943DC 7F05F8AC 00027C40 */  sll   $t7, $v0, 0x11
/* 0943E0 7F05F8B0 27B900A0 */  addiu $t9, $sp, 0xa0
/* 0943E4 7F05F8B4 8C8E0064 */  lw    $t6, 0x64($a0)
/* 0943E8 7F05F8B8 27A60050 */  addiu $a2, $sp, 0x50
/* 0943EC 7F05F8BC 27A70094 */  addiu $a3, $sp, 0x94
/* 0943F0 7F05F8C0 01CFC025 */  or    $t8, $t6, $t7
/* 0943F4 7F05F8C4 AC980064 */  sw    $t8, 0x64($a0)
/* 0943F8 7F05F8C8 8FA500E0 */  lw    $a1, 0xe0($sp)
/* 0943FC 7F05F8CC AFB90010 */  sw    $t9, 0x10($sp)
/* 094400 7F05F8D0 0FC17B07 */  jal   sub_GAME_7F05EC1C
/* 094404 7F05F8D4 24A502E8 */   addiu $a1, $a1, 0x2e8
/* 094408 7F05F8D8 8FA400E4 */  lw    $a0, 0xe4($sp)
/* 09440C 7F05F8DC 3C018005 */  lui   $at, %hi(D_80053DD0)
/* 094410 7F05F8E0 8C880064 */  lw    $t0, 0x64($a0)
/* 094414 7F05F8E4 31090080 */  andi  $t1, $t0, 0x80
/* 094418 7F05F8E8 5120000C */  beql  $t1, $zero, .L7F05F91C
/* 09441C 7F05F8EC 8FBF001C */   lw    $ra, 0x1c($sp)
/* 094420 7F05F8F0 C4263DD0 */  lwc1  $f6, %lo(D_80053DD0)($at)
/* 094424 7F05F8F4 8C8A006C */  lw    $t2, 0x6c($a0)
/* 094428 7F05F8F8 3C018005 */  lui   $at, %hi(D_80053DD4)
/* 09442C 7F05F8FC 240C003C */  li    $t4, 60
/* 094430 7F05F900 E546008C */  swc1  $f6, 0x8c($t2)
/* 094434 7F05F904 8C8B006C */  lw    $t3, 0x6c($a0)
/* 094438 7F05F908 C4243DD4 */  lwc1  $f4, %lo(D_80053DD4)($at)
/* 09443C 7F05F90C E5640094 */  swc1  $f4, 0x94($t3)
/* 094440 7F05F910 8C8D006C */  lw    $t5, 0x6c($a0)
/* 094444 7F05F914 ADAC00BC */  sw    $t4, 0xbc($t5)
.L7F05F918:
/* 094448 7F05F918 8FBF001C */  lw    $ra, 0x1c($sp)
.L7F05F91C:
/* 09444C 7F05F91C 27BD00E8 */  addiu $sp, $sp, 0xe8
/* 094450 7F05F920 03E00008 */  jr    $ra
/* 094454 7F05F924 00000000 */   nop
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.late_rodata
glabel D_80053DCC
.word 0x42055555 /*33.333332*/
glabel D_80053DD0
.word 0x3e99999a /*0.30000001*/
glabel D_80053DD4
.word 0x3e088888 /*0.13333333*/
.text
glabel sub_GAME_7F05F73C
/* 0925E4 7F05FBF4 000470C0 */  sll   $t6, $a0, 3
/* 0925E8 7F05FBF8 01C47023 */  subu  $t6, $t6, $a0
/* 0925EC 7F05FBFC 000E7080 */  sll   $t6, $t6, 2
/* 0925F0 7F05FC00 01C47021 */  addu  $t6, $t6, $a0
/* 0925F4 7F05FC04 3C0F8007 */  lui   $t7, %hi(g_CurrentPlayer) # $t7, 0x8007
/* 0925F8 7F05FC08 8DEF8BC0 */  lw    $t7, %lo(g_CurrentPlayer)($t7)
/* 0925FC 7F05FC0C 000E7080 */  sll   $t6, $t6, 2
/* 092600 7F05FC10 01C47021 */  addu  $t6, $t6, $a0
/* 092604 7F05FC14 27BDFF18 */  addiu $sp, $sp, -0xe8
/* 092608 7F05FC18 000E70C0 */  sll   $t6, $t6, 3
/* 09260C 7F05FC1C 01EEC021 */  addu  $t8, $t7, $t6
/* 092610 7F05FC20 AFBF001C */  sw    $ra, 0x1c($sp)
/* 092614 7F05FC24 27190868 */  addiu $t9, $t8, 0x868
/* 092618 7F05FC28 AFA400E8 */  sw    $a0, 0xe8($sp)
/* 09261C 7F05FC2C AFB900E0 */  sw    $t9, 0xe0($sp)
/* 092620 7F05FC30 0FC22640 */  jal   get_curplayer_positiondata
/* 092624 7F05FC34 AFAE0028 */   sw    $t6, 0x28($sp)
/* 092628 7F05FC38 0FC2287E */  jal   get_BONDdata_field408
/* 09262C 7F05FC3C AFA20034 */   sw    $v0, 0x34($sp)
/* 092630 7F05FC40 AFA20030 */  sw    $v0, 0x30($sp)
/* 092634 7F05FC44 0FC1611E */  jal   matrix_4x4_set_identity
/* 092638 7F05FC48 27A400A0 */   addiu $a0, $sp, 0xa0
/* 09263C 7F05FC4C 27A40044 */  addiu $a0, $sp, 0x44
/* 092640 7F05FC50 27A50038 */  addiu $a1, $sp, 0x38
/* 092644 7F05FC54 0FC1A25D */  jal   bullet_path_from_screen_center
/* 092648 7F05FC58 8FA600E8 */   lw    $a2, 0xe8($sp)
/* 09264C 7F05FC5C 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 092650 7F05FC60 00000000 */   nop
/* 092654 7F05FC64 00402025 */  move  $a0, $v0
/* 092658 7F05FC68 0FC16220 */  jal   mtx4RotateVecInPlace
/* 09265C 7F05FC6C 27A50038 */   addiu $a1, $sp, 0x38
/* 092660 7F05FC70 3C018005 */  lui   $at, %hi(D_80053DCC) # $at, 0x8005
/* 092664 7F05FC74 C4209F0C */  lwc1  $f0, %lo(D_80053DCC)($at)
/* 092668 7F05FC78 C7A40038 */  lwc1  $f4, 0x38($sp)
/* 09266C 7F05FC7C C7A8003C */  lwc1  $f8, 0x3c($sp)
/* 092670 7F05FC80 C7B00040 */  lwc1  $f16, 0x40($sp)
/* 092674 7F05FC84 46002182 */  mul.s $f6, $f4, $f0
/* 092678 7F05FC88 3C088004 */  lui   $t0, %hi(g_ClockTimer) # $t0, 0x8004
/* 09267C 7F05FC8C 8D080FF4 */  lw    $t0, %lo(g_ClockTimer)($t0)
/* 092680 7F05FC90 46004282 */  mul.s $f10, $f8, $f0
/* 092684 7F05FC94 8FA20030 */  lw    $v0, 0x30($sp)
/* 092688 7F05FC98 8FA30034 */  lw    $v1, 0x34($sp)
/* 09268C 7F05FC9C 46008482 */  mul.s $f18, $f16, $f0
/* 092690 7F05FCA0 E7A60094 */  swc1  $f6, 0x94($sp)
/* 092694 7F05FCA4 3C098007 */  lui   $t1, %hi(g_CurrentPlayer) # $t1, 0x8007
/* 092698 7F05FCA8 E7AA0098 */  swc1  $f10, 0x98($sp)
/* 09269C 7F05FCAC 19000015 */  blez  $t0, .L7F05FD04
/* 0926A0 7F05FCB0 E7B2009C */   swc1  $f18, 0x9c($sp)
/* 0926A4 7F05FCB4 C4640008 */  lwc1  $f4, 8($v1)
/* 0926A8 7F05FCB8 C4480000 */  lwc1  $f8, ($v0)
/* 0926AC 7F05FCBC 3C018004 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8004
/* 0926B0 7F05FCC0 C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 0926B4 7F05FCC4 46082401 */  sub.s $f16, $f4, $f8
/* 0926B8 7F05FCC8 46008103 */  div.s $f4, $f16, $f0
/* 0926BC 7F05FCCC 46043200 */  add.s $f8, $f6, $f4
/* 0926C0 7F05FCD0 E7A80094 */  swc1  $f8, 0x94($sp)
/* 0926C4 7F05FCD4 C4460004 */  lwc1  $f6, 4($v0)
/* 0926C8 7F05FCD8 C470000C */  lwc1  $f16, 0xc($v1)
/* 0926CC 7F05FCDC 46068101 */  sub.s $f4, $f16, $f6
/* 0926D0 7F05FCE0 46002203 */  div.s $f8, $f4, $f0
/* 0926D4 7F05FCE4 46085400 */  add.s $f16, $f10, $f8
/* 0926D8 7F05FCE8 E7B00098 */  swc1  $f16, 0x98($sp)
/* 0926DC 7F05FCEC C4440008 */  lwc1  $f4, 8($v0)
/* 0926E0 7F05FCF0 C4660010 */  lwc1  $f6, 0x10($v1)
/* 0926E4 7F05FCF4 46043281 */  sub.s $f10, $f6, $f4
/* 0926E8 7F05FCF8 46005203 */  div.s $f8, $f10, $f0
/* 0926EC 7F05FCFC 46089400 */  add.s $f16, $f18, $f8
/* 0926F0 7F05FD00 E7B0009C */  swc1  $f16, 0x9c($sp)
.L7F05FD04:
/* 0926F4 7F05FD04 8D298BC0 */  lw    $t1, %lo(g_CurrentPlayer)($t1)
/* 0926F8 7F05FD08 8FAA0028 */  lw    $t2, 0x28($sp)
/* 0926FC 7F05FD0C 27A50050 */  addiu $a1, $sp, 0x50
/* 092700 7F05FD10 012A2021 */  addu  $a0, $t1, $t2
/* 092704 7F05FD14 0FC16132 */  jal   matrix_4x4_copy
/* 092708 7F05FD18 24840AD0 */   addiu $a0, $a0, 0xad0
/* 09270C 7F05FD1C 44800000 */  mtc1  $zero, $f0
/* 092710 7F05FD20 240400CB */  li    $a0, 203
/* 092714 7F05FD24 24050057 */  li    $a1, 87
/* 092718 7F05FD28 E7A00080 */  swc1  $f0, 0x80($sp)
/* 09271C 7F05FD2C E7A00084 */  swc1  $f0, 0x84($sp)
/* 092720 7F05FD30 0FC148D3 */  jal   create_new_item_instance_of_model
/* 092724 7F05FD34 E7A00088 */   swc1  $f0, 0x88($sp)
/* 092728 7F05FD38 10400025 */  beqz  $v0, .L7F05FDD0
/* 09272C 7F05FD3C 240B03E8 */   li    $t3, 1000
/* 092730 7F05FD40 8C4C0064 */  lw    $t4, 0x64($v0)
/* 092734 7F05FD44 3C01FFF9 */  lui   $at, (0xFFF9FFFF >> 16) # lui $at, 0xfff9
/* 092738 7F05FD48 3421FFFF */  ori   $at, (0xFFF9FFFF & 0xFFFF) # ori $at, $at, 0xffff
/* 09273C 7F05FD4C 01816824 */  and   $t5, $t4, $at
/* 092740 7F05FD50 A44B0082 */  sh    $t3, 0x82($v0)
/* 092744 7F05FD54 AC4D0064 */  sw    $t5, 0x64($v0)
/* 092748 7F05FD58 0FC269A4 */  jal   get_cur_playernum
/* 09274C 7F05FD5C AFA200E4 */   sw    $v0, 0xe4($sp)
/* 092750 7F05FD60 8FA400E4 */  lw    $a0, 0xe4($sp)
/* 092754 7F05FD64 00027C40 */  sll   $t7, $v0, 0x11
/* 092758 7F05FD68 27B900A0 */  addiu $t9, $sp, 0xa0
/* 09275C 7F05FD6C 8C8E0064 */  lw    $t6, 0x64($a0)
/* 092760 7F05FD70 27A60050 */  addiu $a2, $sp, 0x50
/* 092764 7F05FD74 27A70094 */  addiu $a3, $sp, 0x94
/* 092768 7F05FD78 01CFC025 */  or    $t8, $t6, $t7
/* 09276C 7F05FD7C AC980064 */  sw    $t8, 0x64($a0)
/* 092770 7F05FD80 8FA500E0 */  lw    $a1, 0xe0($sp)
/* 092774 7F05FD84 AFB90010 */  sw    $t9, 0x10($sp)
/* 092778 7F05FD88 0FC17C35 */  jal   sub_GAME_7F05EC1C
/* 09277C 7F05FD8C 24A502E8 */   addiu $a1, $a1, 0x2e8
/* 092780 7F05FD90 8FA400E4 */  lw    $a0, 0xe4($sp)
/* 092784 7F05FD94 3C018005 */  lui   $at, %hi(D_80053DD0) # $at, 0x8005
/* 092788 7F05FD98 8C880064 */  lw    $t0, 0x64($a0)
/* 09278C 7F05FD9C 31090080 */  andi  $t1, $t0, 0x80
/* 092790 7F05FDA0 5120000C */  beql  $t1, $zero, .L7F05FDD4
/* 092794 7F05FDA4 8FBF001C */   lw    $ra, 0x1c($sp)
/* 092798 7F05FDA8 C4269F10 */  lwc1  $f6, %lo(D_80053DD0)($at)
/* 09279C 7F05FDAC 8C8A006C */  lw    $t2, 0x6c($a0)
/* 0927A0 7F05FDB0 3C018005 */  lui   $at, %hi(D_80053DD4) # $at, 0x8005
/* 0927A4 7F05FDB4 240C0032 */  li    $t4, 50
/* 0927A8 7F05FDB8 E546008C */  swc1  $f6, 0x8c($t2)
/* 0927AC 7F05FDBC 8C8B006C */  lw    $t3, 0x6c($a0)
/* 0927B0 7F05FDC0 C4249F14 */  lwc1  $f4, %lo(D_80053DD4)($at)
/* 0927B4 7F05FDC4 E5640094 */  swc1  $f4, 0x94($t3)
/* 0927B8 7F05FDC8 8C8D006C */  lw    $t5, 0x6c($a0)
/* 0927BC 7F05FDCC ADAC00BC */  sw    $t4, 0xbc($t5)
.L7F05FDD0:
/* 0927C0 7F05FDD0 8FBF001C */  lw    $ra, 0x1c($sp)
.L7F05FDD4:
/* 0927C4 7F05FDD4 27BD00E8 */  addiu $sp, $sp, 0xe8
/* 0927C8 7F05FDD8 03E00008 */  jr    $ra
/* 0927CC 7F05FDDC 00000000 */   nop
)
#endif
#endif





#ifdef NONMATCHING
void sub_GAME_7F05F928(void) {
    void       *sp7C;
    Mtxf        sp34;
    PropRecord *sp30;
    void       *temp_s0;
    void       *temp_s1;
    void       *temp_s3;
    void       *temp_v0;

    temp_v0 = g_CurrentPlayer + (arg0 * 0x3A8);
    temp_s0 = temp_v0->unkA90;
    if (temp_s0 != NULL)
    {
        temp_s3 = temp_s0->unk10;
        if (temp_s3 != NULL)
        {
            sp7C    = temp_v0 + 0x870;
            sp30    = get_curplayer_positiondata();
            temp_s1 = temp_s0->unk14;
            matrix_4x4_copy(sp7C + 0x268, &sp34);
            sp34.m[3][0] = 0.0f;
            sp34.m[3][1] = 0.0f;
            sp34.m[3][2] = 0.0f;
            matrix_scalar_multiply(temp_s0->unk14->unk14, &sp34);
            objChangeShading(temp_s0, sp7C + 0x2E8, &sp34, sp30->stan);
            chrobjCollisionRelated(temp_s0);
            temp_s1->unkC = dynAllocate(temp_s1->unk8->unkE << 6);
            matrix_4x4_copy(temp_s0 + 0x18, &sp34);
            matrix_4x4_set_position(temp_s0 + 0x58, &sp34);
            matrix_4x4_multiply_homogeneous(camGetWorldToScreenMtxf(), &sp34, temp_s1->unkC);
            modelUpdateRelationsQuick(temp_s1, temp_s1->unk8->unk0);
            temp_s3->unk1  = temp_s3->unk1 | 2;
            temp_s3->unk18 = -temp_s1->unkC->unk38;
        }
    }
}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.text
glabel sub_GAME_7F05F928
/* 094458 7F05F928 000478C0 */  sll   $t7, $a0, 3
/* 09445C 7F05F92C 01E47823 */  subu  $t7, $t7, $a0
/* 094460 7F05F930 27BDFF80 */  addiu $sp, $sp, -0x80
/* 094464 7F05F934 000F7880 */  sll   $t7, $t7, 2
/* 094468 7F05F938 01E47821 */  addu  $t7, $t7, $a0
/* 09446C 7F05F93C 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer)
/* 094470 7F05F940 8DCEA0B0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 094474 7F05F944 000F7880 */  sll   $t7, $t7, 2
/* 094478 7F05F948 01E47821 */  addu  $t7, $t7, $a0
/* 09447C 7F05F94C 000F78C0 */  sll   $t7, $t7, 3
/* 094480 7F05F950 AFBF0024 */  sw    $ra, 0x24($sp)
/* 094484 7F05F954 AFB30020 */  sw    $s3, 0x20($sp)
/* 094488 7F05F958 AFB2001C */  sw    $s2, 0x1c($sp)
/* 09448C 7F05F95C AFB10018 */  sw    $s1, 0x18($sp)
/* 094490 7F05F960 AFB00014 */  sw    $s0, 0x14($sp)
/* 094494 7F05F964 01CF1021 */  addu  $v0, $t6, $t7
/* 094498 7F05F968 8C500A90 */  lw    $s0, 0xa90($v0)
/* 09449C 7F05F96C 24420870 */  addiu $v0, $v0, 0x870
/* 0944A0 7F05F970 5200003C */  beql  $s0, $zero, .L7F05FA64
/* 0944A4 7F05F974 8FBF0024 */   lw    $ra, 0x24($sp)
/* 0944A8 7F05F978 8E130010 */  lw    $s3, 0x10($s0)
/* 0944AC 7F05F97C 52600039 */  beql  $s3, $zero, .L7F05FA64
/* 0944B0 7F05F980 8FBF0024 */   lw    $ra, 0x24($sp)
/* 0944B4 7F05F984 0FC225E6 */  jal   get_curplayer_positiondata
/* 0944B8 7F05F988 AFA2007C */   sw    $v0, 0x7c($sp)
/* 0944BC 7F05F98C 8FA4007C */  lw    $a0, 0x7c($sp)
/* 0944C0 7F05F990 27B20034 */  addiu $s2, $sp, 0x34
/* 0944C4 7F05F994 AFA20030 */  sw    $v0, 0x30($sp)
/* 0944C8 7F05F998 8E110014 */  lw    $s1, 0x14($s0)
/* 0944CC 7F05F99C 02402825 */  move  $a1, $s2
/* 0944D0 7F05F9A0 0FC16008 */  jal   matrix_4x4_copy
/* 0944D4 7F05F9A4 24840268 */   addiu $a0, $a0, 0x268
/* 0944D8 7F05F9A8 44800000 */  mtc1  $zero, $f0
/* 0944DC 7F05F9AC 02402825 */  move  $a1, $s2
/* 0944E0 7F05F9B0 E7A00064 */  swc1  $f0, 0x64($sp)
/* 0944E4 7F05F9B4 E7A00068 */  swc1  $f0, 0x68($sp)
/* 0944E8 7F05F9B8 E7A0006C */  swc1  $f0, 0x6c($sp)
/* 0944EC 7F05F9BC 8E180014 */  lw    $t8, 0x14($s0)
/* 0944F0 7F05F9C0 0FC1629F */  jal   matrix_scalar_multiply
/* 0944F4 7F05F9C4 C70C0014 */   lwc1  $f12, 0x14($t8)
/* 0944F8 7F05F9C8 8FA5007C */  lw    $a1, 0x7c($sp)
/* 0944FC 7F05F9CC 8FB90030 */  lw    $t9, 0x30($sp)
/* 094500 7F05F9D0 02002025 */  move  $a0, $s0
/* 094504 7F05F9D4 02403025 */  move  $a2, $s2
/* 094508 7F05F9D8 24A502E8 */  addiu $a1, $a1, 0x2e8
/* 09450C 7F05F9DC 0FC101D5 */  jal   objChangeShading
/* 094510 7F05F9E0 8F270014 */   lw    $a3, 0x14($t9)
/* 094514 7F05F9E4 0FC10121 */  jal   chrobjCollisionRelated
/* 094518 7F05F9E8 02002025 */   move  $a0, $s0
/* 09451C 7F05F9EC 8E280008 */  lw    $t0, 8($s1)
/* 094520 7F05F9F0 8504000E */  lh    $a0, 0xe($t0)
/* 094524 7F05F9F4 00044980 */  sll   $t1, $a0, 6
/* 094528 7F05F9F8 0FC2F5C5 */  jal   dynAllocate
/* 09452C 7F05F9FC 01202025 */   move  $a0, $t1
/* 094530 7F05FA00 AE22000C */  sw    $v0, 0xc($s1)
/* 094534 7F05FA04 26040018 */  addiu $a0, $s0, 0x18
/* 094538 7F05FA08 0FC16008 */  jal   matrix_4x4_copy
/* 09453C 7F05FA0C 02402825 */   move  $a1, $s2
/* 094540 7F05FA10 26040058 */  addiu $a0, $s0, 0x58
/* 094544 7F05FA14 0FC16266 */  jal   matrix_4x4_set_position
/* 094548 7F05FA18 02402825 */   move  $a1, $s2
/* 09454C 7F05FA1C 0FC1E0F1 */  jal   camGetWorldToScreenMtxf
/* 094550 7F05FA20 00000000 */   nop
/* 094554 7F05FA24 00402025 */  move  $a0, $v0
/* 094558 7F05FA28 02402825 */  move  $a1, $s2
/* 09455C 7F05FA2C 0FC16063 */  jal   matrix_4x4_multiply_homogeneous
/* 094560 7F05FA30 8E26000C */   lw    $a2, 0xc($s1)
/* 094564 7F05FA34 8E2A0008 */  lw    $t2, 8($s1)
/* 094568 7F05FA38 02202025 */  move  $a0, $s1
/* 09456C 7F05FA3C 0FC1BBA9 */  jal   modelUpdateRelationsQuick
/* 094570 7F05FA40 8D450000 */   lw    $a1, ($t2)
/* 094574 7F05FA44 926B0001 */  lbu   $t3, 1($s3)
/* 094578 7F05FA48 356C0002 */  ori   $t4, $t3, 2
/* 09457C 7F05FA4C A26C0001 */  sb    $t4, 1($s3)
/* 094580 7F05FA50 8E2D000C */  lw    $t5, 0xc($s1)
/* 094584 7F05FA54 C5A40038 */  lwc1  $f4, 0x38($t5)
/* 094588 7F05FA58 46002187 */  neg.s $f6, $f4
/* 09458C 7F05FA5C E6660018 */  swc1  $f6, 0x18($s3)
/* 094590 7F05FA60 8FBF0024 */  lw    $ra, 0x24($sp)
.L7F05FA64:
/* 094594 7F05FA64 8FB00014 */  lw    $s0, 0x14($sp)
/* 094598 7F05FA68 8FB10018 */  lw    $s1, 0x18($sp)
/* 09459C 7F05FA6C 8FB2001C */  lw    $s2, 0x1c($sp)
/* 0945A0 7F05FA70 8FB30020 */  lw    $s3, 0x20($sp)
/* 0945A4 7F05FA74 03E00008 */  jr    $ra
/* 0945A8 7F05FA78 27BD0080 */   addiu $sp, $sp, 0x80
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.text
glabel sub_GAME_7F05F928
/* 0927D0 7F05FDE0 000478C0 */  sll   $t7, $a0, 3
/* 0927D4 7F05FDE4 01E47823 */  subu  $t7, $t7, $a0
/* 0927D8 7F05FDE8 27BDFF80 */  addiu $sp, $sp, -0x80
/* 0927DC 7F05FDEC 000F7880 */  sll   $t7, $t7, 2
/* 0927E0 7F05FDF0 01E47821 */  addu  $t7, $t7, $a0
/* 0927E4 7F05FDF4 3C0E8007 */  lui   $t6, %hi(g_CurrentPlayer) # $t6, 0x8007
/* 0927E8 7F05FDF8 8DCE8BC0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 0927EC 7F05FDFC 000F7880 */  sll   $t7, $t7, 2
/* 0927F0 7F05FE00 01E47821 */  addu  $t7, $t7, $a0
/* 0927F4 7F05FE04 000F78C0 */  sll   $t7, $t7, 3
/* 0927F8 7F05FE08 AFBF0024 */  sw    $ra, 0x24($sp)
/* 0927FC 7F05FE0C AFB30020 */  sw    $s3, 0x20($sp)
/* 092800 7F05FE10 AFB2001C */  sw    $s2, 0x1c($sp)
/* 092804 7F05FE14 AFB10018 */  sw    $s1, 0x18($sp)
/* 092808 7F05FE18 AFB00014 */  sw    $s0, 0x14($sp)
/* 09280C 7F05FE1C 01CF1021 */  addu  $v0, $t6, $t7
/* 092810 7F05FE20 8C500A88 */  lw    $s0, 0xa88($v0)
/* 092814 7F05FE24 24420868 */  addiu $v0, $v0, 0x868
/* 092818 7F05FE28 5200003C */  beql  $s0, $zero, .L7F05FF1C
/* 09281C 7F05FE2C 8FBF0024 */   lw    $ra, 0x24($sp)
/* 092820 7F05FE30 8E130010 */  lw    $s3, 0x10($s0)
/* 092824 7F05FE34 52600039 */  beql  $s3, $zero, .L7F05FF1C
/* 092828 7F05FE38 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09282C 7F05FE3C 0FC22640 */  jal   get_curplayer_positiondata
/* 092830 7F05FE40 AFA2007C */   sw    $v0, 0x7c($sp)
/* 092834 7F05FE44 8FA4007C */  lw    $a0, 0x7c($sp)
/* 092838 7F05FE48 27B20034 */  addiu $s2, $sp, 0x34
/* 09283C 7F05FE4C AFA20030 */  sw    $v0, 0x30($sp)
/* 092840 7F05FE50 8E110014 */  lw    $s1, 0x14($s0)
/* 092844 7F05FE54 02402825 */  move  $a1, $s2
/* 092848 7F05FE58 0FC16132 */  jal   matrix_4x4_copy
/* 09284C 7F05FE5C 24840268 */   addiu $a0, $a0, 0x268
/* 092850 7F05FE60 44800000 */  mtc1  $zero, $f0
/* 092854 7F05FE64 02402825 */  move  $a1, $s2
/* 092858 7F05FE68 E7A00064 */  swc1  $f0, 0x64($sp)
/* 09285C 7F05FE6C E7A00068 */  swc1  $f0, 0x68($sp)
/* 092860 7F05FE70 E7A0006C */  swc1  $f0, 0x6c($sp)
/* 092864 7F05FE74 8E180014 */  lw    $t8, 0x14($s0)
/* 092868 7F05FE78 0FC163C9 */  jal   matrix_scalar_multiply
/* 09286C 7F05FE7C C70C0014 */   lwc1  $f12, 0x14($t8)
/* 092870 7F05FE80 8FA5007C */  lw    $a1, 0x7c($sp)
/* 092874 7F05FE84 8FB90030 */  lw    $t9, 0x30($sp)
/* 092878 7F05FE88 02002025 */  move  $a0, $s0
/* 09287C 7F05FE8C 02403025 */  move  $a2, $s2
/* 092880 7F05FE90 24A502E8 */  addiu $a1, $a1, 0x2e8
/* 092884 7F05FE94 0FC10205 */  jal   objChangeShading
/* 092888 7F05FE98 8F270014 */   lw    $a3, 0x14($t9)
/* 09288C 7F05FE9C 0FC10151 */  jal   chrobjCollisionRelated
/* 092890 7F05FEA0 02002025 */   move  $a0, $s0
/* 092894 7F05FEA4 8E280008 */  lw    $t0, 8($s1)
/* 092898 7F05FEA8 8504000E */  lh    $a0, 0xe($t0)
/* 09289C 7F05FEAC 00044980 */  sll   $t1, $a0, 6
/* 0928A0 7F05FEB0 0FC2F2B1 */  jal   dynAllocate
/* 0928A4 7F05FEB4 01202025 */   move  $a0, $t1
/* 0928A8 7F05FEB8 AE22000C */  sw    $v0, 0xc($s1)
/* 0928AC 7F05FEBC 26040018 */  addiu $a0, $s0, 0x18
/* 0928B0 7F05FEC0 0FC16132 */  jal   matrix_4x4_copy
/* 0928B4 7F05FEC4 02402825 */   move  $a1, $s2
/* 0928B8 7F05FEC8 26040058 */  addiu $a0, $s0, 0x58
/* 0928BC 7F05FECC 0FC16390 */  jal   matrix_4x4_set_position
/* 0928C0 7F05FED0 02402825 */   move  $a1, $s2
/* 0928C4 7F05FED4 0FC1E111 */  jal   camGetWorldToScreenMtxf
/* 0928C8 7F05FED8 00000000 */   nop
/* 0928CC 7F05FEDC 00402025 */  move  $a0, $v0
/* 0928D0 7F05FEE0 02402825 */  move  $a1, $s2
/* 0928D4 7F05FEE4 0FC1618D */  jal   matrix_4x4_multiply_homogeneous
/* 0928D8 7F05FEE8 8E26000C */   lw    $a2, 0xc($s1)
/* 0928DC 7F05FEEC 8E2A0008 */  lw    $t2, 8($s1)
/* 0928E0 7F05FEF0 02202025 */  move  $a0, $s1
/* 0928E4 7F05FEF4 0FC1BCA4 */  jal   modelUpdateRelationsQuick
/* 0928E8 7F05FEF8 8D450000 */   lw    $a1, ($t2)
/* 0928EC 7F05FEFC 926B0001 */  lbu   $t3, 1($s3)
/* 0928F0 7F05FF00 356C0002 */  ori   $t4, $t3, 2
/* 0928F4 7F05FF04 A26C0001 */  sb    $t4, 1($s3)
/* 0928F8 7F05FF08 8E2D000C */  lw    $t5, 0xc($s1)
/* 0928FC 7F05FF0C C5A40038 */  lwc1  $f4, 0x38($t5)
/* 092900 7F05FF10 46002187 */  neg.s $f6, $f4
/* 092904 7F05FF14 E6660018 */  swc1  $f6, 0x18($s3)
/* 092908 7F05FF18 8FBF0024 */  lw    $ra, 0x24($sp)
.L7F05FF1C:
/* 09290C 7F05FF1C 8FB00014 */  lw    $s0, 0x14($sp)
/* 092910 7F05FF20 8FB10018 */  lw    $s1, 0x18($sp)
/* 092914 7F05FF24 8FB2001C */  lw    $s2, 0x1c($sp)
/* 092918 7F05FF28 8FB30020 */  lw    $s3, 0x20($sp)
/* 09291C 7F05FF2C 03E00008 */  jr    $ra
/* 092920 7F05FF30 27BD0080 */   addiu $sp, $sp, 0x80
)
#endif
#endif



void currentPlayerCreateRocket(GUNHAND hand)
{
    struct hand * hand_ptr;
    struct WeaponObjRecord * rocket;

    hand_ptr = &g_CurrentPlayer->hands[hand];

    if ((hand_ptr->rocket == NULL) && (hand_ptr->weapon_ammo_in_magazine > 0))
    {
        rocket = (struct WeaponObjRecord *)create_new_item_instance_of_model(PROP_CHRROCKET, ITEM_ROCKETROUND);
        if (rocket != NULL)
        {
            hand_ptr->rocket = (ObjectRecord *)rocket;
            hand_ptr->firedrocket = 0;
            rocket->timer = 1;
        }
    }
}



/* This function frees some sort of ObjectRecord from the given hand */
void sub_GAME_7F05FB00(enum GUNHAND hand)
{
    struct hand* hand_ptr;
    ObjectRecord* hand_obj_record;

    hand_ptr = &g_CurrentPlayer->hands[hand];
    hand_obj_record = hand_ptr->rocket;

    if (hand_obj_record != NULL)
    {
        objFreePermanently(hand_obj_record, 1);
        hand_ptr->rocket = NULL;
    }
}



#ifdef NONMATCHING
void gunFireTankShell(void) {

}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.late_rodata
glabel D_80053DD8
.word 0x42855555 /*66.666664*/
glabel D_80053DDC
.word 0x3f8e38e3 /*1.111111*/
.text
glabel gunFireTankShell
/* 094694 7F05FB64 000470C0 */  sll   $t6, $a0, 3
/* 094698 7F05FB68 01C47023 */  subu  $t6, $t6, $a0
/* 09469C 7F05FB6C 000E7080 */  sll   $t6, $t6, 2
/* 0946A0 7F05FB70 01C47021 */  addu  $t6, $t6, $a0
/* 0946A4 7F05FB74 3C0F8008 */  lui   $t7, %hi(g_CurrentPlayer)
/* 0946A8 7F05FB78 8DEFA0B0 */  lw    $t7, %lo(g_CurrentPlayer)($t7)
/* 0946AC 7F05FB7C 000E7080 */  sll   $t6, $t6, 2
/* 0946B0 7F05FB80 01C47021 */  addu  $t6, $t6, $a0
/* 0946B4 7F05FB84 27BDFEF8 */  addiu $sp, $sp, -0x108
/* 0946B8 7F05FB88 000E70C0 */  sll   $t6, $t6, 3
/* 0946BC 7F05FB8C 01EEC021 */  addu  $t8, $t7, $t6
/* 0946C0 7F05FB90 AFBF001C */  sw    $ra, 0x1c($sp)
/* 0946C4 7F05FB94 27190870 */  addiu $t9, $t8, 0x870
/* 0946C8 7F05FB98 AFA40108 */  sw    $a0, 0x108($sp)
/* 0946CC 7F05FB9C AFB90100 */  sw    $t9, 0x100($sp)
/* 0946D0 7F05FBA0 0FC225E6 */  jal   get_curplayer_positiondata
/* 0946D4 7F05FBA4 AFAE0024 */   sw    $t6, 0x24($sp)
/* 0946D8 7F05FBA8 0FC2280B */  jal   get_BONDdata_field408
/* 0946DC 7F05FBAC AFA20048 */   sw    $v0, 0x48($sp)
/* 0946E0 7F05FBB0 AFA20044 */  sw    $v0, 0x44($sp)
/* 0946E4 7F05FBB4 0FC17674 */  jal   getCurrentPlayerWeaponId
/* 0946E8 7F05FBB8 8FA40108 */   lw    $a0, 0x108($sp)
/* 0946EC 7F05FBBC AFA20040 */  sw    $v0, 0x40($sp)
/* 0946F0 7F05FBC0 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 0946F4 7F05FBC4 27A400C0 */   addiu $a0, $sp, 0xc0
/* 0946F8 7F05FBC8 8FA80040 */  lw    $t0, 0x40($sp)
/* 0946FC 7F05FBCC 24010020 */  li    $at, 32
/* 094700 7F05FBD0 27A40058 */  addiu $a0, $sp, 0x58
/* 094704 7F05FBD4 1501005E */  bne   $t0, $at, .L7F05FD50
/* 094708 7F05FBD8 27A5004C */   addiu $a1, $sp, 0x4c
/* 09470C 7F05FBDC 0FC1F3A1 */  jal   get_ptr_for_players_tank
/* 094710 7F05FBE0 00000000 */   nop
/* 094714 7F05FBE4 10400009 */  beqz  $v0, .L7F05FC0C
/* 094718 7F05FBE8 AFA20030 */   sw    $v0, 0x30($sp)
/* 09471C 7F05FBEC 90490001 */  lbu   $t1, 1($v0)
/* 094720 7F05FBF0 312A0002 */  andi  $t2, $t1, 2
/* 094724 7F05FBF4 51400006 */  beql  $t2, $zero, .L7F05FC10
/* 094728 7F05FBF8 27A40058 */   addiu $a0, $sp, 0x58
/* 09472C 7F05FBFC 0FC1F3AC */  jal   bondviewSet3dCoord7F07CEB0
/* 094730 7F05FC00 27A4004C */   addiu $a0, $sp, 0x4c
/* 094734 7F05FC04 10000009 */  b     .L7F05FC2C
/* 094738 7F05FC08 00000000 */   nop
.L7F05FC0C:
/* 09473C 7F05FC0C 27A40058 */  addiu $a0, $sp, 0x58
.L7F05FC10:
/* 094740 7F05FC10 0FC1A064 */  jal   sub_GAME_7F068190
/* 094744 7F05FC14 27A5004C */   addiu $a1, $sp, 0x4c
/* 094748 7F05FC18 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 09474C 7F05FC1C 00000000 */   nop
/* 094750 7F05FC20 00402025 */  move  $a0, $v0
/* 094754 7F05FC24 0FC160F6 */  jal   mtx4RotateVecInPlace
/* 094758 7F05FC28 27A5004C */   addiu $a1, $sp, 0x4c
.L7F05FC2C:
/* 09475C 7F05FC2C 3C018005 */  lui   $at, %hi(D_80053DD8)
/* 094760 7F05FC30 C4203DD8 */  lwc1  $f0, %lo(D_80053DD8)($at)
/* 094764 7F05FC34 C7A4004C */  lwc1  $f4, 0x4c($sp)
/* 094768 7F05FC38 C7A80050 */  lwc1  $f8, 0x50($sp)
/* 09476C 7F05FC3C C7B00054 */  lwc1  $f16, 0x54($sp)
/* 094770 7F05FC40 46002182 */  mul.s $f6, $f4, $f0
/* 094774 7F05FC44 3C0B8005 */  lui   $t3, %hi(g_ClockTimer)
/* 094778 7F05FC48 8D6B8374 */  lw    $t3, %lo(g_ClockTimer)($t3)
/* 09477C 7F05FC4C 46004282 */  mul.s $f10, $f8, $f0
/* 094780 7F05FC50 8FA20048 */  lw    $v0, 0x48($sp)
/* 094784 7F05FC54 8FA30044 */  lw    $v1, 0x44($sp)
/* 094788 7F05FC58 46008482 */  mul.s $f18, $f16, $f0
/* 09478C 7F05FC5C E7A600B4 */  swc1  $f6, 0xb4($sp)
/* 094790 7F05FC60 E7AA00B8 */  swc1  $f10, 0xb8($sp)
/* 094794 7F05FC64 19600015 */  blez  $t3, .L7F05FCBC
/* 094798 7F05FC68 E7B200BC */   swc1  $f18, 0xbc($sp)
/* 09479C 7F05FC6C C4440008 */  lwc1  $f4, 8($v0)
/* 0947A0 7F05FC70 C4680000 */  lwc1  $f8, ($v1)
/* 0947A4 7F05FC74 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta)
/* 0947A8 7F05FC78 C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 0947AC 7F05FC7C 46082401 */  sub.s $f16, $f4, $f8
/* 0947B0 7F05FC80 46008103 */  div.s $f4, $f16, $f0
/* 0947B4 7F05FC84 46043200 */  add.s $f8, $f6, $f4
/* 0947B8 7F05FC88 E7A800B4 */  swc1  $f8, 0xb4($sp)
/* 0947BC 7F05FC8C C4660004 */  lwc1  $f6, 4($v1)
/* 0947C0 7F05FC90 C450000C */  lwc1  $f16, 0xc($v0)
/* 0947C4 7F05FC94 46068101 */  sub.s $f4, $f16, $f6
/* 0947C8 7F05FC98 46002203 */  div.s $f8, $f4, $f0
/* 0947CC 7F05FC9C 46085400 */  add.s $f16, $f10, $f8
/* 0947D0 7F05FCA0 E7B000B8 */  swc1  $f16, 0xb8($sp)
/* 0947D4 7F05FCA4 C4640008 */  lwc1  $f4, 8($v1)
/* 0947D8 7F05FCA8 C4460010 */  lwc1  $f6, 0x10($v0)
/* 0947DC 7F05FCAC 46043281 */  sub.s $f10, $f6, $f4
/* 0947E0 7F05FCB0 46005203 */  div.s $f8, $f10, $f0
/* 0947E4 7F05FCB4 46089400 */  add.s $f16, $f18, $f8
/* 0947E8 7F05FCB8 E7B000BC */  swc1  $f16, 0xbc($sp)
.L7F05FCBC:
/* 0947EC 7F05FCBC 8FA30030 */  lw    $v1, 0x30($sp)
/* 0947F0 7F05FCC0 8FA20048 */  lw    $v0, 0x48($sp)
/* 0947F4 7F05FCC4 50600019 */  beql  $v1, $zero, .L7F05FD2C
/* 0947F8 7F05FCC8 C4520008 */   lwc1  $f18, 8($v0)
/* 0947FC 7F05FCCC 906C0001 */  lbu   $t4, 1($v1)
/* 094800 7F05FCD0 318D0002 */  andi  $t5, $t4, 2
/* 094804 7F05FCD4 51A00015 */  beql  $t5, $zero, .L7F05FD2C
/* 094808 7F05FCD8 C4520008 */   lwc1  $f18, 8($v0)
/* 09480C 7F05FCDC 8C620004 */  lw    $v0, 4($v1)
/* 094810 7F05FCE0 8C4E0014 */  lw    $t6, 0x14($v0)
/* 094814 7F05FCE4 8DCF000C */  lw    $t7, 0xc($t6)
/* 094818 7F05FCE8 C5E60130 */  lwc1  $f6, 0x130($t7)
/* 09481C 7F05FCEC E7A60034 */  swc1  $f6, 0x34($sp)
/* 094820 7F05FCF0 8C580014 */  lw    $t8, 0x14($v0)
/* 094824 7F05FCF4 8F19000C */  lw    $t9, 0xc($t8)
/* 094828 7F05FCF8 C7240134 */  lwc1  $f4, 0x134($t9)
/* 09482C 7F05FCFC E7A40038 */  swc1  $f4, 0x38($sp)
/* 094830 7F05FD00 8C480014 */  lw    $t0, 0x14($v0)
/* 094834 7F05FD04 8D09000C */  lw    $t1, 0xc($t0)
/* 094838 7F05FD08 C52A0138 */  lwc1  $f10, 0x138($t1)
/* 09483C 7F05FD0C 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 094840 7F05FD10 E7AA003C */   swc1  $f10, 0x3c($sp)
/* 094844 7F05FD14 00402025 */  move  $a0, $v0
/* 094848 7F05FD18 0FC1611D */  jal   mtx4TransformVecInPlace
/* 09484C 7F05FD1C 27A50034 */   addiu $a1, $sp, 0x34
/* 094850 7F05FD20 10000007 */  b     .L7F05FD40
/* 094854 7F05FD24 00000000 */   nop
/* 094858 7F05FD28 C4520008 */  lwc1  $f18, 8($v0)
.L7F05FD2C:
/* 09485C 7F05FD2C E7B20034 */  swc1  $f18, 0x34($sp)
/* 094860 7F05FD30 C448000C */  lwc1  $f8, 0xc($v0)
/* 094864 7F05FD34 E7A80038 */  swc1  $f8, 0x38($sp)
/* 094868 7F05FD38 C4500010 */  lwc1  $f16, 0x10($v0)
/* 09486C 7F05FD3C E7B0003C */  swc1  $f16, 0x3c($sp)
.L7F05FD40:
/* 094870 7F05FD40 0FC271EB */  jal   setSixExplosionAndSmokeEntries
/* 094874 7F05FD44 00000000 */   nop
/* 094878 7F05FD48 1000003A */  b     .L7F05FE34
/* 09487C 7F05FD4C 00000000 */   nop
.L7F05FD50:
/* 094880 7F05FD50 0FC1A073 */  jal   bullet_path_from_screen_center
/* 094884 7F05FD54 8FA60108 */   lw    $a2, 0x108($sp)
/* 094888 7F05FD58 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 09488C 7F05FD5C 00000000 */   nop
/* 094890 7F05FD60 00402025 */  move  $a0, $v0
/* 094894 7F05FD64 0FC160F6 */  jal   mtx4RotateVecInPlace
/* 094898 7F05FD68 27A5004C */   addiu $a1, $sp, 0x4c
/* 09489C 7F05FD6C 8FA60100 */  lw    $a2, 0x100($sp)
/* 0948A0 7F05FD70 3C018005 */  lui   $at, %hi(D_80053DDC)
/* 0948A4 7F05FD74 C4223DDC */  lwc1  $f2, %lo(D_80053DDC)($at)
/* 0948A8 7F05FD78 C4C602E8 */  lwc1  $f6, 0x2e8($a2)
/* 0948AC 7F05FD7C C7B2004C */  lwc1  $f18, 0x4c($sp)
/* 0948B0 7F05FD80 C7B00050 */  lwc1  $f16, 0x50($sp)
/* 0948B4 7F05FD84 E7A60034 */  swc1  $f6, 0x34($sp)
/* 0948B8 7F05FD88 C4C402EC */  lwc1  $f4, 0x2ec($a2)
/* 0948BC 7F05FD8C 46029202 */  mul.s $f8, $f18, $f2
/* 0948C0 7F05FD90 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta)
/* 0948C4 7F05FD94 E7A40038 */  swc1  $f4, 0x38($sp)
/* 0948C8 7F05FD98 C4CA02F0 */  lwc1  $f10, 0x2f0($a2)
/* 0948CC 7F05FD9C 46028182 */  mul.s $f6, $f16, $f2
/* 0948D0 7F05FDA0 C7A40054 */  lwc1  $f4, 0x54($sp)
/* 0948D4 7F05FDA4 E7AA003C */  swc1  $f10, 0x3c($sp)
/* 0948D8 7F05FDA8 C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 0948DC 7F05FDAC 46022282 */  mul.s $f10, $f4, $f2
/* 0948E0 7F05FDB0 3C0A8005 */  lui   $t2, %hi(g_ClockTimer)
/* 0948E4 7F05FDB4 8D4A8374 */  lw    $t2, %lo(g_ClockTimer)($t2)
/* 0948E8 7F05FDB8 46004482 */  mul.s $f18, $f8, $f0
/* 0948EC 7F05FDBC E7A800A4 */  swc1  $f8, 0xa4($sp)
/* 0948F0 7F05FDC0 E7A600A8 */  swc1  $f6, 0xa8($sp)
/* 0948F4 7F05FDC4 46003402 */  mul.s $f16, $f6, $f0
/* 0948F8 7F05FDC8 E7AA00AC */  swc1  $f10, 0xac($sp)
/* 0948FC 7F05FDCC 8FA20044 */  lw    $v0, 0x44($sp)
/* 094900 7F05FDD0 46005102 */  mul.s $f4, $f10, $f0
/* 094904 7F05FDD4 E7B200B4 */  swc1  $f18, 0xb4($sp)
/* 094908 7F05FDD8 8FA30048 */  lw    $v1, 0x48($sp)
/* 09490C 7F05FDDC E7B000B8 */  swc1  $f16, 0xb8($sp)
/* 094910 7F05FDE0 19400014 */  blez  $t2, .L7F05FE34
/* 094914 7F05FDE4 E7A400BC */   swc1  $f4, 0xbc($sp)
/* 094918 7F05FDE8 C4680008 */  lwc1  $f8, 8($v1)
/* 09491C 7F05FDEC C4460000 */  lwc1  $f6, ($v0)
/* 094920 7F05FDF0 46064281 */  sub.s $f10, $f8, $f6
/* 094924 7F05FDF4 46005103 */  div.s $f4, $f10, $f0
/* 094928 7F05FDF8 46049200 */  add.s $f8, $f18, $f4
/* 09492C 7F05FDFC E7A800B4 */  swc1  $f8, 0xb4($sp)
/* 094930 7F05FE00 C44A0004 */  lwc1  $f10, 4($v0)
/* 094934 7F05FE04 C466000C */  lwc1  $f6, 0xc($v1)
/* 094938 7F05FE08 460A3481 */  sub.s $f18, $f6, $f10
/* 09493C 7F05FE0C 46009103 */  div.s $f4, $f18, $f0
/* 094940 7F05FE10 46048200 */  add.s $f8, $f16, $f4
/* 094944 7F05FE14 C7A400BC */  lwc1  $f4, 0xbc($sp)
/* 094948 7F05FE18 E7A800B8 */  swc1  $f8, 0xb8($sp)
/* 09494C 7F05FE1C C44A0008 */  lwc1  $f10, 8($v0)
/* 094950 7F05FE20 C4660010 */  lwc1  $f6, 0x10($v1)
/* 094954 7F05FE24 460A3481 */  sub.s $f18, $f6, $f10
/* 094958 7F05FE28 46009403 */  div.s $f16, $f18, $f0
/* 09495C 7F05FE2C 46102200 */  add.s $f8, $f4, $f16
/* 094960 7F05FE30 E7A800BC */  swc1  $f8, 0xbc($sp)
.L7F05FE34:
/* 094964 7F05FE34 3C0B8008 */  lui   $t3, %hi(g_CurrentPlayer)
/* 094968 7F05FE38 8D6BA0B0 */  lw    $t3, %lo(g_CurrentPlayer)($t3)
/* 09496C 7F05FE3C 8FAC0024 */  lw    $t4, 0x24($sp)
/* 094970 7F05FE40 27A50064 */  addiu $a1, $sp, 0x64
/* 094974 7F05FE44 016C2021 */  addu  $a0, $t3, $t4
/* 094978 7F05FE48 0FC16008 */  jal   matrix_4x4_copy
/* 09497C 7F05FE4C 24840AD8 */   addiu $a0, $a0, 0xad8
/* 094980 7F05FE50 44800000 */  mtc1  $zero, $f0
/* 094984 7F05FE54 8FA30100 */  lw    $v1, 0x100($sp)
/* 094988 7F05FE58 240D0001 */  li    $t5, 1
/* 09498C 7F05FE5C E7A00094 */  swc1  $f0, 0x94($sp)
/* 094990 7F05FE60 E7A00098 */  swc1  $f0, 0x98($sp)
/* 094994 7F05FE64 E7A0009C */  swc1  $f0, 0x9c($sp)
/* 094998 7F05FE68 8C620220 */  lw    $v0, 0x220($v1)
/* 09499C 7F05FE6C 240400CA */  li    $a0, 202
/* 0949A0 7F05FE70 10400004 */  beqz  $v0, .L7F05FE84
/* 0949A4 7F05FE74 00000000 */   nop
/* 0949A8 7F05FE78 00402025 */  move  $a0, $v0
/* 0949AC 7F05FE7C 10000004 */  b     .L7F05FE90
/* 0949B0 7F05FE80 AC6D0224 */   sw    $t5, 0x224($v1)
.L7F05FE84:
/* 0949B4 7F05FE84 0FC1481B */  jal   create_new_item_instance_of_model
/* 0949B8 7F05FE88 24050056 */   li    $a1, 86
/* 0949BC 7F05FE8C 00402025 */  move  $a0, $v0
.L7F05FE90:
/* 0949C0 7F05FE90 10800049 */  beqz  $a0, .L7F05FFB8
/* 0949C4 7F05FE94 240EFFFF */   li    $t6, -1
/* 0949C8 7F05FE98 8C8F0064 */  lw    $t7, 0x64($a0)
/* 0949CC 7F05FE9C 3C01FFF9 */  lui   $at, (0xFFF9FFFF >> 16) # lui $at, 0xfff9
/* 0949D0 7F05FEA0 3421FFFF */  ori   $at, (0xFFF9FFFF & 0xFFFF) # ori $at, $at, 0xffff
/* 0949D4 7F05FEA4 01E1C024 */  and   $t8, $t7, $at
/* 0949D8 7F05FEA8 A48E0082 */  sh    $t6, 0x82($a0)
/* 0949DC 7F05FEAC AC980064 */  sw    $t8, 0x64($a0)
/* 0949E0 7F05FEB0 0FC26C54 */  jal   get_cur_playernum
/* 0949E4 7F05FEB4 AFA40104 */   sw    $a0, 0x104($sp)
/* 0949E8 7F05FEB8 8FA40104 */  lw    $a0, 0x104($sp)
/* 0949EC 7F05FEBC 00024440 */  sll   $t0, $v0, 0x11
/* 0949F0 7F05FEC0 27AA00C0 */  addiu $t2, $sp, 0xc0
/* 0949F4 7F05FEC4 8C990064 */  lw    $t9, 0x64($a0)
/* 0949F8 7F05FEC8 27A50034 */  addiu $a1, $sp, 0x34
/* 0949FC 7F05FECC 27A60064 */  addiu $a2, $sp, 0x64
/* 094A00 7F05FED0 03284825 */  or    $t1, $t9, $t0
/* 094A04 7F05FED4 AC890064 */  sw    $t1, 0x64($a0)
/* 094A08 7F05FED8 AFAA0010 */  sw    $t2, 0x10($sp)
/* 094A0C 7F05FEDC 0FC17B07 */  jal   sub_GAME_7F05EC1C
/* 094A10 7F05FEE0 27A700B4 */   addiu $a3, $sp, 0xb4
/* 094A14 7F05FEE4 8FA40104 */  lw    $a0, 0x104($sp)
/* 094A18 7F05FEE8 8C8B0064 */  lw    $t3, 0x64($a0)
/* 094A1C 7F05FEEC 316C0080 */  andi  $t4, $t3, 0x80
/* 094A20 7F05FEF0 51800032 */  beql  $t4, $zero, .L7F05FFBC
/* 094A24 7F05FEF4 8FBF001C */   lw    $ra, 0x1c($sp)
/* 094A28 7F05FEF8 8C82006C */  lw    $v0, 0x6c($a0)
/* 094A2C 7F05FEFC 24010020 */  li    $at, 32
/* 094A30 7F05FF00 8C4D0000 */  lw    $t5, ($v0)
/* 094A34 7F05FF04 35AE0080 */  ori   $t6, $t5, 0x80
/* 094A38 7F05FF08 AC4E0000 */  sw    $t6, ($v0)
/* 094A3C 7F05FF0C 8FAF0040 */  lw    $t7, 0x40($sp)
/* 094A40 7F05FF10 51E1002A */  beql  $t7, $at, .L7F05FFBC
/* 094A44 7F05FF14 8FBF001C */   lw    $ra, 0x1c($sp)
/* 094A48 7F05FF18 8C82006C */  lw    $v0, 0x6c($a0)
/* 094A4C 7F05FF1C 240C003C */  li    $t4, 60
/* 094A50 7F05FF20 24050001 */  li    $a1, 1
/* 094A54 7F05FF24 8C580000 */  lw    $t8, ($v0)
/* 094A58 7F05FF28 37190020 */  ori   $t9, $t8, 0x20
/* 094A5C 7F05FF2C AC590000 */  sw    $t9, ($v0)
/* 094A60 7F05FF30 8C88006C */  lw    $t0, 0x6c($a0)
/* 094A64 7F05FF34 C486005C */  lwc1  $f6, 0x5c($a0)
/* 094A68 7F05FF38 E50600B0 */  swc1  $f6, 0xb0($t0)
/* 094A6C 7F05FF3C 8C82006C */  lw    $v0, 0x6c($a0)
/* 094A70 7F05FF40 C44A0008 */  lwc1  $f10, 8($v0)
/* 094A74 7F05FF44 E44A00B4 */  swc1  $f10, 0xb4($v0)
/* 094A78 7F05FF48 8C89006C */  lw    $t1, 0x6c($a0)
/* 094A7C 7F05FF4C C7B200A4 */  lwc1  $f18, 0xa4($sp)
/* 094A80 7F05FF50 E5320010 */  swc1  $f18, 0x10($t1)
/* 094A84 7F05FF54 8C8A006C */  lw    $t2, 0x6c($a0)
/* 094A88 7F05FF58 C7A400A8 */  lwc1  $f4, 0xa8($sp)
/* 094A8C 7F05FF5C E5440014 */  swc1  $f4, 0x14($t2)
/* 094A90 7F05FF60 8C8B006C */  lw    $t3, 0x6c($a0)
/* 094A94 7F05FF64 C7B000AC */  lwc1  $f16, 0xac($sp)
/* 094A98 7F05FF68 E5700018 */  swc1  $f16, 0x18($t3)
/* 094A9C 7F05FF6C 8C8D006C */  lw    $t5, 0x6c($a0)
/* 094AA0 7F05FF70 ADAC00BC */  sw    $t4, 0xbc($t5)
/* 094AA4 7F05FF74 8C82006C */  lw    $v0, 0x6c($a0)
/* 094AA8 7F05FF78 3C048006 */  lui   $a0, %hi(g_musicSfxBufferPtr)
/* 094AAC 7F05FF7C 8C4E0098 */  lw    $t6, 0x98($v0)
/* 094AB0 7F05FF80 24460098 */  addiu $a2, $v0, 0x98
/* 094AB4 7F05FF84 55C00006 */  bnezl $t6, .L7F05FFA0
/* 094AB8 7F05FF88 8C4F009C */   lw    $t7, 0x9c($v0)
/* 094ABC 7F05FF8C 0C002382 */  jal   sndPlaySfx
/* 094AC0 7F05FF90 8C843720 */   lw    $a0, %lo(g_musicSfxBufferPtr)($a0)
/* 094AC4 7F05FF94 10000009 */  b     .L7F05FFBC
/* 094AC8 7F05FF98 8FBF001C */   lw    $ra, 0x1c($sp)
/* 094ACC 7F05FF9C 8C4F009C */  lw    $t7, 0x9c($v0)
.L7F05FFA0:
/* 094AD0 7F05FFA0 3C048006 */  lui   $a0, %hi(g_musicSfxBufferPtr)
/* 094AD4 7F05FFA4 24050001 */  li    $a1, 1
/* 094AD8 7F05FFA8 15E00003 */  bnez  $t7, .L7F05FFB8
/* 094ADC 7F05FFAC 2446009C */   addiu $a2, $v0, 0x9c
/* 094AE0 7F05FFB0 0C002382 */  jal   sndPlaySfx
/* 094AE4 7F05FFB4 8C843720 */   lw    $a0, %lo(g_musicSfxBufferPtr)($a0)
.L7F05FFB8:
/* 094AE8 7F05FFB8 8FBF001C */  lw    $ra, 0x1c($sp)
.L7F05FFBC:
/* 094AEC 7F05FFBC 27BD0108 */  addiu $sp, $sp, 0x108
/* 094AF0 7F05FFC0 03E00008 */  jr    $ra
/* 094AF4 7F05FFC4 00000000 */   nop
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.late_rodata
glabel D_80053DD8
.word 0x42855555 /*66.666664*/
glabel D_80053DDC
.word 0x3f8e38e3 /*1.111111*/
.text
glabel gunFireTankShell
/* 092A0C 7F06001C 000470C0 */  sll   $t6, $a0, 3
/* 092A10 7F060020 01C47023 */  subu  $t6, $t6, $a0
/* 092A14 7F060024 000E7080 */  sll   $t6, $t6, 2
/* 092A18 7F060028 01C47021 */  addu  $t6, $t6, $a0
/* 092A1C 7F06002C 3C0F8007 */  lui   $t7, %hi(g_CurrentPlayer) # $t7, 0x8007
/* 092A20 7F060030 8DEF8BC0 */  lw    $t7, %lo(g_CurrentPlayer)($t7)
/* 092A24 7F060034 000E7080 */  sll   $t6, $t6, 2
/* 092A28 7F060038 01C47021 */  addu  $t6, $t6, $a0
/* 092A2C 7F06003C 27BDFEF8 */  addiu $sp, $sp, -0x108
/* 092A30 7F060040 000E70C0 */  sll   $t6, $t6, 3
/* 092A34 7F060044 01EEC021 */  addu  $t8, $t7, $t6
/* 092A38 7F060048 AFBF001C */  sw    $ra, 0x1c($sp)
/* 092A3C 7F06004C 27190868 */  addiu $t9, $t8, 0x868
/* 092A40 7F060050 AFA40108 */  sw    $a0, 0x108($sp)
/* 092A44 7F060054 AFB90100 */  sw    $t9, 0x100($sp)
/* 092A48 7F060058 0FC22640 */  jal   get_curplayer_positiondata
/* 092A4C 7F06005C AFAE0024 */   sw    $t6, 0x24($sp)
/* 092A50 7F060060 0FC2287E */  jal   get_BONDdata_field408
/* 092A54 7F060064 AFA20048 */   sw    $v0, 0x48($sp)
/* 092A58 7F060068 AFA20044 */  sw    $v0, 0x44($sp)
/* 092A5C 7F06006C 0FC177A2 */  jal   getCurrentPlayerWeaponId
/* 092A60 7F060070 8FA40108 */   lw    $a0, 0x108($sp)
/* 092A64 7F060074 AFA20040 */  sw    $v0, 0x40($sp)
/* 092A68 7F060078 0FC1611E */  jal   matrix_4x4_set_identity
/* 092A6C 7F06007C 27A400C0 */   addiu $a0, $sp, 0xc0
/* 092A70 7F060080 8FA80040 */  lw    $t0, 0x40($sp)
/* 092A74 7F060084 24010020 */  li    $at, 32
/* 092A78 7F060088 27A40058 */  addiu $a0, $sp, 0x58
/* 092A7C 7F06008C 1501005E */  bne   $t0, $at, .L7F060208
/* 092A80 7F060090 27A5004C */   addiu $a1, $sp, 0x4c
/* 092A84 7F060094 0FC1F3D6 */  jal   get_ptr_for_players_tank
/* 092A88 7F060098 00000000 */   nop
/* 092A8C 7F06009C 10400009 */  beqz  $v0, .L7F0600C4
/* 092A90 7F0600A0 AFA20030 */   sw    $v0, 0x30($sp)
/* 092A94 7F0600A4 90490001 */  lbu   $t1, 1($v0)
/* 092A98 7F0600A8 312A0002 */  andi  $t2, $t1, 2
/* 092A9C 7F0600AC 51400006 */  beql  $t2, $zero, .L7F0600C8
/* 092AA0 7F0600B0 27A40058 */   addiu $a0, $sp, 0x58
/* 092AA4 7F0600B4 0FC1F3E1 */  jal   bondviewSet3dCoord7F07CEB0
/* 092AA8 7F0600B8 27A4004C */   addiu $a0, $sp, 0x4c
/* 092AAC 7F0600BC 10000009 */  b     .L7F0600E4
/* 092AB0 7F0600C0 00000000 */   nop
.L7F0600C4:
/* 092AB4 7F0600C4 27A40058 */  addiu $a0, $sp, 0x58
.L7F0600C8:
/* 092AB8 7F0600C8 0FC1A24E */  jal   sub_GAME_7F068190
/* 092ABC 7F0600CC 27A5004C */   addiu $a1, $sp, 0x4c
/* 092AC0 7F0600D0 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 092AC4 7F0600D4 00000000 */   nop
/* 092AC8 7F0600D8 00402025 */  move  $a0, $v0
/* 092ACC 7F0600DC 0FC16220 */  jal   mtx4RotateVecInPlace
/* 092AD0 7F0600E0 27A5004C */   addiu $a1, $sp, 0x4c
.L7F0600E4:
/* 092AD4 7F0600E4 3C018005 */  lui   $at, %hi(D_80053DD8) # $at, 0x8005
/* 092AD8 7F0600E8 C4209F18 */  lwc1  $f0, %lo(D_80053DD8)($at)
/* 092ADC 7F0600EC C7A4004C */  lwc1  $f4, 0x4c($sp)
/* 092AE0 7F0600F0 C7A80050 */  lwc1  $f8, 0x50($sp)
/* 092AE4 7F0600F4 C7B00054 */  lwc1  $f16, 0x54($sp)
/* 092AE8 7F0600F8 46002182 */  mul.s $f6, $f4, $f0
/* 092AEC 7F0600FC 3C0B8004 */  lui   $t3, %hi(g_ClockTimer) # $t3, 0x8004
/* 092AF0 7F060100 8D6B0FF4 */  lw    $t3, %lo(g_ClockTimer)($t3)
/* 092AF4 7F060104 46004282 */  mul.s $f10, $f8, $f0
/* 092AF8 7F060108 8FA20048 */  lw    $v0, 0x48($sp)
/* 092AFC 7F06010C 8FA30044 */  lw    $v1, 0x44($sp)
/* 092B00 7F060110 46008482 */  mul.s $f18, $f16, $f0
/* 092B04 7F060114 E7A600B4 */  swc1  $f6, 0xb4($sp)
/* 092B08 7F060118 E7AA00B8 */  swc1  $f10, 0xb8($sp)
/* 092B0C 7F06011C 19600015 */  blez  $t3, .L7F060174
/* 092B10 7F060120 E7B200BC */   swc1  $f18, 0xbc($sp)
/* 092B14 7F060124 C4440008 */  lwc1  $f4, 8($v0)
/* 092B18 7F060128 C4680000 */  lwc1  $f8, ($v1)
/* 092B1C 7F06012C 3C018004 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8004
/* 092B20 7F060130 C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 092B24 7F060134 46082401 */  sub.s $f16, $f4, $f8
/* 092B28 7F060138 46008103 */  div.s $f4, $f16, $f0
/* 092B2C 7F06013C 46043200 */  add.s $f8, $f6, $f4
/* 092B30 7F060140 E7A800B4 */  swc1  $f8, 0xb4($sp)
/* 092B34 7F060144 C4660004 */  lwc1  $f6, 4($v1)
/* 092B38 7F060148 C450000C */  lwc1  $f16, 0xc($v0)
/* 092B3C 7F06014C 46068101 */  sub.s $f4, $f16, $f6
/* 092B40 7F060150 46002203 */  div.s $f8, $f4, $f0
/* 092B44 7F060154 46085400 */  add.s $f16, $f10, $f8
/* 092B48 7F060158 E7B000B8 */  swc1  $f16, 0xb8($sp)
/* 092B4C 7F06015C C4640008 */  lwc1  $f4, 8($v1)
/* 092B50 7F060160 C4460010 */  lwc1  $f6, 0x10($v0)
/* 092B54 7F060164 46043281 */  sub.s $f10, $f6, $f4
/* 092B58 7F060168 46005203 */  div.s $f8, $f10, $f0
/* 092B5C 7F06016C 46089400 */  add.s $f16, $f18, $f8
/* 092B60 7F060170 E7B000BC */  swc1  $f16, 0xbc($sp)
.L7F060174:
/* 092B64 7F060174 8FA30030 */  lw    $v1, 0x30($sp)
/* 092B68 7F060178 8FA20048 */  lw    $v0, 0x48($sp)
/* 092B6C 7F06017C 50600019 */  beql  $v1, $zero, .L7F0601E4
/* 092B70 7F060180 C4520008 */   lwc1  $f18, 8($v0)
/* 092B74 7F060184 906C0001 */  lbu   $t4, 1($v1)
/* 092B78 7F060188 318D0002 */  andi  $t5, $t4, 2
/* 092B7C 7F06018C 51A00015 */  beql  $t5, $zero, .L7F0601E4
/* 092B80 7F060190 C4520008 */   lwc1  $f18, 8($v0)
/* 092B84 7F060194 8C620004 */  lw    $v0, 4($v1)
/* 092B88 7F060198 8C4E0014 */  lw    $t6, 0x14($v0)
/* 092B8C 7F06019C 8DCF000C */  lw    $t7, 0xc($t6)
/* 092B90 7F0601A0 C5E60130 */  lwc1  $f6, 0x130($t7)
/* 092B94 7F0601A4 E7A60034 */  swc1  $f6, 0x34($sp)
/* 092B98 7F0601A8 8C580014 */  lw    $t8, 0x14($v0)
/* 092B9C 7F0601AC 8F19000C */  lw    $t9, 0xc($t8)
/* 092BA0 7F0601B0 C7240134 */  lwc1  $f4, 0x134($t9)
/* 092BA4 7F0601B4 E7A40038 */  swc1  $f4, 0x38($sp)
/* 092BA8 7F0601B8 8C480014 */  lw    $t0, 0x14($v0)
/* 092BAC 7F0601BC 8D09000C */  lw    $t1, 0xc($t0)
/* 092BB0 7F0601C0 C52A0138 */  lwc1  $f10, 0x138($t1)
/* 092BB4 7F0601C4 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 092BB8 7F0601C8 E7AA003C */   swc1  $f10, 0x3c($sp)
/* 092BBC 7F0601CC 00402025 */  move  $a0, $v0
/* 092BC0 7F0601D0 0FC16247 */  jal   mtx4TransformVecInPlace
/* 092BC4 7F0601D4 27A50034 */   addiu $a1, $sp, 0x34
/* 092BC8 7F0601D8 10000007 */  b     .L7F0601F8
/* 092BCC 7F0601DC 00000000 */   nop
/* 092BD0 7F0601E0 C4520008 */  lwc1  $f18, 8($v0)
.L7F0601E4:
/* 092BD4 7F0601E4 E7B20034 */  swc1  $f18, 0x34($sp)
/* 092BD8 7F0601E8 C448000C */  lwc1  $f8, 0xc($v0)
/* 092BDC 7F0601EC E7A80038 */  swc1  $f8, 0x38($sp)
/* 092BE0 7F0601F0 C4500010 */  lwc1  $f16, 0x10($v0)
/* 092BE4 7F0601F4 E7B0003C */  swc1  $f16, 0x3c($sp)
.L7F0601F8:
/* 092BE8 7F0601F8 0FC26F30 */  jal   setSixExplosionAndSmokeEntries
/* 092BEC 7F0601FC 00000000 */   nop
/* 092BF0 7F060200 1000003A */  b     .L7F0602EC
/* 092BF4 7F060204 00000000 */   nop
.L7F060208:
/* 092BF8 7F060208 0FC1A25D */  jal   bullet_path_from_screen_center
/* 092BFC 7F06020C 8FA60108 */   lw    $a2, 0x108($sp)
/* 092C00 7F060210 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 092C04 7F060214 00000000 */   nop
/* 092C08 7F060218 00402025 */  move  $a0, $v0
/* 092C0C 7F06021C 0FC16220 */  jal   mtx4RotateVecInPlace
/* 092C10 7F060220 27A5004C */   addiu $a1, $sp, 0x4c
/* 092C14 7F060224 8FA60100 */  lw    $a2, 0x100($sp)
/* 092C18 7F060228 3C018005 */  lui   $at, %hi(D_80053DDC) # $at, 0x8005
/* 092C1C 7F06022C C4229F1C */  lwc1  $f2, %lo(D_80053DDC)($at)
/* 092C20 7F060230 C4C602E8 */  lwc1  $f6, 0x2e8($a2)
/* 092C24 7F060234 C7B2004C */  lwc1  $f18, 0x4c($sp)
/* 092C28 7F060238 C7B00050 */  lwc1  $f16, 0x50($sp)
/* 092C2C 7F06023C E7A60034 */  swc1  $f6, 0x34($sp)
/* 092C30 7F060240 C4C402EC */  lwc1  $f4, 0x2ec($a2)
/* 092C34 7F060244 46029202 */  mul.s $f8, $f18, $f2
/* 092C38 7F060248 3C018004 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8004
/* 092C3C 7F06024C E7A40038 */  swc1  $f4, 0x38($sp)
/* 092C40 7F060250 C4CA02F0 */  lwc1  $f10, 0x2f0($a2)
/* 092C44 7F060254 46028182 */  mul.s $f6, $f16, $f2
/* 092C48 7F060258 C7A40054 */  lwc1  $f4, 0x54($sp)
/* 092C4C 7F06025C E7AA003C */  swc1  $f10, 0x3c($sp)
/* 092C50 7F060260 C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 092C54 7F060264 46022282 */  mul.s $f10, $f4, $f2
/* 092C58 7F060268 3C0A8004 */  lui   $t2, %hi(g_ClockTimer) # $t2, 0x8004
/* 092C5C 7F06026C 8D4A0FF4 */  lw    $t2, %lo(g_ClockTimer)($t2)
/* 092C60 7F060270 46004482 */  mul.s $f18, $f8, $f0
/* 092C64 7F060274 E7A800A4 */  swc1  $f8, 0xa4($sp)
/* 092C68 7F060278 E7A600A8 */  swc1  $f6, 0xa8($sp)
/* 092C6C 7F06027C 46003402 */  mul.s $f16, $f6, $f0
/* 092C70 7F060280 E7AA00AC */  swc1  $f10, 0xac($sp)
/* 092C74 7F060284 8FA20044 */  lw    $v0, 0x44($sp)
/* 092C78 7F060288 46005102 */  mul.s $f4, $f10, $f0
/* 092C7C 7F06028C E7B200B4 */  swc1  $f18, 0xb4($sp)
/* 092C80 7F060290 8FA30048 */  lw    $v1, 0x48($sp)
/* 092C84 7F060294 E7B000B8 */  swc1  $f16, 0xb8($sp)
/* 092C88 7F060298 19400014 */  blez  $t2, .L7F0602EC
/* 092C8C 7F06029C E7A400BC */   swc1  $f4, 0xbc($sp)
/* 092C90 7F0602A0 C4680008 */  lwc1  $f8, 8($v1)
/* 092C94 7F0602A4 C4460000 */  lwc1  $f6, ($v0)
/* 092C98 7F0602A8 46064281 */  sub.s $f10, $f8, $f6
/* 092C9C 7F0602AC 46005103 */  div.s $f4, $f10, $f0
/* 092CA0 7F0602B0 46049200 */  add.s $f8, $f18, $f4
/* 092CA4 7F0602B4 E7A800B4 */  swc1  $f8, 0xb4($sp)
/* 092CA8 7F0602B8 C44A0004 */  lwc1  $f10, 4($v0)
/* 092CAC 7F0602BC C466000C */  lwc1  $f6, 0xc($v1)
/* 092CB0 7F0602C0 460A3481 */  sub.s $f18, $f6, $f10
/* 092CB4 7F0602C4 46009103 */  div.s $f4, $f18, $f0
/* 092CB8 7F0602C8 46048200 */  add.s $f8, $f16, $f4
/* 092CBC 7F0602CC C7A400BC */  lwc1  $f4, 0xbc($sp)
/* 092CC0 7F0602D0 E7A800B8 */  swc1  $f8, 0xb8($sp)
/* 092CC4 7F0602D4 C44A0008 */  lwc1  $f10, 8($v0)
/* 092CC8 7F0602D8 C4660010 */  lwc1  $f6, 0x10($v1)
/* 092CCC 7F0602DC 460A3481 */  sub.s $f18, $f6, $f10
/* 092CD0 7F0602E0 46009403 */  div.s $f16, $f18, $f0
/* 092CD4 7F0602E4 46102200 */  add.s $f8, $f4, $f16
/* 092CD8 7F0602E8 E7A800BC */  swc1  $f8, 0xbc($sp)
.L7F0602EC:
/* 092CDC 7F0602EC 3C0B8007 */  lui   $t3, %hi(g_CurrentPlayer) # $t3, 0x8007
/* 092CE0 7F0602F0 8D6B8BC0 */  lw    $t3, %lo(g_CurrentPlayer)($t3)
/* 092CE4 7F0602F4 8FAC0024 */  lw    $t4, 0x24($sp)
/* 092CE8 7F0602F8 27A50064 */  addiu $a1, $sp, 0x64
/* 092CEC 7F0602FC 016C2021 */  addu  $a0, $t3, $t4
/* 092CF0 7F060300 0FC16132 */  jal   matrix_4x4_copy
/* 092CF4 7F060304 24840AD0 */   addiu $a0, $a0, 0xad0
/* 092CF8 7F060308 44800000 */  mtc1  $zero, $f0
/* 092CFC 7F06030C 8FA30100 */  lw    $v1, 0x100($sp)
/* 092D00 7F060310 240D0001 */  li    $t5, 1
/* 092D04 7F060314 E7A00094 */  swc1  $f0, 0x94($sp)
/* 092D08 7F060318 E7A00098 */  swc1  $f0, 0x98($sp)
/* 092D0C 7F06031C E7A0009C */  swc1  $f0, 0x9c($sp)
/* 092D10 7F060320 8C620220 */  lw    $v0, 0x220($v1)
/* 092D14 7F060324 240400CA */  li    $a0, 202
/* 092D18 7F060328 10400004 */  beqz  $v0, .L7F06033C
/* 092D1C 7F06032C 00000000 */   nop
/* 092D20 7F060330 00402025 */  move  $a0, $v0
/* 092D24 7F060334 10000004 */  b     .L7F060348
/* 092D28 7F060338 AC6D0224 */   sw    $t5, 0x224($v1)
.L7F06033C:
/* 092D2C 7F06033C 0FC148D3 */  jal   create_new_item_instance_of_model
/* 092D30 7F060340 24050056 */   li    $a1, 86
/* 092D34 7F060344 00402025 */  move  $a0, $v0
.L7F060348:
/* 092D38 7F060348 10800049 */  beqz  $a0, .L7F060470
/* 092D3C 7F06034C 240EFFFF */   li    $t6, -1
/* 092D40 7F060350 8C8F0064 */  lw    $t7, 0x64($a0)
/* 092D44 7F060354 3C01FFF9 */  lui   $at, (0xFFF9FFFF >> 16) # lui $at, 0xfff9
/* 092D48 7F060358 3421FFFF */  ori   $at, (0xFFF9FFFF & 0xFFFF) # ori $at, $at, 0xffff
/* 092D4C 7F06035C 01E1C024 */  and   $t8, $t7, $at
/* 092D50 7F060360 A48E0082 */  sh    $t6, 0x82($a0)
/* 092D54 7F060364 AC980064 */  sw    $t8, 0x64($a0)
/* 092D58 7F060368 0FC269A4 */  jal   get_cur_playernum
/* 092D5C 7F06036C AFA40104 */   sw    $a0, 0x104($sp)
/* 092D60 7F060370 8FA40104 */  lw    $a0, 0x104($sp)
/* 092D64 7F060374 00024440 */  sll   $t0, $v0, 0x11
/* 092D68 7F060378 27AA00C0 */  addiu $t2, $sp, 0xc0
/* 092D6C 7F06037C 8C990064 */  lw    $t9, 0x64($a0)
/* 092D70 7F060380 27A50034 */  addiu $a1, $sp, 0x34
/* 092D74 7F060384 27A60064 */  addiu $a2, $sp, 0x64
/* 092D78 7F060388 03284825 */  or    $t1, $t9, $t0
/* 092D7C 7F06038C AC890064 */  sw    $t1, 0x64($a0)
/* 092D80 7F060390 AFAA0010 */  sw    $t2, 0x10($sp)
/* 092D84 7F060394 0FC17C35 */  jal   sub_GAME_7F05EC1C
/* 092D88 7F060398 27A700B4 */   addiu $a3, $sp, 0xb4
/* 092D8C 7F06039C 8FA40104 */  lw    $a0, 0x104($sp)
/* 092D90 7F0603A0 8C8B0064 */  lw    $t3, 0x64($a0)
/* 092D94 7F0603A4 316C0080 */  andi  $t4, $t3, 0x80
/* 092D98 7F0603A8 51800032 */  beql  $t4, $zero, .L7F060474
/* 092D9C 7F0603AC 8FBF001C */   lw    $ra, 0x1c($sp)
/* 092DA0 7F0603B0 8C82006C */  lw    $v0, 0x6c($a0)
/* 092DA4 7F0603B4 24010020 */  li    $at, 32
/* 092DA8 7F0603B8 8C4D0000 */  lw    $t5, ($v0)
/* 092DAC 7F0603BC 35AE0080 */  ori   $t6, $t5, 0x80
/* 092DB0 7F0603C0 AC4E0000 */  sw    $t6, ($v0)
/* 092DB4 7F0603C4 8FAF0040 */  lw    $t7, 0x40($sp)
/* 092DB8 7F0603C8 51E1002A */  beql  $t7, $at, .L7F060474
/* 092DBC 7F0603CC 8FBF001C */   lw    $ra, 0x1c($sp)
/* 092DC0 7F0603D0 8C82006C */  lw    $v0, 0x6c($a0)
/* 092DC4 7F0603D4 240C0032 */  li    $t4, 50
/* 092DC8 7F0603D8 24050001 */  li    $a1, 1
/* 092DCC 7F0603DC 8C580000 */  lw    $t8, ($v0)
/* 092DD0 7F0603E0 37190020 */  ori   $t9, $t8, 0x20
/* 092DD4 7F0603E4 AC590000 */  sw    $t9, ($v0)
/* 092DD8 7F0603E8 8C88006C */  lw    $t0, 0x6c($a0)
/* 092DDC 7F0603EC C486005C */  lwc1  $f6, 0x5c($a0)
/* 092DE0 7F0603F0 E50600B0 */  swc1  $f6, 0xb0($t0)
/* 092DE4 7F0603F4 8C82006C */  lw    $v0, 0x6c($a0)
/* 092DE8 7F0603F8 C44A0008 */  lwc1  $f10, 8($v0)
/* 092DEC 7F0603FC E44A00B4 */  swc1  $f10, 0xb4($v0)
/* 092DF0 7F060400 8C89006C */  lw    $t1, 0x6c($a0)
/* 092DF4 7F060404 C7B200A4 */  lwc1  $f18, 0xa4($sp)
/* 092DF8 7F060408 E5320010 */  swc1  $f18, 0x10($t1)
/* 092DFC 7F06040C 8C8A006C */  lw    $t2, 0x6c($a0)
/* 092E00 7F060410 C7A400A8 */  lwc1  $f4, 0xa8($sp)
/* 092E04 7F060414 E5440014 */  swc1  $f4, 0x14($t2)
/* 092E08 7F060418 8C8B006C */  lw    $t3, 0x6c($a0)
/* 092E0C 7F06041C C7B000AC */  lwc1  $f16, 0xac($sp)
/* 092E10 7F060420 E5700018 */  swc1  $f16, 0x18($t3)
/* 092E14 7F060424 8C8D006C */  lw    $t5, 0x6c($a0)
/* 092E18 7F060428 ADAC00BC */  sw    $t4, 0xbc($t5)
/* 092E1C 7F06042C 8C82006C */  lw    $v0, 0x6c($a0)
/* 092E20 7F060430 3C048005 */  lui   $a0, %hi(g_musicSfxBufferPtr) # $a0, 0x8005
/* 092E24 7F060434 8C4E0098 */  lw    $t6, 0x98($v0)
/* 092E28 7F060438 24460098 */  addiu $a2, $v0, 0x98
/* 092E2C 7F06043C 55C00006 */  bnezl $t6, .L7F060458
/* 092E30 7F060440 8C4F009C */   lw    $t7, 0x9c($v0)
/* 092E34 7F060444 0C00209A */  jal   sndPlaySfx
/* 092E38 7F060448 8C846900 */   lw    $a0, %lo(g_musicSfxBufferPtr)($a0)
/* 092E3C 7F06044C 10000009 */  b     .L7F060474
/* 092E40 7F060450 8FBF001C */   lw    $ra, 0x1c($sp)
/* 092E44 7F060454 8C4F009C */  lw    $t7, 0x9c($v0)
.L7F060458:
/* 092E48 7F060458 3C048005 */  lui   $a0, %hi(g_musicSfxBufferPtr) # $a0, 0x8005
/* 092E4C 7F06045C 24050001 */  li    $a1, 1
/* 092E50 7F060460 15E00003 */  bnez  $t7, .L7F060470
/* 092E54 7F060464 2446009C */   addiu $a2, $v0, 0x9c
/* 092E58 7F060468 0C00209A */  jal   sndPlaySfx
/* 092E5C 7F06046C 8C846900 */   lw    $a0, %lo(g_musicSfxBufferPtr)($a0)
.L7F060470:
/* 092E60 7F060470 8FBF001C */  lw    $ra, 0x1c($sp)
.L7F060474:
/* 092E64 7F060474 27BD0108 */  addiu $sp, $sp, 0x108
/* 092E68 7F060478 03E00008 */  jr    $ra
/* 092E6C 7F06047C 00000000 */   nop
)
#endif
#endif





#ifdef NONMATCHING
void handles_firing_or_throwing_weapon_in_hand(void) {
    Mtxf *sp2A4;
    Mtxf  sp264;
    f32   sp224;
    Mtxf  sp1E4;
    Mtxf  sp1A4;
    void *sp1A0;
    f32   sp19C;
    f32   sp198;
    f32   sp194;
    Mtxf  sp154;
    Mtxf  sp114;
    s32  *sp10C;
    f32  *sp108;
    s32   sp100;
    s32   spFC;
    void *spF8;
    f32   spE8;
    f32   spE4;
    f32   spE0;
    f32   spD4;
    f32   spC8;
    ? spB8;
    ? spAC;
    ? spA0;
    f32  *sp9C;
    f32  *sp94;
    f32   sp8C;
    f32   sp88;
    f32   sp84;
    f32   sp80;
    f32   sp7C;
    f32  *sp70;
    s32   sp6C;
    void *sp68;
    f32  *sp64;
    s32   sp60;
    s32   sp5C;
    f32   sp50;
    f32   sp4C;
    f32   sp48;
    Mtxf *sp44;
    Mtxf *sp40;
    void *sp3C;
    void *sp38; // compiler-managed
    Mtxf *temp_a0;
    Mtxf *temp_a0_2;
    Mtxf *temp_a1;
    Mtxf *temp_t0;
    Mtxf *temp_t1;
    Mtxf *temp_v0_8;
    Mtxf *var_a0;
    Mtxf *var_a2;
    f32  *temp_a0_4;
    f32  *temp_a0_5;
    f32   temp_f10;
    f32   temp_f10_2;
    f32   temp_f16;
    f32   temp_f16_2;
    f32   var_f10;
    f32   var_f10_2;
    f32   var_f12;
    f32   var_f16;
    f32   var_f4;
    f32   var_f6;
    f32   var_f8;
    f32   var_f8_2;
    s32  *temp_v0_17;
    s32  *temp_v0_18;
    s32   temp_a1_2;
    s32   temp_a1_3;
    s32   temp_v0;
    s32   temp_v0_10;
    s32   temp_v0_11;
    s32   temp_v0_12;
    s32   temp_v0_13;
    s32   temp_v0_2;
    s32   temp_v0_3;
    s32   temp_v0_4;
    s32   temp_v0_5;
    s32   temp_v0_6;
    s32   var_v1;
    s32   var_v1_2;
    s32   var_v1_3;
    void *temp_a0_3;
    void *temp_a0_6;
    void *temp_a0_7;
    void *temp_s0;
    void *temp_t2;
    void *temp_t3;
    void *temp_v0_14;
    void *temp_v0_15;
    void *temp_v0_16;
    void *temp_v0_7;
    void *temp_v0_9;
    void *temp_v1;
    void *temp_v1_2;
    void *temp_v1_3;
    void *temp_v1_4;
    void *temp_v1_5;
    void *temp_v1_6;
    void *var_v0;
    void *var_v0_2;

    sp194.unk0 = D_80035C40.unk0;
    sp194.unk4 = D_80035C40.unk4;
    sp194.unk8 = D_80035C40.unk8;
    sp10C      = NULL;
    sp108      = NULL;
    temp_s0    = g_CurrentPlayer + (arg0 * 0x3A8) + 0x870;
    temp_v0    = get_item_in_hand_or_watch_menu();
    spFC       = temp_v0;
    spF8       = get_ptr_item_statistics(temp_v0);
    if (arg0 == 0)
    {
        if (bondwalkItemCheckBitflags(get_item_in_hand_or_watch_menu(1), 0x800) != 0)
        {
            temp_s0->unk1C4 = temp_s0->unk1C4 + ((2.0f * g_GlobalTimerDelta) / 240.0f);
            if (temp_s0->unk1C4 > 2.0f)
            {
                temp_s0->unk1C4 = 2.0f;
            }
        }
        else
        {
            temp_s0->unk1C4 = temp_s0->unk1C4 - ((2.0f * g_GlobalTimerDelta) / 240.0f);
            if (temp_s0->unk1C4 < 0.0f)
            {
                temp_s0->unk1C4 = 0.0f;
            }
        }
    }
    else if (bondwalkItemCheckBitflags(get_item_in_hand_or_watch_menu(0), 0x800) != 0)
    {
        temp_s0->unk1C4 = temp_s0->unk1C4 - ((2.0f * g_GlobalTimerDelta) / 240.0f);
        if (temp_s0->unk1C4 < -2.0f)
        {
            temp_s0->unk1C4 = -2.0f;
        }
    }
    else
    {
        temp_s0->unk1C4 = temp_s0->unk1C4 + ((2.0f * g_GlobalTimerDelta) / 240.0f);
        if (temp_s0->unk1C4 > 0.0f)
        {
            temp_s0->unk1C4 = 0.0f;
        }
    }
    spE0.unk0 = D_80035C4C.unk0;
    spE0.unk4 = D_80035C4C.unk4;
    spE0.unk8 = D_80035C4C.unk8;
    spD4.unk0 = D_80035C58.unk0;
    spD4.unk4 = D_80035C58.unk4;
    spD4.unk8 = D_80035C58.unk8;
    spC8.unk0 = D_80035C64.unk0;
    spC8.unk4 = D_80035C64.unk4;
    spC8.unk8 = D_80035C64.unk8;
    temp_v0_2 = temp_s0->unk198;
    temp_t0   = temp_s0 + (((temp_v0_2 + 3) % 4) * 0xC);
    sp44      = temp_t0;
    temp_t1   = temp_s0 + (temp_v0_2 * 0xC);
    sp40      = temp_t1;
    temp_t2   = temp_s0 + (((temp_v0_2 + 1) % 4) * 0xC);
    sp3C      = temp_t2;
    temp_t3   = temp_s0 + (((temp_v0_2 + 2) % 4) * 0xC);
    sp38      = temp_t3;
    sub_GAME_7F05AEFC(temp_t0 + 0x108, temp_t1 + 0x108, temp_t2 + 0x108, temp_t3 + 0x108, temp_s0->unk19C, &spE0);
    sub_GAME_7F05AEFC(sp44 + 0x138, sp40 + 0x138, sp3C + 0x138, sp38 + 0x138, temp_s0->unk19C, &spD4);
    sub_GAME_7F05AEFC(sp44 + 0x168, sp40 + 0x168, sp3C + 0x168, sp38 + 0x168, temp_s0->unk19C, &spC8);
    temp_f16 = spE0 * g_CurrentPlayer->unkFC0;
    spE0     = temp_f16;
    temp_f10 = spE4 * g_CurrentPlayer->unkFC0;
    spE4     = temp_f10;
    spE8 *= g_CurrentPlayer->unkFC0;
    spE0   = temp_f16 + temp_s0->unk1AC;
    spE4   = temp_f10 + temp_s0->unk1B0;
    var_v1 = 0;
    spE0 += sub_GAME_7F05DCB8(arg0);
    if (g_ClockTimer > 0)
    {
        do
        {
            var_v1 += 1;
            temp_s0->unkE4  = spE0 + (0.95f * temp_s0->unkE4);
            temp_s0->unkE8  = spE4 + (0.95f * temp_s0->unkE8);
            temp_s0->unkEC  = spE8 + (0.95f * temp_s0->unkEC);
            temp_s0->unkF0  = spD4 + (0.95f * temp_s0->unkF0);
            temp_s0->unkF4  = spD8 + (0.95f * temp_s0->unkF4);
            temp_s0->unkF8  = spDC + (0.95f * temp_s0->unkF8);
            temp_s0->unkFC  = spC8 + (0.95f * temp_s0->unkFC);
            temp_s0->unk100 = spCC + (0.95f * temp_s0->unk100);
            temp_s0->unk104 = spD0 + (0.95f * temp_s0->unk104);
        } while (var_v1 < g_ClockTimer);
    }
    temp_s0->unkC0 = temp_s0->unkE4 * 0.050000012f;
    temp_s0->unkC4 = temp_s0->unkE8 * 0.050000012f;
    temp_s0->unkC8 = temp_s0->unkEC * 0.050000012f;
    temp_s0->unkCC = temp_s0->unkF0 * 0.050000012f;
    temp_s0->unkD0 = temp_s0->unkF4 * 0.050000012f;
    temp_s0->unkD4 = temp_s0->unkF8 * 0.050000012f;
    temp_s0->unkD8 = temp_s0->unkFC * 0.050000012f;
    temp_s0->unkDC = temp_s0->unk100 * 0.050000012f;
    temp_s0->unkE0 = temp_s0->unk104 * 0.050000012f;
    if (arg0 == 0)
    {
        sp194 = temp_s0->unk1B8 + (gunSetHorizontalOffset(arg0) + temp_s0->unkC0);
    }
    else
    {
        sp194 = (gunSetHorizontalOffset(arg0) + temp_s0->unkC0) - temp_s0->unk1B8;
    }
    sp198 = temp_s0->unk1BC + (spF8->unk8 + temp_s0->unkC4);
    sp19C = temp_s0->unk1C0 + (spF8->unkC + temp_s0->unkC8);
    if ((spFC == 0x19) || (spFC == 0x1E) || (spFC == 0x17))
    {
        sp198 += g_CurrentPlayer->unkA0 / -100.0f;
        sp19C += (3.0f * g_CurrentPlayer->unkA0) / -100.0f;
        if ((spFC == 0x19) && ((cur_player_get_screen_setting(spFC) == 1) || (cur_player_get_screen_setting() == 2) || (get_screen_ratio() == 1)))
        {
            sp198 -= 3.0f;
        }
    }
    else
    {
        if (spFC == 0x1F)
        {
            sp198 += (2.5f * g_CurrentPlayer->unkA0) / -100.0f;
            var_f10 = sp19C + ((7.5f * g_CurrentPlayer->unkA0) / -100.0f);
        }
        else
        {
            sp198 += (5.0f * g_CurrentPlayer->unkA0) / -100.0f;
            var_f10 = sp19C + ((15.0f * g_CurrentPlayer->unkA0) / -100.0f);
        }
        sp19C = var_f10;
    }
    if ((temp_s0->unkC != 0) && (bondwalkItemCheckBitflags(spFC, 0x20) != 0))
    {
        if (bondwalkItemCheckBitflags(spFC, 0x40) != 0)
        {
            temp_v0_3 = randomGetNext();
            var_f8    = temp_v0_3;
            if (temp_v0_3 < 0)
            {
                var_f8 += 4294967296.0f;
            }
            sp194 += 0.3f - (var_f8 * 2.3283064e-10f * 0.6f);
        }
        temp_v0_4 = randomGetNext();
        var_f16   = temp_v0_4;
        if (temp_v0_4 < 0)
        {
            var_f16 += 4294967296.0f;
        }
        sp198 += 0.3f - (var_f16 * 2.3283064e-10f * 0.6f);
        temp_v0_5 = randomGetNext();
        var_f4    = temp_v0_5;
        if (temp_v0_5 < 0)
        {
            var_f4 += 4294967296.0f;
        }
        sp19C += 0.3f - (var_f4 * 2.3283064e-10f * 0.6f);
    }
    sp48 = getPlayer_c_screenwidth();
    sp4C = getPlayer_c_screenwidth();
    sp194 += (((g_CurrentPlayer->unkFFC - getPlayer_c_screenleft()) - (sp4C * 0.5f)) * spF8->unk18) / (sp48 * 0.5f);
    sp50 = getPlayer_c_screentop();
    if ((getPlayer_c_screenheight() * 0.5f) < (g_CurrentPlayer->unk1000 - sp50))
    {
        sp48 = getPlayer_c_screenheight();
        sp4C = getPlayer_c_screenheight();
        sp198 -= (((g_CurrentPlayer->unk1000 - getPlayer_c_screentop()) - (sp4C * 0.5f)) * spF8->unk14) / (sp48 * 0.5f);
    }
    else
    {
        sp48 = getPlayer_c_screenheight();
        sp4C = getPlayer_c_screenheight();
        sp198 -= (((g_CurrentPlayer->unk1000 - getPlayer_c_screentop()) - (sp4C * 0.5f)) * spF8->unk10) / (sp48 * 0.5f);
    }
    sub_GAME_7F05C614();
    matrix_4x4_set_identity(&sp154);
    if ((spFC == 0x1E) || (spFC == 0x17))
    {
        spB8.unk0 = D_80035C70.unk0;
        spB8.unk4 = D_80035C70.unk4;
        spB8.unk8 = D_80035C70.unk8;
        matrix_4x4_set_rotation_around_xyz(&spB8, &sp1A4);
        matrix_4x4_multiply_homogeneous_in_place(&sp1A4, &sp154);
    }
    else if (spFC == 0x1F)
    {
        spAC.unk0 = D_80035C7C.unk0;
        spAC.unk4 = D_80035C7C.unk4;
        spAC.unk8 = D_80035C7C.unk8;
        matrix_4x4_set_rotation_around_xyz(&spAC, &sp1A4);
        matrix_4x4_multiply_homogeneous_in_place(&sp1A4, &sp154);
    }
    else if ((spFC == 1) && (g_CurrentPlayer->unk2A38 == 0x11))
    {
        spA0.unk0 = D_80035C88.unk0;
        spA0.unk4 = D_80035C88.unk4;
        spA0.unk8 = D_80035C88.unk8;
        matrix_4x4_set_rotation_around_xyz(&spA0, &sp1A4);
        matrix_4x4_multiply_homogeneous_in_place(&sp1A4, &sp154);
        sp194 += -2.5f;
        sp198 += 27.8f;
        sp19C += 2.0f;
    }
    if (temp_s0->unkBC != 0)
    {
        sp194 += temp_s0->unkAC;
        sp198 += temp_s0->unkB0;
        sp19C += temp_s0->unkB4;
        matrix_4x4_multiply_homogeneous_in_place(temp_s0 + 0x7C, &sp154);
        sp154.m[3][0] = 0.0f;
        sp154.m[3][1] = 0.0f;
        sp154.m[3][2] = 0.0f;
    }
    else
    {
        temp_s0->unk78 = 0.0f;
        temp_s0->unk6C = 0.0f;
        temp_s0->unk70 = 0.0f;
        temp_s0->unk74 = 0.0f;
    }
    matrix_4x4_7F059908(&sp1A4, 0.0f, 0.0f, 0.0f, temp_s0->unkCC, temp_s0->unkD0, temp_s0->unkD4, temp_s0->unkD8, temp_s0->unkDC, temp_s0->unkE0);
    matrix_4x4_multiply_homogeneous_in_place(&sp1A4, &sp154);
    matrix_4x4_align(&sp1A4, 0.0f, sp194 - temp_s0->unk1C8, sp198 - temp_s0->unk1CC, sp19C - temp_s0->unk1D0);
    matrix_4x4_multiply_homogeneous_in_place(&sp1A4, &sp154);
    matrix_4x4_copy(&sp154, &sp264);
    matrix_4x4_set_position(&sp194, &sp264);
    temp_a1 = temp_s0 + 0x228;
    sp44    = temp_a1;
    matrix_4x4_copy(&sp264, temp_a1);
    temp_a0 = temp_s0 + 0x268;
    sp40    = temp_a0;
    matrix_4x4_copy(temp_a0, temp_s0 + 0x2A8);
    matrix_4x4_multiply_homogeneous(currentPlayerGetMatrix10D4(), sp44, sp40);
    temp_s0->unkF = 1;
    if ((get_ptr_weapon_model_header_line(spFC) == 0) || (bondwalkItemCheckBitflags(spFC, 0x800) == 0) || (bondwalkItemCheckBitflags(spFC, 0x2000) != 0) || (temp_v0_6 = temp_s0->unk24, (temp_v0_6 == 6)) || (temp_v0_6 == 7) || (Gun_hand_without_item(arg0) == 0) || (get_itemtype_in_hand(arg0) == 0))
    {
        temp_s0->unkF = 0;
    }
    if ((temp_s0->unk2C <= 0) && (bondwalkItemCheckBitflags(spFC, 2) != 0))
    {
        temp_s0->unkF = 0;
    }
    if (temp_s0->unkF != 0)
    {
        temp_v0_7 = g_CurrentPlayer + (arg0 << 5);
        sp1A0     = temp_v0_7 + 0x810;
        sp100     = 0;
        temp_v0_8 = dynAllocate(temp_v0_7->unk81E << 6);
        sp2A4     = temp_v0_8;
        var_v1_2  = sp100;
        var_a0    = temp_v0_8;
        if (sp1A0->unkE > 0)
        {
            do
            {
                sp100 = var_v1_2;
                sp44  = var_a0;
                matrix_4x4_set_identity(var_a0);
                var_v1_2 += 1;
                var_a0 += 0x40;
            } while (var_v1_2 < sp1A0->unkE);
        }
        modelCalculateRwDataLen(sp1A0);
    #ifdef DEBUG
        if (sp1a0->unk14 >= 32) ossyncprintf("Increase GUNSAVESIZE to %d!!! ", sp1a0->unk14);
    #endif
        temp_a0_2 = temp_s0 + 0x2F8;
        sp44      = temp_a0_2;
        modelInit(temp_a0_2, sp1A0, temp_s0 + 0x318);
        sub_GAME_7F05E978(temp_a0_2, 1);
        sub_GAME_7F05EA94(temp_a0_2, temp_s0->unkE);
        temp_v0_9 = sp1A0->unk8;
        temp_a0_3 = temp_v0_9->unk4;
        if (temp_a0_3 != NULL)
        {
            sp10C = temp_s0 + (temp_a0_3->unk4->unk4 * 4) + 0x318;
        }
        temp_v1 = temp_v0_9->unkC;
        if (temp_v1 != NULL)
        {
            sp108 = temp_v1->unk4;
        }
        temp_s0->unk304 = sp2A4;
        if ((bondwalkItemCheckBitflags(spFC, 0x400) != 0) && (arg0 == 1))
        {
            matrix_column_1_scalar_multiply(0xBF800000, &sp264);
        }
        matrix_scalar_multiply(0.10000001f, &sp264);
        matrix_4x4_copy(&sp264, sp2A4);
        if (&skeleton_gun_revolver == sp1A0->unk4)
        {
            var_v0    = sp1A0->unk8;
            temp_v1_2 = var_v0->unk10;
            if (temp_v1_2 != NULL)
            {
                var_f12   = 0.0f;
                temp_a0_4 = temp_v1_2->unk4;
                if (spFC == 0x12)
                {
                    if (temp_s0->unk24 == 1)
                    {
                        var_f12 = (((temp_s0->unk20 - (temp_s0->unk2C * 6)) + 0x1E) * 6.2831855f) / 36.0f;
                    }
                    else
                    {
                        var_f12 = ((6 - temp_s0->unk2C) * 6.2831855f) / 6.0f;
                    }
                }
                else if (temp_s0->unk24 == 1)
                {
                    temp_v0_10 = temp_s0->unk20;
                    if (temp_v0_10 < 6)
                    {
                        var_f12 = (temp_v0_10 * 6.2831855f) / 36.0f;
                    }
                }
                sp9C = temp_a0_4;
                matrix_4x4_set_rotation_around_z(var_f12, temp_a0_4, &sp1A4);
                matrix_4x4_set_position(sp9C, &sp1A4);
                matrix_4x4_multiply(&sp264, &sp1A4, sp2A4 + 0xC0);
                var_v0 = sp1A0->unk8;
            }
            temp_v1_3 = var_v0->unk14;
            if (temp_v1_3 != NULL)
            {
                temp_a0_5 = temp_v1_3->unk4;
                if (temp_s0->unk24 == 1)
                {
                    temp_v0_11 = temp_s0->unk20;
                    if (temp_v0_11 < 3)
                    {
                        var_f6 = 2.0f * (-temp_v0_11 * 0.5235988f);
                    }
                    else
                    {
                        var_f6 = 2.0f * (-(6 - temp_v0_11) * 0.5235988f);
                    }
                    sp94 = temp_a0_5;
                    matrix_4x4_set_rotation_around_x(var_f6 / 6.0f, temp_a0_5, &sp1A4);
                    matrix_4x4_set_position(sp94, &sp1A4);
                }
                else
                {
                    matrix_4x4_set_identity_and_position(temp_a0_5, &sp1A4);
                }
                matrix_4x4_multiply(&sp264, &sp1A4, sp2A4 + 0x100);
            }
        }
        if (sp10C != NULL)
        {
            *sp10C = 0;
        }
        if (sp108 != NULL)
        {
            temp_v0_12 = randomGetNext();
            var_f8_2   = temp_v0_12;
            if (temp_v0_12 < 0)
            {
                var_f8_2 += 4294967296.0f;
            }
            sp80 = (var_f8_2 * 2.3283064e-10f * 0.25f) + 1.0f;
            sp7C = spF8->unk0;
            if (bondwalkItemCheckBitflags(spFC, 1) != 0)
            {
                temp_v0_13 = randomGetNext(sp108);
                var_f10_2  = temp_v0_13;
                if (temp_v0_13 < 0)
                {
                    var_f10_2 += 4294967296.0f;
                }
                matrix_4x4_set_rotation_around_z(var_f10_2 * 2.3283064e-10f * 6.2831855f, &sp224);
                matrix_4x4_set_position(sp108, &sp224);
            }
            else
            {
                matrix_4x4_set_identity_and_position(sp108, &sp224);
            }
            matrix_scalar_multiply(sp80, &sp224);
            matrix_column_3_scalar_multiply(sp7C, &sp224);
            matrix_4x4_multiply_in_place(&sp264, &sp224);
            matrix_4x4_copy(&sp224, sp2A4 + 0x40);
            temp_s0->unk2E8 = sp254;
            temp_s0->unk2EC = sp258;
            temp_s0->unk2F0 = sp25C;
            mtx4TransformVecInPlace(currentPlayerGetMatrix10D4(), temp_s0 + 0x2E8);
            temp_s0->unk2F4 = -sp25C;
            if (temp_s0->unkD != 0)
            {
                if (sp10C != NULL)
                {
                    *sp10C = 1;
                }
                temp_v1_4 = sp1A0->unk8->unk8;
                if (temp_v1_4 != NULL)
                {
                    temp_v0_14 = temp_v1_4->unk4;
                    sp84       = sp254 + ((temp_v0_14->unk0 * sp224) + (temp_v0_14->unk4 * sp234) + (temp_v0_14->unk8 * sp244));
                    temp_f16_2 = sp258 + ((temp_v0_14->unk0 * sp228) + (temp_v0_14->unk4 * sp238) + (temp_v0_14->unk8 * sp248));
                    sp88       = temp_f16_2;
                    temp_f10_2 = sp25C + ((temp_v0_14->unk0 * sp22C) + (temp_v0_14->unk4 * sp23C) + (temp_v0_14->unk8 * sp24C));
                    sp8C       = temp_f10_2;
                    matrix_4x4_align(&sp1E4, randomGetNext() * 2.3283064e-10f * 6.2831855f, -sp84, -temp_f16_2, -temp_f10_2);
                    matrix_scalar_multiply(0.10000001f * sp80, &sp1E4);
                    matrix_4x4_7F059B58(&sp114, 0, sp194 - temp_s0->unk1C8, sp198 - temp_s0->unk1CC, sp19C - temp_s0->unk1D0);
                    matrix_4x4_multiply_in_place(&sp114, sp1E4.m[0]);
                    matrix_row_3_scalar_multiply(sp7C, &sp1E4);
                    matrix_4x4_multiply_in_place(&sp154, sp1E4.m[0]);
                    matrix_4x4_set_position(&sp84, &sp1E4);
                    matrix_4x4_copy(&sp1E4, sp2A4 + 0x80);
                }
                if (&skeleton_gun_kf7 == sp1A0->unk4)
                {
                    temp_v1_5 = sp1A0->unk8->unk10;
                    if (temp_v1_5 != NULL)
                    {
                        temp_v0_15 = temp_v1_5->unk4;
                        sp84       = sp254 + ((temp_v0_15->unk0 * sp224) + (temp_v0_15->unk4 * sp234) + (temp_v0_15->unk8 * sp244));
                        sp88       = sp258 + ((temp_v0_15->unk0 * sp228) + (temp_v0_15->unk4 * sp238) + (temp_v0_15->unk8 * sp248));
                        sp40       = sp2A4 + 0xC0;
                        sp8C       = sp25C + ((temp_v0_15->unk0 * sp22C) + (temp_v0_15->unk4 * sp23C) + (temp_v0_15->unk8 * sp24C));
                        sp38       = 0.10000001f * sp80;
                        matrix_4x4_align(&sp1E4, randomGetNext() * 2.3283064e-10f * 6.2831855f, -sp84, -sp88, -sp8C);
                        matrix_scalar_multiply(sp38, &sp1E4);
                        matrix_4x4_7F059B58(&sp114, 0, sp194 - temp_s0->unk1C8, sp198 - temp_s0->unk1CC, sp19C - temp_s0->unk1D0);
                        matrix_4x4_multiply_in_place(&sp114, sp1E4.m[0]);
                        matrix_row_3_scalar_multiply(sp7C, &sp1E4);
                        matrix_4x4_multiply_in_place(&sp154, sp1E4.m[0]);
                        matrix_4x4_set_position(&sp84, &sp1E4);
                        matrix_4x4_copy(&sp1E4, sp40);
                    }
                }
            }
            var_v0_2 = sp1A0->unk8;
        }
        else
        {
            temp_s0->unk2E8 = temp_s0->unk298;
            temp_s0->unk2F4 = -temp_s0->unk260;
            temp_s0->unk2EC = temp_s0->unk29C;
            temp_s0->unk2F0 = temp_s0->unk2A0;
            var_v0_2        = sp1A0->unk8;
        }
        temp_a0_6 = var_v0_2->unk18;
        if (temp_a0_6 != NULL)
        {
            sp70 = temp_a0_6->unk4;
            sp6C = modelFindNodeMtxIndex(temp_a0_6, 0);
            sub_GAME_7F05E6B4(arg0, temp_s0->unk10);
            if ((sp1A0->unkC >= 0x1D) && (temp_v1_6 = sp1A0->unk8->unk70, (temp_v1_6 != NULL)))
            {
                temp_v0_16 = temp_v1_6->unk4;
                sp68       = temp_v0_16;
                guRotateF(&sp1A4, (((temp_s0->unk214 + 6.2831855f) - get_value_if_watch_is_on_hand_or_not(arg0)) * 360.0f) / 6.2831855f, temp_v0_16->unk0 - temp_v0_16->unkC, temp_v0_16->unk4 - temp_v0_16->unk10, temp_v0_16->unk8 - temp_v0_16->unk14);
                matrix_4x4_set_position(sp70, &sp1A4);
            }
            else
            {
                matrix_4x4_set_position_and_rotation_around_y(sp70, temp_s0->unk214, &sp1A4);
            }
            matrix_4x4_multiply_homogeneous(&sp264, &sp1A4, &sp2A4[sp6C]);
        }
        if (sp1A0->unkC >= 0x1E)
        {
            seems_to_load_cuff_microcode(sp44, sp1A0, 0x1D);
        }
        temp_a0_7 = sp1A0->unk8->unk1C;
        if (temp_a0_7 != NULL)
        {
            sp64 = temp_a0_7->unk4;
            sp60 = modelFindNodeMtxIndex(temp_a0_7, 0);
            sub_GAME_7F05E83C(arg0);
            matrix_4x4_set_identity_and_position(sp64, &sp1A4);
            sp1A4.m[3][2] -= temp_s0->unk218;
            matrix_4x4_multiply(&sp264, &sp1A4, &sp2A4[sp60]);
        }
        var_v1_3 = 0;
        var_a2   = NULL;
        if (sp1A0->unkC >= 0x13)
        {
            do
            {
                temp_a1_2 = (sp1A0->unk8 + var_a2)->unk48;
                if (temp_a1_2 != 0)
                {
                    sp5C       = var_v1_3;
                    sp40       = var_a2;
                    temp_v0_17 = modelGetNodeRwData(sp44, temp_a1_2, var_a2, 5);
                    if (temp_v0_17 != NULL)
                    {
                        *temp_v0_17 = (temp_s0->unk34 < (5 - var_v1_3)) ^ 1;
                    }
                }
                temp_a1_3 = (sp1A0->unk8 + var_a2)->unk5C;
                if (temp_a1_3 != 0)
                {
                    sp5C       = var_v1_3;
                    sp40       = var_a2;
                    temp_v0_18 = modelGetNodeRwData(sp44, temp_a1_3, var_a2, 5);
                    if (temp_v0_18 != NULL)
                    {
                        *temp_v0_18 = (temp_s0->unk34 < (5 - var_v1_3)) ^ 1;
                    }
                }
                var_v1_3 += 1;
                var_a2 += 4;
            } while (var_v1_3 != 5);
        }
        sub_GAME_7F06EFC4(sp44);
        if (temp_s0->unkC != 0)
        {
            switch (spFC)
            {
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                    sub_GAME_7F061BF4(arg0);
                    temp_s0->unk30 = temp_s0->unk30 + 1;
                    break;
                case 22:
                case 23:
                    sub_GAME_7F061BF4(arg0);
                    break;
            }
        }
    }
    if (spFC == 0x19)
    {
        sub_GAME_7F05F928(arg0);
    }
    if (temp_s0->unkC != 0)
    {
        sub_GAME_7F068508(arg0, bondviewGetPlayerStanHeight(g_CurrentPlayer));
        if (spFC == 0x18)
        {
            sub_GAME_7F05F73C(arg0);
            return;
        }
        if (spFC == 0x1A)
        {
            generate_player_thrown_grenade(arg0);
            return;
        }
        if (spFC == 0x19)
        {
            gunFireTankShell(arg0);
            return;
        }
        if (spFC == 3)
        {
            generate_player_thrown_knife(arg0);
            return;
        }
        if ((spFC == 0x1D) || (spFC == 0x1C) || (spFC == 0x1B) || (spFC == 0x21) || (spFC == 0x2F) || (spFC == 0x30) || (spFC == 0x3D) || (spFC == 0x22))
        {
            generate_player_thrown_object(arg0);
            return;
        }
        if (spFC == 0x23)
        {
            sub_GAME_7F05F73C(arg0);
            return;
        }
        if (spFC == 0x24)
        {
            sub_GAME_7F05F73C(arg0);
        }
    }
}
#else

#ifdef VERSION_US
GLOBAL_ASM(
.late_rodata
glabel D_80053DE0
.word 0x3f733333 /*0.94999999*/
glabel D_80053DE4
.word 0x3d4cccd0 /*0.050000012*/
glabel D_80053DE8
.word 0x3f19999a /*0.60000002*/
glabel D_80053DEC
.word 0x3e99999a /*0.30000001*/
glabel D_80053DF0
.word 0x3f19999a /*0.60000002*/
glabel D_80053DF4
.word 0x3e99999a /*0.30000001*/
glabel D_80053DF8
.word 0x3f19999a /*0.60000002*/
glabel D_80053DFC
.word 0x3e99999a /*0.30000001*/
glabel D_80053E00
.word 0x41de6666 /*27.799999*/
glabel D_80053E04
.word 0x3dccccce /*0.10000001*/
glabel D_80053E08
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E0C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E10
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E14
.word 0x3f060a92 /*0.52359879*/
glabel D_80053E18
.word 0x3f060a92 /*0.52359879*/
glabel D_80053E1C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E20
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E24
.word 0x3dccccce /*0.10000001*/
glabel D_80053E28
.word 0x3dccccce /*0.10000001*/
glabel D_80053E2C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E30
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E34
.word 0x40c90fdb /*6.2831855*/

/*D:80053E38*/
glabel jpt_weapon_bullet_type
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_shotgun_mine
.word weapon_bullet_type_shotgun_mine
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_none
.word weapon_bullet_type_none
.size jpt_weapon_bullet_type, . - jpt_weapon_bullet_type

.text
glabel handles_firing_or_throwing_weapon_in_hand
/* 094AF8 7F05FFC8 27BDFD58 */  addiu $sp, $sp, -0x2a8
/* 094AFC 7F05FFCC 3C0F8003 */  lui   $t7, %hi(D_80035C40)
/* 094B00 7F05FFD0 AFBF0034 */  sw    $ra, 0x34($sp)
/* 094B04 7F05FFD4 AFB00030 */  sw    $s0, 0x30($sp)
/* 094B08 7F05FFD8 25EF5C40 */  addiu $t7, %lo(D_80035C40) # addiu $t7, $t7, 0x5c40
/* 094B0C 7F05FFDC 8DE10000 */  lw    $at, ($t7)
/* 094B10 7F05FFE0 27AE0194 */  addiu $t6, $sp, 0x194
/* 094B14 7F05FFE4 8DED0004 */  lw    $t5, 4($t7)
/* 094B18 7F05FFE8 ADC10000 */  sw    $at, ($t6)
/* 094B1C 7F05FFEC 8DE10008 */  lw    $at, 8($t7)
/* 094B20 7F05FFF0 0004C0C0 */  sll   $t8, $a0, 3
/* 094B24 7F05FFF4 0304C023 */  subu  $t8, $t8, $a0
/* 094B28 7F05FFF8 0018C080 */  sll   $t8, $t8, 2
/* 094B2C 7F05FFFC 0304C021 */  addu  $t8, $t8, $a0
/* 094B30 7F060000 3C198008 */  lui   $t9, %hi(g_CurrentPlayer)
/* 094B34 7F060004 ADCD0004 */  sw    $t5, 4($t6)
/* 094B38 7F060008 ADC10008 */  sw    $at, 8($t6)
/* 094B3C 7F06000C 8F39A0B0 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 094B40 7F060010 0018C080 */  sll   $t8, $t8, 2
/* 094B44 7F060014 0304C021 */  addu  $t8, $t8, $a0
/* 094B48 7F060018 0018C0C0 */  sll   $t8, $t8, 3
/* 094B4C 7F06001C 03388021 */  addu  $s0, $t9, $t8
/* 094B50 7F060020 AFA0010C */  sw    $zero, 0x10c($sp)
/* 094B54 7F060024 AFA00108 */  sw    $zero, 0x108($sp)
/* 094B58 7F060028 26100870 */  addiu $s0, $s0, 0x870
/* 094B5C 7F06002C 0FC17691 */  jal   get_item_in_hand_or_watch_menu
/* 094B60 7F060030 AFA402A8 */   sw    $a0, 0x2a8($sp)
/* 094B64 7F060034 AFA200FC */  sw    $v0, 0xfc($sp)
/* 094B68 7F060038 0FC1722D */  jal   get_ptr_item_statistics
/* 094B6C 7F06003C 00402025 */   move  $a0, $v0
/* 094B70 7F060040 8FAE02A8 */  lw    $t6, 0x2a8($sp)
/* 094B74 7F060044 AFA200F8 */  sw    $v0, 0xf8($sp)
/* 094B78 7F060048 15C0002D */  bnez  $t6, .L7F060100
/* 094B7C 7F06004C 00000000 */   nop
/* 094B80 7F060050 0FC17691 */  jal   get_item_in_hand_or_watch_menu
/* 094B84 7F060054 24040001 */   li    $a0, 1
/* 094B88 7F060058 00402025 */  move  $a0, $v0
/* 094B8C 7F06005C 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 094B90 7F060060 24050800 */   li    $a1, 2048
/* 094B94 7F060064 10400015 */  beqz  $v0, .L7F0600BC
/* 094B98 7F060068 3C018005 */   lui   $at, %hi(g_GlobalTimerDelta)
/* 094B9C 7F06006C 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta)
/* 094BA0 7F060070 C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 094BA4 7F060074 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 094BA8 7F060078 44813000 */  mtc1  $at, $f6
/* 094BAC 7F06007C 46000100 */  add.s $f4, $f0, $f0
/* 094BB0 7F060080 C60A01C4 */  lwc1  $f10, 0x1c4($s0)
/* 094BB4 7F060084 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 094BB8 7F060088 44819000 */  mtc1  $at, $f18
/* 094BBC 7F06008C 46062203 */  div.s $f8, $f4, $f6
/* 094BC0 7F060090 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 094BC4 7F060094 46085400 */  add.s $f16, $f10, $f8
/* 094BC8 7F060098 E61001C4 */  swc1  $f16, 0x1c4($s0)
/* 094BCC 7F06009C C60401C4 */  lwc1  $f4, 0x1c4($s0)
/* 094BD0 7F0600A0 4604903C */  c.lt.s $f18, $f4
/* 094BD4 7F0600A4 00000000 */  nop
/* 094BD8 7F0600A8 4500003F */  bc1f  .L7F0601A8
/* 094BDC 7F0600AC 00000000 */   nop
/* 094BE0 7F0600B0 44813000 */  mtc1  $at, $f6
/* 094BE4 7F0600B4 1000003C */  b     .L7F0601A8
/* 094BE8 7F0600B8 E60601C4 */   swc1  $f6, 0x1c4($s0)
.L7F0600BC:
/* 094BEC 7F0600BC C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 094BF0 7F0600C0 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 094BF4 7F0600C4 44814000 */  mtc1  $at, $f8
/* 094BF8 7F0600C8 46000280 */  add.s $f10, $f0, $f0
/* 094BFC 7F0600CC C61201C4 */  lwc1  $f18, 0x1c4($s0)
/* 094C00 7F0600D0 46085403 */  div.s $f16, $f10, $f8
/* 094C04 7F0600D4 44805000 */  mtc1  $zero, $f10
/* 094C08 7F0600D8 46109101 */  sub.s $f4, $f18, $f16
/* 094C0C 7F0600DC E60401C4 */  swc1  $f4, 0x1c4($s0)
/* 094C10 7F0600E0 C60601C4 */  lwc1  $f6, 0x1c4($s0)
/* 094C14 7F0600E4 460A303C */  c.lt.s $f6, $f10
/* 094C18 7F0600E8 00000000 */  nop
/* 094C1C 7F0600EC 4500002E */  bc1f  .L7F0601A8
/* 094C20 7F0600F0 00000000 */   nop
/* 094C24 7F0600F4 44804000 */  mtc1  $zero, $f8
/* 094C28 7F0600F8 1000002B */  b     .L7F0601A8
/* 094C2C 7F0600FC E60801C4 */   swc1  $f8, 0x1c4($s0)
.L7F060100:
/* 094C30 7F060100 0FC17691 */  jal   get_item_in_hand_or_watch_menu
/* 094C34 7F060104 00002025 */   move  $a0, $zero
/* 094C38 7F060108 00402025 */  move  $a0, $v0
/* 094C3C 7F06010C 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 094C40 7F060110 24050800 */   li    $a1, 2048
/* 094C44 7F060114 10400013 */  beqz  $v0, .L7F060164
/* 094C48 7F060118 3C018005 */   lui   $at, %hi(g_GlobalTimerDelta)
/* 094C4C 7F06011C 3C01C000 */  li    $at, 0xC0000000 # -2.000000
/* 094C50 7F060120 44811000 */  mtc1  $at, $f2
/* 094C54 7F060124 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta)
/* 094C58 7F060128 C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 094C5C 7F06012C 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 094C60 7F060130 44818000 */  mtc1  $at, $f16
/* 094C64 7F060134 46000480 */  add.s $f18, $f0, $f0
/* 094C68 7F060138 C60601C4 */  lwc1  $f6, 0x1c4($s0)
/* 094C6C 7F06013C 46109103 */  div.s $f4, $f18, $f16
/* 094C70 7F060140 46043281 */  sub.s $f10, $f6, $f4
/* 094C74 7F060144 E60A01C4 */  swc1  $f10, 0x1c4($s0)
/* 094C78 7F060148 C60801C4 */  lwc1  $f8, 0x1c4($s0)
/* 094C7C 7F06014C 4602403C */  c.lt.s $f8, $f2
/* 094C80 7F060150 00000000 */  nop
/* 094C84 7F060154 45000014 */  bc1f  .L7F0601A8
/* 094C88 7F060158 00000000 */   nop
/* 094C8C 7F06015C 10000012 */  b     .L7F0601A8
/* 094C90 7F060160 E60201C4 */   swc1  $f2, 0x1c4($s0)
.L7F060164:
/* 094C94 7F060164 C4208378 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 094C98 7F060168 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 094C9C 7F06016C 44818000 */  mtc1  $at, $f16
/* 094CA0 7F060170 46000480 */  add.s $f18, $f0, $f0
/* 094CA4 7F060174 C60401C4 */  lwc1  $f4, 0x1c4($s0)
/* 094CA8 7F060178 44804000 */  mtc1  $zero, $f8
/* 094CAC 7F06017C 46109183 */  div.s $f6, $f18, $f16
/* 094CB0 7F060180 46062280 */  add.s $f10, $f4, $f6
/* 094CB4 7F060184 E60A01C4 */  swc1  $f10, 0x1c4($s0)
/* 094CB8 7F060188 C61201C4 */  lwc1  $f18, 0x1c4($s0)
/* 094CBC 7F06018C 4612403C */  c.lt.s $f8, $f18
/* 094CC0 7F060190 00000000 */  nop
/* 094CC4 7F060194 45000004 */  bc1f  .L7F0601A8
/* 094CC8 7F060198 00000000 */   nop
/* 094CCC 7F06019C 44808000 */  mtc1  $zero, $f16
/* 094CD0 7F0601A0 00000000 */  nop
/* 094CD4 7F0601A4 E61001C4 */  swc1  $f16, 0x1c4($s0)
.L7F0601A8:
/* 094CD8 7F0601A8 3C0F8003 */  lui   $t7, %hi(D_80035C4C)
/* 094CDC 7F0601AC 25EF5C4C */  addiu $t7, %lo(D_80035C4C) # addiu $t7, $t7, 0x5c4c
/* 094CE0 7F0601B0 8DE10000 */  lw    $at, ($t7)
/* 094CE4 7F0601B4 27AC00E0 */  addiu $t4, $sp, 0xe0
/* 094CE8 7F0601B8 3C0E8003 */  lui   $t6, %hi(D_80035C58)
/* 094CEC 7F0601BC AD810000 */  sw    $at, ($t4)
/* 094CF0 7F0601C0 8DF90004 */  lw    $t9, 4($t7)
/* 094CF4 7F0601C4 25CE5C58 */  addiu $t6, %lo(D_80035C58) # addiu $t6, $t6, 0x5c58
/* 094CF8 7F0601C8 27B800D4 */  addiu $t8, $sp, 0xd4
/* 094CFC 7F0601CC AD990004 */  sw    $t9, 4($t4)
/* 094D00 7F0601D0 8DE10008 */  lw    $at, 8($t7)
/* 094D04 7F0601D4 3C0D8003 */  lui   $t5, %hi(D_80035C64)
/* 094D08 7F0601D8 25AD5C64 */  addiu $t5, %lo(D_80035C64) # addiu $t5, $t5, 0x5c64
/* 094D0C 7F0601DC AD810008 */  sw    $at, 8($t4)
/* 094D10 7F0601E0 8DC10000 */  lw    $at, ($t6)
/* 094D14 7F0601E4 8DCF0004 */  lw    $t7, 4($t6)
/* 094D18 7F0601E8 27B900C8 */  addiu $t9, $sp, 0xc8
/* 094D1C 7F0601EC AF010000 */  sw    $at, ($t8)
/* 094D20 7F0601F0 8DC10008 */  lw    $at, 8($t6)
/* 094D24 7F0601F4 AF0F0004 */  sw    $t7, 4($t8)
/* 094D28 7F0601F8 2403000C */  li    $v1, 12
/* 094D2C 7F0601FC AF010008 */  sw    $at, 8($t8)
/* 094D30 7F060200 8DA10000 */  lw    $at, ($t5)
/* 094D34 7F060204 8DAE0004 */  lw    $t6, 4($t5)
/* 094D38 7F060208 AF210000 */  sw    $at, ($t9)
/* 094D3C 7F06020C 8DA10008 */  lw    $at, 8($t5)
/* 094D40 7F060210 AF2E0004 */  sw    $t6, 4($t9)
/* 094D44 7F060214 AF210008 */  sw    $at, 8($t9)
/* 094D48 7F060218 8E020198 */  lw    $v0, 0x198($s0)
/* 094D4C 7F06021C C604019C */  lwc1  $f4, 0x19c($s0)
/* 094D50 7F060220 AFAC0014 */  sw    $t4, 0x14($sp)
/* 094D54 7F060224 244F0003 */  addiu $t7, $v0, 3
/* 094D58 7F060228 05E10004 */  bgez  $t7, .L7F06023C
/* 094D5C 7F06022C 31F80003 */   andi  $t8, $t7, 3
/* 094D60 7F060230 13000002 */  beqz  $t8, .L7F06023C
/* 094D64 7F060234 00000000 */   nop
/* 094D68 7F060238 2718FFFC */  addiu $t8, $t8, -4
.L7F06023C:
/* 094D6C 7F06023C 03030019 */  multu $t8, $v1
/* 094D70 7F060240 244E0001 */  addiu $t6, $v0, 1
/* 094D74 7F060244 E7A40010 */  swc1  $f4, 0x10($sp)
/* 094D78 7F060248 0000C812 */  mflo  $t9
/* 094D7C 7F06024C 02194021 */  addu  $t0, $s0, $t9
/* 094D80 7F060250 24590002 */  addiu $t9, $v0, 2
/* 094D84 7F060254 00430019 */  multu $v0, $v1
/* 094D88 7F060258 25040108 */  addiu $a0, $t0, 0x108
/* 094D8C 7F06025C AFA80044 */  sw    $t0, 0x44($sp)
/* 094D90 7F060260 00006812 */  mflo  $t5
/* 094D94 7F060264 020D4821 */  addu  $t1, $s0, $t5
/* 094D98 7F060268 25250108 */  addiu $a1, $t1, 0x108
/* 094D9C 7F06026C 05C10004 */  bgez  $t6, .L7F060280
/* 094DA0 7F060270 31CF0003 */   andi  $t7, $t6, 3
/* 094DA4 7F060274 11E00002 */  beqz  $t7, .L7F060280
/* 094DA8 7F060278 00000000 */   nop
/* 094DAC 7F06027C 25EFFFFC */  addiu $t7, $t7, -4
.L7F060280:
/* 094DB0 7F060280 01E30019 */  multu $t7, $v1
/* 094DB4 7F060284 AFA90040 */  sw    $t1, 0x40($sp)
/* 094DB8 7F060288 0000C012 */  mflo  $t8
/* 094DBC 7F06028C 02185021 */  addu  $t2, $s0, $t8
/* 094DC0 7F060290 25460108 */  addiu $a2, $t2, 0x108
/* 094DC4 7F060294 07210004 */  bgez  $t9, .L7F0602A8
/* 094DC8 7F060298 332D0003 */   andi  $t5, $t9, 3
/* 094DCC 7F06029C 11A00002 */  beqz  $t5, .L7F0602A8
/* 094DD0 7F0602A0 00000000 */   nop
/* 094DD4 7F0602A4 25ADFFFC */  addiu $t5, $t5, -4
.L7F0602A8:
/* 094DD8 7F0602A8 01A30019 */  multu $t5, $v1
/* 094DDC 7F0602AC AFAA003C */  sw    $t2, 0x3c($sp)
/* 094DE0 7F0602B0 00007012 */  mflo  $t6
/* 094DE4 7F0602B4 020E5821 */  addu  $t3, $s0, $t6
/* 094DE8 7F0602B8 25670108 */  addiu $a3, $t3, 0x108
/* 094DEC 7F0602BC 0FC16BBF */  jal   sub_GAME_7F05AEFC
/* 094DF0 7F0602C0 AFAB0038 */   sw    $t3, 0x38($sp)
/* 094DF4 7F0602C4 8FA40044 */  lw    $a0, 0x44($sp)
/* 094DF8 7F0602C8 8FA50040 */  lw    $a1, 0x40($sp)
/* 094DFC 7F0602CC 8FA6003C */  lw    $a2, 0x3c($sp)
/* 094E00 7F0602D0 8FA70038 */  lw    $a3, 0x38($sp)
/* 094E04 7F0602D4 C606019C */  lwc1  $f6, 0x19c($s0)
/* 094E08 7F0602D8 27AF00D4 */  addiu $t7, $sp, 0xd4
/* 094E0C 7F0602DC AFAF0014 */  sw    $t7, 0x14($sp)
/* 094E10 7F0602E0 24840138 */  addiu $a0, $a0, 0x138
/* 094E14 7F0602E4 24A50138 */  addiu $a1, $a1, 0x138
/* 094E18 7F0602E8 24C60138 */  addiu $a2, $a2, 0x138
/* 094E1C 7F0602EC 24E70138 */  addiu $a3, $a3, 0x138
/* 094E20 7F0602F0 0FC16BBF */  jal   sub_GAME_7F05AEFC
/* 094E24 7F0602F4 E7A60010 */   swc1  $f6, 0x10($sp)
/* 094E28 7F0602F8 8FA40044 */  lw    $a0, 0x44($sp)
/* 094E2C 7F0602FC 8FA50040 */  lw    $a1, 0x40($sp)
/* 094E30 7F060300 8FA6003C */  lw    $a2, 0x3c($sp)
/* 094E34 7F060304 8FA70038 */  lw    $a3, 0x38($sp)
/* 094E38 7F060308 C60A019C */  lwc1  $f10, 0x19c($s0)
/* 094E3C 7F06030C 27B800C8 */  addiu $t8, $sp, 0xc8
/* 094E40 7F060310 AFB80014 */  sw    $t8, 0x14($sp)
/* 094E44 7F060314 24840168 */  addiu $a0, $a0, 0x168
/* 094E48 7F060318 24A50168 */  addiu $a1, $a1, 0x168
/* 094E4C 7F06031C 24C60168 */  addiu $a2, $a2, 0x168
/* 094E50 7F060320 24E70168 */  addiu $a3, $a3, 0x168
/* 094E54 7F060324 0FC16BBF */  jal   sub_GAME_7F05AEFC
/* 094E58 7F060328 E7AA0010 */   swc1  $f10, 0x10($sp)
/* 094E5C 7F06032C 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 094E60 7F060330 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 094E64 7F060334 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 094E68 7F060338 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 094E6C 7F06033C C4520FC0 */  lwc1  $f18, 0xfc0($v0)
/* 094E70 7F060340 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 094E74 7F060344 46124402 */  mul.s $f16, $f8, $f18
/* 094E78 7F060348 C7A800E8 */  lwc1  $f8, 0xe8($sp)
/* 094E7C 7F06034C E7B000E0 */  swc1  $f16, 0xe0($sp)
/* 094E80 7F060350 C4460FC0 */  lwc1  $f6, 0xfc0($v0)
/* 094E84 7F060354 46062282 */  mul.s $f10, $f4, $f6
/* 094E88 7F060358 E7AA00E4 */  swc1  $f10, 0xe4($sp)
/* 094E8C 7F06035C C4520FC0 */  lwc1  $f18, 0xfc0($v0)
/* 094E90 7F060360 46124102 */  mul.s $f4, $f8, $f18
/* 094E94 7F060364 E7A400E8 */  swc1  $f4, 0xe8($sp)
/* 094E98 7F060368 C60601AC */  lwc1  $f6, 0x1ac($s0)
/* 094E9C 7F06036C 46068200 */  add.s $f8, $f16, $f6
/* 094EA0 7F060370 E7A800E0 */  swc1  $f8, 0xe0($sp)
/* 094EA4 7F060374 C61201B0 */  lwc1  $f18, 0x1b0($s0)
/* 094EA8 7F060378 46125100 */  add.s $f4, $f10, $f18
/* 094EAC 7F06037C 0FC1772E */  jal   sub_GAME_7F05DCB8
/* 094EB0 7F060380 E7A400E4 */   swc1  $f4, 0xe4($sp)
/* 094EB4 7F060384 C7B000E0 */  lwc1  $f16, 0xe0($sp)
/* 094EB8 7F060388 3C028005 */  lui   $v0, %hi(g_ClockTimer)
/* 094EBC 7F06038C 24428374 */  addiu $v0, %lo(g_ClockTimer) # addiu $v0, $v0, -0x7c8c
/* 094EC0 7F060390 46008180 */  add.s $f6, $f16, $f0
/* 094EC4 7F060394 8C590000 */  lw    $t9, ($v0)
/* 094EC8 7F060398 00001825 */  move  $v1, $zero
/* 094ECC 7F06039C 1B200035 */  blez  $t9, .L7F060474
/* 094ED0 7F0603A0 E7A600E0 */   swc1  $f6, 0xe0($sp)
/* 094ED4 7F0603A4 3C018005 */  lui   $at, %hi(D_80053DE0)
/* 094ED8 7F0603A8 C4203DE0 */  lwc1  $f0, %lo(D_80053DE0)($at)
/* 094EDC 7F0603AC C60A00E4 */  lwc1  $f10, 0xe4($s0)
.L7F0603B0:
/* 094EE0 7F0603B0 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 094EE4 7F0603B4 C60600E8 */  lwc1  $f6, 0xe8($s0)
/* 094EE8 7F0603B8 460A0482 */  mul.s $f18, $f0, $f10
/* 094EEC 7F0603BC 24630001 */  addiu $v1, $v1, 1
/* 094EF0 7F0603C0 46060282 */  mul.s $f10, $f0, $f6
/* 094EF4 7F0603C4 46124100 */  add.s $f4, $f8, $f18
/* 094EF8 7F0603C8 E60400E4 */  swc1  $f4, 0xe4($s0)
/* 094EFC 7F0603CC C7B000E4 */  lwc1  $f16, 0xe4($sp)
/* 094F00 7F0603D0 C60400EC */  lwc1  $f4, 0xec($s0)
/* 094F04 7F0603D4 460A8200 */  add.s $f8, $f16, $f10
/* 094F08 7F0603D8 46040182 */  mul.s $f6, $f0, $f4
/* 094F0C 7F0603DC E60800E8 */  swc1  $f8, 0xe8($s0)
/* 094F10 7F0603E0 C7B200E8 */  lwc1  $f18, 0xe8($sp)
/* 094F14 7F0603E4 C60800F0 */  lwc1  $f8, 0xf0($s0)
/* 094F18 7F0603E8 46069400 */  add.s $f16, $f18, $f6
/* 094F1C 7F0603EC 46080102 */  mul.s $f4, $f0, $f8
/* 094F20 7F0603F0 E61000EC */  swc1  $f16, 0xec($s0)
/* 094F24 7F0603F4 C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 094F28 7F0603F8 C61000F4 */  lwc1  $f16, 0xf4($s0)
/* 094F2C 7F0603FC 46045480 */  add.s $f18, $f10, $f4
/* 094F30 7F060400 46100202 */  mul.s $f8, $f0, $f16
/* 094F34 7F060404 E61200F0 */  swc1  $f18, 0xf0($s0)
/* 094F38 7F060408 C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 094F3C 7F06040C C61200F8 */  lwc1  $f18, 0xf8($s0)
/* 094F40 7F060410 46083280 */  add.s $f10, $f6, $f8
/* 094F44 7F060414 46120402 */  mul.s $f16, $f0, $f18
/* 094F48 7F060418 E60A00F4 */  swc1  $f10, 0xf4($s0)
/* 094F4C 7F06041C C7A400DC */  lwc1  $f4, 0xdc($sp)
/* 094F50 7F060420 C60A00FC */  lwc1  $f10, 0xfc($s0)
/* 094F54 7F060424 46102180 */  add.s $f6, $f4, $f16
/* 094F58 7F060428 460A0482 */  mul.s $f18, $f0, $f10
/* 094F5C 7F06042C E60600F8 */  swc1  $f6, 0xf8($s0)
/* 094F60 7F060430 C7A800C8 */  lwc1  $f8, 0xc8($sp)
/* 094F64 7F060434 C6060100 */  lwc1  $f6, 0x100($s0)
/* 094F68 7F060438 46124100 */  add.s $f4, $f8, $f18
/* 094F6C 7F06043C 46060282 */  mul.s $f10, $f0, $f6
/* 094F70 7F060440 E60400FC */  swc1  $f4, 0xfc($s0)
/* 094F74 7F060444 C7B000CC */  lwc1  $f16, 0xcc($sp)
/* 094F78 7F060448 C6040104 */  lwc1  $f4, 0x104($s0)
/* 094F7C 7F06044C 460A8200 */  add.s $f8, $f16, $f10
/* 094F80 7F060450 46040182 */  mul.s $f6, $f0, $f4
/* 094F84 7F060454 E6080100 */  swc1  $f8, 0x100($s0)
/* 094F88 7F060458 C7B200D0 */  lwc1  $f18, 0xd0($sp)
/* 094F8C 7F06045C 46069400 */  add.s $f16, $f18, $f6
/* 094F90 7F060460 E6100104 */  swc1  $f16, 0x104($s0)
/* 094F94 7F060464 8C4D0000 */  lw    $t5, ($v0)
/* 094F98 7F060468 006D082A */  slt   $at, $v1, $t5
/* 094F9C 7F06046C 5420FFD0 */  bnezl $at, .L7F0603B0
/* 094FA0 7F060470 C60A00E4 */   lwc1  $f10, 0xe4($s0)
.L7F060474:
/* 094FA4 7F060474 3C018005 */  lui   $at, %hi(D_80053DE4)
/* 094FA8 7F060478 C4203DE4 */  lwc1  $f0, %lo(D_80053DE4)($at)
/* 094FAC 7F06047C C60A00E4 */  lwc1  $f10, 0xe4($s0)
/* 094FB0 7F060480 C60400E8 */  lwc1  $f4, 0xe8($s0)
/* 094FB4 7F060484 C60600EC */  lwc1  $f6, 0xec($s0)
/* 094FB8 7F060488 46005202 */  mul.s $f8, $f10, $f0
/* 094FBC 7F06048C C60A00F0 */  lwc1  $f10, 0xf0($s0)
/* 094FC0 7F060490 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 094FC4 7F060494 46002482 */  mul.s $f18, $f4, $f0
/* 094FC8 7F060498 C60400F4 */  lwc1  $f4, 0xf4($s0)
/* 094FCC 7F06049C 46003402 */  mul.s $f16, $f6, $f0
/* 094FD0 7F0604A0 E60800C0 */  swc1  $f8, 0xc0($s0)
/* 094FD4 7F0604A4 C60600F8 */  lwc1  $f6, 0xf8($s0)
/* 094FD8 7F0604A8 46005202 */  mul.s $f8, $f10, $f0
/* 094FDC 7F0604AC E61200C4 */  swc1  $f18, 0xc4($s0)
/* 094FE0 7F0604B0 C60A00FC */  lwc1  $f10, 0xfc($s0)
/* 094FE4 7F0604B4 46002482 */  mul.s $f18, $f4, $f0
/* 094FE8 7F0604B8 E61000C8 */  swc1  $f16, 0xc8($s0)
/* 094FEC 7F0604BC C6040100 */  lwc1  $f4, 0x100($s0)
/* 094FF0 7F0604C0 46003402 */  mul.s $f16, $f6, $f0
/* 094FF4 7F0604C4 E60800CC */  swc1  $f8, 0xcc($s0)
/* 094FF8 7F0604C8 C6060104 */  lwc1  $f6, 0x104($s0)
/* 094FFC 7F0604CC 46005202 */  mul.s $f8, $f10, $f0
/* 095000 7F0604D0 E61200D0 */  swc1  $f18, 0xd0($s0)
/* 095004 7F0604D4 46002482 */  mul.s $f18, $f4, $f0
/* 095008 7F0604D8 E61000D4 */  swc1  $f16, 0xd4($s0)
/* 09500C 7F0604DC 46003402 */  mul.s $f16, $f6, $f0
/* 095010 7F0604E0 E60800D8 */  swc1  $f8, 0xd8($s0)
/* 095014 7F0604E4 E61200DC */  swc1  $f18, 0xdc($s0)
/* 095018 7F0604E8 14800009 */  bnez  $a0, .L7F060510
/* 09501C 7F0604EC E61000E0 */   swc1  $f16, 0xe0($s0)
/* 095020 7F0604F0 0FC1773A */  jal   gunSetHorizontalOffset
/* 095024 7F0604F4 00000000 */   nop
/* 095028 7F0604F8 C60800C0 */  lwc1  $f8, 0xc0($s0)
/* 09502C 7F0604FC C60A01B8 */  lwc1  $f10, 0x1b8($s0)
/* 095030 7F060500 46080100 */  add.s $f4, $f0, $f8
/* 095034 7F060504 46045480 */  add.s $f18, $f10, $f4
/* 095038 7F060508 10000008 */  b     .L7F06052C
/* 09503C 7F06050C E7B20194 */   swc1  $f18, 0x194($sp)
.L7F060510:
/* 095040 7F060510 0FC1773A */  jal   gunSetHorizontalOffset
/* 095044 7F060514 00000000 */   nop
/* 095048 7F060518 C60600C0 */  lwc1  $f6, 0xc0($s0)
/* 09504C 7F06051C C60801B8 */  lwc1  $f8, 0x1b8($s0)
/* 095050 7F060520 46060400 */  add.s $f16, $f0, $f6
/* 095054 7F060524 46088281 */  sub.s $f10, $f16, $f8
/* 095058 7F060528 E7AA0194 */  swc1  $f10, 0x194($sp)
.L7F06052C:
/* 09505C 7F06052C 8FAE00F8 */  lw    $t6, 0xf8($sp)
/* 095060 7F060530 C61200C4 */  lwc1  $f18, 0xc4($s0)
/* 095064 7F060534 C61001BC */  lwc1  $f16, 0x1bc($s0)
/* 095068 7F060538 C5C40008 */  lwc1  $f4, 8($t6)
/* 09506C 7F06053C 8FA400FC */  lw    $a0, 0xfc($sp)
/* 095070 7F060540 24010019 */  li    $at, 25
/* 095074 7F060544 46122180 */  add.s $f6, $f4, $f18
/* 095078 7F060548 46068200 */  add.s $f8, $f16, $f6
/* 09507C 7F06054C E7A80198 */  swc1  $f8, 0x198($sp)
/* 095080 7F060550 C60400C8 */  lwc1  $f4, 0xc8($s0)
/* 095084 7F060554 C5CA000C */  lwc1  $f10, 0xc($t6)
/* 095088 7F060558 C61001C0 */  lwc1  $f16, 0x1c0($s0)
/* 09508C 7F06055C 46045480 */  add.s $f18, $f10, $f4
/* 095090 7F060560 46128180 */  add.s $f6, $f16, $f18
/* 095094 7F060564 10810005 */  beq   $a0, $at, .L7F06057C
/* 095098 7F060568 E7A6019C */   swc1  $f6, 0x19c($sp)
/* 09509C 7F06056C 2401001E */  li    $at, 30
/* 0950A0 7F060570 10810002 */  beq   $a0, $at, .L7F06057C
/* 0950A4 7F060574 24010017 */   li    $at, 23
/* 0950A8 7F060578 14810028 */  bne   $a0, $at, .L7F06061C
.L7F06057C:
/* 0950AC 7F06057C 3C028008 */   lui   $v0, %hi(g_CurrentPlayer)
/* 0950B0 7F060580 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 0950B4 7F060584 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 0950B8 7F060588 44810000 */  mtc1  $at, $f0
/* 0950BC 7F06058C C44A00A0 */  lwc1  $f10, 0xa0($v0)
/* 0950C0 7F060590 C7A80198 */  lwc1  $f8, 0x198($sp)
/* 0950C4 7F060594 3C014040 */  li    $at, 0x40400000 # 3.000000
/* 0950C8 7F060598 46005103 */  div.s $f4, $f10, $f0
/* 0950CC 7F06059C 44819000 */  mtc1  $at, $f18
/* 0950D0 7F0605A0 24010019 */  li    $at, 25
/* 0950D4 7F0605A4 46044400 */  add.s $f16, $f8, $f4
/* 0950D8 7F0605A8 C7A4019C */  lwc1  $f4, 0x19c($sp)
/* 0950DC 7F0605AC E7B00198 */  swc1  $f16, 0x198($sp)
/* 0950E0 7F0605B0 C44600A0 */  lwc1  $f6, 0xa0($v0)
/* 0950E4 7F0605B4 46069282 */  mul.s $f10, $f18, $f6
/* 0950E8 7F0605B8 46005203 */  div.s $f8, $f10, $f0
/* 0950EC 7F0605BC 46082400 */  add.s $f16, $f4, $f8
/* 0950F0 7F0605C0 14810014 */  bne   $a0, $at, .L7F060614
/* 0950F4 7F0605C4 E7B0019C */   swc1  $f16, 0x19c($sp)
/* 0950F8 7F0605C8 0FC293B2 */  jal   cur_player_get_screen_setting
/* 0950FC 7F0605CC 00000000 */   nop
/* 095100 7F0605D0 24010001 */  li    $at, 1
/* 095104 7F0605D4 5041000B */  beql  $v0, $at, .L7F060604
/* 095108 7F0605D8 3C014040 */   lui   $at, 0x4040
/* 09510C 7F0605DC 0FC293B2 */  jal   cur_player_get_screen_setting
/* 095110 7F0605E0 00000000 */   nop
/* 095114 7F0605E4 24010002 */  li    $at, 2
/* 095118 7F0605E8 50410006 */  beql  $v0, $at, .L7F060604
/* 09511C 7F0605EC 3C014040 */   lui   $at, 0x4040
/* 095120 7F0605F0 0FC293B8 */  jal   get_screen_ratio
/* 095124 7F0605F4 00000000 */   nop
/* 095128 7F0605F8 24010001 */  li    $at, 1
/* 09512C 7F0605FC 14410005 */  bne   $v0, $at, .L7F060614
/* 095130 7F060600 3C014040 */   li    $at, 0x40400000 # 3.000000
.L7F060604:
/* 095134 7F060604 44813000 */  mtc1  $at, $f6
/* 095138 7F060608 C7B20198 */  lwc1  $f18, 0x198($sp)
/* 09513C 7F06060C 46069281 */  sub.s $f10, $f18, $f6
/* 095140 7F060610 E7AA0198 */  swc1  $f10, 0x198($sp)
.L7F060614:
/* 095144 7F060614 1000002C */  b     .L7F0606C8
/* 095148 7F060618 8FA400FC */   lw    $a0, 0xfc($sp)
.L7F06061C:
/* 09514C 7F06061C 2401001F */  li    $at, 31
/* 095150 7F060620 14810016 */  bne   $a0, $at, .L7F06067C
/* 095154 7F060624 3C028008 */   lui   $v0, %hi(g_CurrentPlayer)
/* 095158 7F060628 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09515C 7F06062C 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 095160 7F060630 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 095164 7F060634 44810000 */  mtc1  $at, $f0
/* 095168 7F060638 3C014020 */  li    $at, 0x40200000 # 2.500000
/* 09516C 7F06063C 44812000 */  mtc1  $at, $f4
/* 095170 7F060640 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 095174 7F060644 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 095178 7F060648 3C0140F0 */  li    $at, 0x40F00000 # 7.500000
/* 09517C 7F06064C 46082402 */  mul.s $f16, $f4, $f8
/* 095180 7F060650 44812000 */  mtc1  $at, $f4
/* 095184 7F060654 46008483 */  div.s $f18, $f16, $f0
/* 095188 7F060658 46123280 */  add.s $f10, $f6, $f18
/* 09518C 7F06065C C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 095190 7F060660 E7AA0198 */  swc1  $f10, 0x198($sp)
/* 095194 7F060664 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 095198 7F060668 46082402 */  mul.s $f16, $f4, $f8
/* 09519C 7F06066C 46008183 */  div.s $f6, $f16, $f0
/* 0951A0 7F060670 46069280 */  add.s $f10, $f18, $f6
/* 0951A4 7F060674 10000014 */  b     .L7F0606C8
/* 0951A8 7F060678 E7AA019C */   swc1  $f10, 0x19c($sp)
.L7F06067C:
/* 0951AC 7F06067C 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 0951B0 7F060680 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 0951B4 7F060684 44810000 */  mtc1  $at, $f0
/* 0951B8 7F060688 3C0140A0 */  li    $at, 0x40A00000 # 5.000000
/* 0951BC 7F06068C 44812000 */  mtc1  $at, $f4
/* 0951C0 7F060690 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 0951C4 7F060694 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 0951C8 7F060698 3C014170 */  li    $at, 0x41700000 # 15.000000
/* 0951CC 7F06069C 46082402 */  mul.s $f16, $f4, $f8
/* 0951D0 7F0606A0 44812000 */  mtc1  $at, $f4
/* 0951D4 7F0606A4 46008483 */  div.s $f18, $f16, $f0
/* 0951D8 7F0606A8 46123280 */  add.s $f10, $f6, $f18
/* 0951DC 7F0606AC C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 0951E0 7F0606B0 E7AA0198 */  swc1  $f10, 0x198($sp)
/* 0951E4 7F0606B4 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 0951E8 7F0606B8 46082402 */  mul.s $f16, $f4, $f8
/* 0951EC 7F0606BC 46008183 */  div.s $f6, $f16, $f0
/* 0951F0 7F0606C0 46069280 */  add.s $f10, $f18, $f6
/* 0951F4 7F0606C4 E7AA019C */  swc1  $f10, 0x19c($sp)
.L7F0606C8:
/* 0951F8 7F0606C8 820F000C */  lb    $t7, 0xc($s0)
/* 0951FC 7F0606CC 11E00047 */  beqz  $t7, .L7F0607EC
/* 095200 7F0606D0 00000000 */   nop
/* 095204 7F0606D4 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 095208 7F0606D8 24050020 */   li    $a1, 32
/* 09520C 7F0606DC 10400043 */  beqz  $v0, .L7F0607EC
/* 095210 7F0606E0 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095214 7F0606E4 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 095218 7F0606E8 24050040 */   li    $a1, 64
/* 09521C 7F0606EC 10400016 */  beqz  $v0, .L7F060748
/* 095220 7F0606F0 00000000 */   nop
/* 095224 7F0606F4 0C002914 */  jal   randomGetNext
/* 095228 7F0606F8 00000000 */   nop
/* 09522C 7F0606FC 44822000 */  mtc1  $v0, $f4
/* 095230 7F060700 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095234 7F060704 04410004 */  bgez  $v0, .L7F060718
/* 095238 7F060708 46802220 */   cvt.s.w $f8, $f4
/* 09523C 7F06070C 44818000 */  mtc1  $at, $f16
/* 095240 7F060710 00000000 */  nop
/* 095244 7F060714 46104200 */  add.s $f8, $f8, $f16
.L7F060718:
/* 095248 7F060718 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09524C 7F06071C 44819000 */  mtc1  $at, $f18
/* 095250 7F060720 3C018005 */  lui   $at, %hi(D_80053DE8)
/* 095254 7F060724 C42A3DE8 */  lwc1  $f10, %lo(D_80053DE8)($at)
/* 095258 7F060728 46124182 */  mul.s $f6, $f8, $f18
/* 09525C 7F06072C 3C018005 */  lui   $at, %hi(D_80053DEC)
/* 095260 7F060730 C4303DEC */  lwc1  $f16, %lo(D_80053DEC)($at)
/* 095264 7F060734 C7B20194 */  lwc1  $f18, 0x194($sp)
/* 095268 7F060738 460A3102 */  mul.s $f4, $f6, $f10
/* 09526C 7F06073C 46048201 */  sub.s $f8, $f16, $f4
/* 095270 7F060740 46089180 */  add.s $f6, $f18, $f8
/* 095274 7F060744 E7A60194 */  swc1  $f6, 0x194($sp)
.L7F060748:
/* 095278 7F060748 0C002914 */  jal   randomGetNext
/* 09527C 7F06074C 00000000 */   nop
/* 095280 7F060750 44825000 */  mtc1  $v0, $f10
/* 095284 7F060754 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095288 7F060758 04410004 */  bgez  $v0, .L7F06076C
/* 09528C 7F06075C 46805420 */   cvt.s.w $f16, $f10
/* 095290 7F060760 44812000 */  mtc1  $at, $f4
/* 095294 7F060764 00000000 */  nop
/* 095298 7F060768 46048400 */  add.s $f16, $f16, $f4
.L7F06076C:
/* 09529C 7F06076C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0952A0 7F060770 44819000 */  mtc1  $at, $f18
/* 0952A4 7F060774 3C018005 */  lui   $at, %hi(D_80053DF0)
/* 0952A8 7F060778 C4263DF0 */  lwc1  $f6, %lo(D_80053DF0)($at)
/* 0952AC 7F06077C 46128202 */  mul.s $f8, $f16, $f18
/* 0952B0 7F060780 3C018005 */  lui   $at, %hi(D_80053DF4)
/* 0952B4 7F060784 C4243DF4 */  lwc1  $f4, %lo(D_80053DF4)($at)
/* 0952B8 7F060788 C7B20198 */  lwc1  $f18, 0x198($sp)
/* 0952BC 7F06078C 46064282 */  mul.s $f10, $f8, $f6
/* 0952C0 7F060790 460A2401 */  sub.s $f16, $f4, $f10
/* 0952C4 7F060794 46109200 */  add.s $f8, $f18, $f16
/* 0952C8 7F060798 0C002914 */  jal   randomGetNext
/* 0952CC 7F06079C E7A80198 */   swc1  $f8, 0x198($sp)
/* 0952D0 7F0607A0 44823000 */  mtc1  $v0, $f6
/* 0952D4 7F0607A4 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0952D8 7F0607A8 04410004 */  bgez  $v0, .L7F0607BC
/* 0952DC 7F0607AC 46803120 */   cvt.s.w $f4, $f6
/* 0952E0 7F0607B0 44815000 */  mtc1  $at, $f10
/* 0952E4 7F0607B4 00000000 */  nop
/* 0952E8 7F0607B8 460A2100 */  add.s $f4, $f4, $f10
.L7F0607BC:
/* 0952EC 7F0607BC 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0952F0 7F0607C0 44819000 */  mtc1  $at, $f18
/* 0952F4 7F0607C4 3C018005 */  lui   $at, %hi(D_80053DF8)
/* 0952F8 7F0607C8 C4283DF8 */  lwc1  $f8, %lo(D_80053DF8)($at)
/* 0952FC 7F0607CC 46122402 */  mul.s $f16, $f4, $f18
/* 095300 7F0607D0 3C018005 */  lui    $at, %hi(D_80053DFC)
/* 095304 7F0607D4 C42A3DFC */  lwc1  $f10, %lo(D_80053DFC)($at)
/* 095308 7F0607D8 C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 09530C 7F0607DC 46088182 */  mul.s $f6, $f16, $f8
/* 095310 7F0607E0 46065101 */  sub.s $f4, $f10, $f6
/* 095314 7F0607E4 46049400 */  add.s $f16, $f18, $f4
/* 095318 7F0607E8 E7B0019C */  swc1  $f16, 0x19c($sp)
.L7F0607EC:
/* 09531C 7F0607EC 0FC1E129 */  jal   getPlayer_c_screenwidth
/* 095320 7F0607F0 00000000 */   nop
/* 095324 7F0607F4 0FC1E129 */  jal   getPlayer_c_screenwidth
/* 095328 7F0607F8 E7A00048 */   swc1  $f0, 0x48($sp)
/* 09532C 7F0607FC 0FC1E131 */  jal   getPlayer_c_screenleft
/* 095330 7F060800 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 095334 7F060804 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 095338 7F060808 3C188008 */  lui   $t8, %hi(g_CurrentPlayer)
/* 09533C 7F06080C 8F18A0B0 */  lw    $t8, %lo(g_CurrentPlayer)($t8)
/* 095340 7F060810 44811000 */  mtc1  $at, $f2
/* 095344 7F060814 C7A6004C */  lwc1  $f6, 0x4c($sp)
/* 095348 7F060818 C7080FFC */  lwc1  $f8, 0xffc($t8)
/* 09534C 7F06081C 8FB900F8 */  lw    $t9, 0xf8($sp)
/* 095350 7F060820 46023482 */  mul.s $f18, $f6, $f2
/* 095354 7F060824 46004281 */  sub.s $f10, $f8, $f0
/* 095358 7F060828 C7300018 */  lwc1  $f16, 0x18($t9)
/* 09535C 7F06082C C7A60048 */  lwc1  $f6, 0x48($sp)
/* 095360 7F060830 46125101 */  sub.s $f4, $f10, $f18
/* 095364 7F060834 46102202 */  mul.s $f8, $f4, $f16
/* 095368 7F060838 C7A40194 */  lwc1  $f4, 0x194($sp)
/* 09536C 7F06083C 46023282 */  mul.s $f10, $f6, $f2
/* 095370 7F060840 460A4483 */  div.s $f18, $f8, $f10
/* 095374 7F060844 46122400 */  add.s $f16, $f4, $f18
/* 095378 7F060848 0FC1E135 */  jal   getPlayer_c_screentop
/* 09537C 7F06084C E7B00194 */   swc1  $f16, 0x194($sp)
/* 095380 7F060850 0FC1E12D */  jal   getPlayer_c_screenheight
/* 095384 7F060854 E7A00050 */   swc1  $f0, 0x50($sp)
/* 095388 7F060858 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09538C 7F06085C 3C0D8008 */  lui   $t5, %hi(g_CurrentPlayer)
/* 095390 7F060860 8DADA0B0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 095394 7F060864 44813000 */  mtc1  $at, $f6
/* 095398 7F060868 C7A40050 */  lwc1  $f4, 0x50($sp)
/* 09539C 7F06086C C5AA1000 */  lwc1  $f10, 0x1000($t5)
/* 0953A0 7F060870 46060202 */  mul.s $f8, $f0, $f6
/* 0953A4 7F060874 46045481 */  sub.s $f18, $f10, $f4
/* 0953A8 7F060878 4612403C */  c.lt.s $f8, $f18
/* 0953AC 7F06087C 00000000 */  nop
/* 0953B0 7F060880 4500001A */  bc1f  .L7F0608EC
/* 0953B4 7F060884 00000000 */   nop
/* 0953B8 7F060888 0FC1E12D */  jal   getPlayer_c_screenheight
/* 0953BC 7F06088C 00000000 */   nop
/* 0953C0 7F060890 0FC1E12D */  jal   getPlayer_c_screenheight
/* 0953C4 7F060894 E7A00048 */   swc1  $f0, 0x48($sp)
/* 0953C8 7F060898 0FC1E135 */  jal   getPlayer_c_screentop
/* 0953CC 7F06089C E7A0004C */   swc1  $f0, 0x4c($sp)
/* 0953D0 7F0608A0 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 0953D4 7F0608A4 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer)
/* 0953D8 7F0608A8 8DCEA0B0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 0953DC 7F0608AC 44811000 */  mtc1  $at, $f2
/* 0953E0 7F0608B0 C7AA004C */  lwc1  $f10, 0x4c($sp)
/* 0953E4 7F0608B4 C5D01000 */  lwc1  $f16, 0x1000($t6)
/* 0953E8 7F0608B8 8FAF00F8 */  lw    $t7, 0xf8($sp)
/* 0953EC 7F0608BC 46025102 */  mul.s $f4, $f10, $f2
/* 0953F0 7F0608C0 46008181 */  sub.s $f6, $f16, $f0
/* 0953F4 7F0608C4 C5F20014 */  lwc1  $f18, 0x14($t7)
/* 0953F8 7F0608C8 C7AA0048 */  lwc1  $f10, 0x48($sp)
/* 0953FC 7F0608CC 46043201 */  sub.s $f8, $f6, $f4
/* 095400 7F0608D0 46124402 */  mul.s $f16, $f8, $f18
/* 095404 7F0608D4 C7A80198 */  lwc1  $f8, 0x198($sp)
/* 095408 7F0608D8 46025182 */  mul.s $f6, $f10, $f2
/* 09540C 7F0608DC 46068103 */  div.s $f4, $f16, $f6
/* 095410 7F0608E0 46044481 */  sub.s $f18, $f8, $f4
/* 095414 7F0608E4 1000001A */  b     .L7F060950
/* 095418 7F0608E8 E7B20198 */   swc1  $f18, 0x198($sp)
.L7F0608EC:
/* 09541C 7F0608EC 0FC1E12D */  jal   getPlayer_c_screenheight
/* 095420 7F0608F0 00000000 */   nop
/* 095424 7F0608F4 0FC1E12D */  jal   getPlayer_c_screenheight
/* 095428 7F0608F8 E7A00048 */   swc1  $f0, 0x48($sp)
/* 09542C 7F0608FC 0FC1E135 */  jal   getPlayer_c_screentop
/* 095430 7F060900 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 095434 7F060904 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 095438 7F060908 3C188008 */  lui   $t8, %hi(g_CurrentPlayer)
/* 09543C 7F06090C 8F18A0B0 */  lw    $t8, %lo(g_CurrentPlayer)($t8)
/* 095440 7F060910 44818000 */  mtc1  $at, $f16
/* 095444 7F060914 C7AA004C */  lwc1  $f10, 0x4c($sp)
/* 095448 7F060918 C7081000 */  lwc1  $f8, 0x1000($t8)
/* 09544C 7F06091C 8FB900F8 */  lw    $t9, 0xf8($sp)
/* 095450 7F060920 46105182 */  mul.s $f6, $f10, $f16
/* 095454 7F060924 46004101 */  sub.s $f4, $f8, $f0
/* 095458 7F060928 C72A0010 */  lwc1  $f10, 0x10($t9)
/* 09545C 7F06092C C7A80048 */  lwc1  $f8, 0x48($sp)
/* 095460 7F060930 46062481 */  sub.s $f18, $f4, $f6
/* 095464 7F060934 44812000 */  mtc1  $at, $f4
/* 095468 7F060938 460A9402 */  mul.s $f16, $f18, $f10
/* 09546C 7F06093C C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 095470 7F060940 46044182 */  mul.s $f6, $f8, $f4
/* 095474 7F060944 46068483 */  div.s $f18, $f16, $f6
/* 095478 7F060948 46125201 */  sub.s $f8, $f10, $f18
/* 09547C 7F06094C E7A80198 */  swc1  $f8, 0x198($sp)
.L7F060950:
/* 095480 7F060950 0FC17185 */  jal   sub_GAME_7F05C614
/* 095484 7F060954 00000000 */   nop
/* 095488 7F060958 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 09548C 7F06095C 27A40154 */   addiu $a0, $sp, 0x154
/* 095490 7F060960 8FA200FC */  lw    $v0, 0xfc($sp)
/* 095494 7F060964 2401001E */  li    $at, 30
/* 095498 7F060968 10410002 */  beq   $v0, $at, .L7F060974
/* 09549C 7F06096C 24010017 */   li    $at, 23
/* 0954A0 7F060970 14410010 */  bne   $v0, $at, .L7F0609B4
.L7F060974:
/* 0954A4 7F060974 3C0D8003 */   lui   $t5, %hi(D_80035C70)
/* 0954A8 7F060978 25AD5C70 */  addiu $t5, %lo(D_80035C70) # addiu $t5, $t5, 0x5c70
/* 0954AC 7F06097C 8DA10000 */  lw    $at, ($t5)
/* 0954B0 7F060980 27A400B8 */  addiu $a0, $sp, 0xb8
/* 0954B4 7F060984 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 0954B8 7F060988 AC810000 */  sw    $at, ($a0)
/* 0954BC 7F06098C 8DAF0004 */  lw    $t7, 4($t5)
/* 0954C0 7F060990 AC8F0004 */  sw    $t7, 4($a0)
/* 0954C4 7F060994 8DA10008 */  lw    $at, 8($t5)
/* 0954C8 7F060998 0FC161C5 */  jal   matrix_4x4_set_rotation_around_xyz
/* 0954CC 7F06099C AC810008 */   sw    $at, 8($a0)
/* 0954D0 7F0609A0 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 0954D4 7F0609A4 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 0954D8 7F0609A8 27A50154 */   addiu $a1, $sp, 0x154
/* 0954DC 7F0609AC 10000039 */  b     .L7F060A94
/* 0954E0 7F0609B0 8E0D00BC */   lw    $t5, 0xbc($s0)
.L7F0609B4:
/* 0954E4 7F0609B4 2401001F */  li    $at, 31
/* 0954E8 7F0609B8 14410010 */  bne   $v0, $at, .L7F0609FC
/* 0954EC 7F0609BC 3C188003 */   lui   $t8, %hi(D_80035C7C)
/* 0954F0 7F0609C0 27185C7C */  addiu $t8, %lo(D_80035C7C) # addiu $t8, $t8, 0x5c7c
/* 0954F4 7F0609C4 8F010000 */  lw    $at, ($t8)
/* 0954F8 7F0609C8 27A400AC */  addiu $a0, $sp, 0xac
/* 0954FC 7F0609CC 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095500 7F0609D0 AC810000 */  sw    $at, ($a0)
/* 095504 7F0609D4 8F0E0004 */  lw    $t6, 4($t8)
/* 095508 7F0609D8 AC8E0004 */  sw    $t6, 4($a0)
/* 09550C 7F0609DC 8F010008 */  lw    $at, 8($t8)
/* 095510 7F0609E0 0FC161C5 */  jal   matrix_4x4_set_rotation_around_xyz
/* 095514 7F0609E4 AC810008 */   sw    $at, 8($a0)
/* 095518 7F0609E8 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 09551C 7F0609EC 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095520 7F0609F0 27A50154 */   addiu $a1, $sp, 0x154
/* 095524 7F0609F4 10000027 */  b     .L7F060A94
/* 095528 7F0609F8 8E0D00BC */   lw    $t5, 0xbc($s0)
.L7F0609FC:
/* 09552C 7F0609FC 24010001 */  li    $at, 1
/* 095530 7F060A00 14410023 */  bne   $v0, $at, .L7F060A90
/* 095534 7F060A04 3C0D8008 */   lui   $t5, %hi(g_CurrentPlayer)
/* 095538 7F060A08 8DADA0B0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 09553C 7F060A0C 24010011 */  li    $at, 17
/* 095540 7F060A10 3C198003 */  lui   $t9, %hi(D_80035C88)
/* 095544 7F060A14 8DAF2A38 */  lw    $t7, 0x2a38($t5)
/* 095548 7F060A18 27395C88 */  addiu $t9, %lo(D_80035C88) # addiu $t9, $t9, 0x5c88
/* 09554C 7F060A1C 55E1001D */  bnel  $t7, $at, .L7F060A94
/* 095550 7F060A20 8E0D00BC */   lw    $t5, 0xbc($s0)
/* 095554 7F060A24 8F210000 */  lw    $at, ($t9)
/* 095558 7F060A28 27A400A0 */  addiu $a0, $sp, 0xa0
/* 09555C 7F060A2C 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095560 7F060A30 AC810000 */  sw    $at, ($a0)
/* 095564 7F060A34 8F2E0004 */  lw    $t6, 4($t9)
/* 095568 7F060A38 AC8E0004 */  sw    $t6, 4($a0)
/* 09556C 7F060A3C 8F210008 */  lw    $at, 8($t9)
/* 095570 7F060A40 0FC161C5 */  jal   matrix_4x4_set_rotation_around_xyz
/* 095574 7F060A44 AC810008 */   sw    $at, 8($a0)
/* 095578 7F060A48 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 09557C 7F060A4C 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095580 7F060A50 27A50154 */   addiu $a1, $sp, 0x154
/* 095584 7F060A54 3C01C020 */  li    $at, 0xC0200000 # -2.500000
/* 095588 7F060A58 44818000 */  mtc1  $at, $f16
/* 09558C 7F060A5C C7A40194 */  lwc1  $f4, 0x194($sp)
/* 095590 7F060A60 3C018005 */  lui   $at, %hi(D_80053E00)
/* 095594 7F060A64 C4323E00 */  lwc1  $f18, %lo(D_80053E00)($at)
/* 095598 7F060A68 46102180 */  add.s $f6, $f4, $f16
/* 09559C 7F060A6C 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 0955A0 7F060A70 C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 0955A4 7F060A74 44818000 */  mtc1  $at, $f16
/* 0955A8 7F060A78 C7A4019C */  lwc1  $f4, 0x19c($sp)
/* 0955AC 7F060A7C E7A60194 */  swc1  $f6, 0x194($sp)
/* 0955B0 7F060A80 46125200 */  add.s $f8, $f10, $f18
/* 0955B4 7F060A84 46102180 */  add.s $f6, $f4, $f16
/* 0955B8 7F060A88 E7A80198 */  swc1  $f8, 0x198($sp)
/* 0955BC 7F060A8C E7A6019C */  swc1  $f6, 0x19c($sp)
.L7F060A90:
/* 0955C0 7F060A90 8E0D00BC */  lw    $t5, 0xbc($s0)
.L7F060A94:
/* 0955C4 7F060A94 51A00017 */  beql  $t5, $zero, .L7F060AF4
/* 0955C8 7F060A98 44802000 */   mtc1  $zero, $f4
/* 0955CC 7F060A9C C7AA0194 */  lwc1  $f10, 0x194($sp)
/* 0955D0 7F060AA0 C61200AC */  lwc1  $f18, 0xac($s0)
/* 0955D4 7F060AA4 C7A40198 */  lwc1  $f4, 0x198($sp)
/* 0955D8 7F060AA8 2604007C */  addiu $a0, $s0, 0x7c
/* 0955DC 7F060AAC 46125200 */  add.s $f8, $f10, $f18
/* 0955E0 7F060AB0 C7AA019C */  lwc1  $f10, 0x19c($sp)
/* 0955E4 7F060AB4 27A50154 */  addiu $a1, $sp, 0x154
/* 0955E8 7F060AB8 E7A80194 */  swc1  $f8, 0x194($sp)
/* 0955EC 7F060ABC C61000B0 */  lwc1  $f16, 0xb0($s0)
/* 0955F0 7F060AC0 46102180 */  add.s $f6, $f4, $f16
/* 0955F4 7F060AC4 E7A60198 */  swc1  $f6, 0x198($sp)
/* 0955F8 7F060AC8 C61200B4 */  lwc1  $f18, 0xb4($s0)
/* 0955FC 7F060ACC 46125200 */  add.s $f8, $f10, $f18
/* 095600 7F060AD0 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095604 7F060AD4 E7A8019C */   swc1  $f8, 0x19c($sp)
/* 095608 7F060AD8 44800000 */  mtc1  $zero, $f0
/* 09560C 7F060ADC 00000000 */  nop
/* 095610 7F060AE0 E7A00184 */  swc1  $f0, 0x184($sp)
/* 095614 7F060AE4 E7A00188 */  swc1  $f0, 0x188($sp)
/* 095618 7F060AE8 1000000A */  b     .L7F060B14
/* 09561C 7F060AEC E7A0018C */   swc1  $f0, 0x18c($sp)
/* 095620 7F060AF0 44802000 */  mtc1  $zero, $f4
.L7F060AF4:
/* 095624 7F060AF4 44808000 */  mtc1  $zero, $f16
/* 095628 7F060AF8 44803000 */  mtc1  $zero, $f6
/* 09562C 7F060AFC 44805000 */  mtc1  $zero, $f10
/* 095630 7F060B00 44800000 */  mtc1  $zero, $f0
/* 095634 7F060B04 E6040078 */  swc1  $f4, 0x78($s0)
/* 095638 7F060B08 E610006C */  swc1  $f16, 0x6c($s0)
/* 09563C 7F060B0C E6060070 */  swc1  $f6, 0x70($s0)
/* 095640 7F060B10 E60A0074 */  swc1  $f10, 0x74($s0)
.L7F060B14:
/* 095644 7F060B14 C61200CC */  lwc1  $f18, 0xcc($s0)
/* 095648 7F060B18 44050000 */  mfc1  $a1, $f0
/* 09564C 7F060B1C 44060000 */  mfc1  $a2, $f0
/* 095650 7F060B20 E7B20010 */  swc1  $f18, 0x10($sp)
/* 095654 7F060B24 C60800D0 */  lwc1  $f8, 0xd0($s0)
/* 095658 7F060B28 44070000 */  mfc1  $a3, $f0
/* 09565C 7F060B2C 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095660 7F060B30 E7A80014 */  swc1  $f8, 0x14($sp)
/* 095664 7F060B34 C60400D4 */  lwc1  $f4, 0xd4($s0)
/* 095668 7F060B38 E7A40018 */  swc1  $f4, 0x18($sp)
/* 09566C 7F060B3C C61000D8 */  lwc1  $f16, 0xd8($s0)
/* 095670 7F060B40 E7B0001C */  swc1  $f16, 0x1c($sp)
/* 095674 7F060B44 C60600DC */  lwc1  $f6, 0xdc($s0)
/* 095678 7F060B48 E7A60020 */  swc1  $f6, 0x20($sp)
/* 09567C 7F060B4C C60A00E0 */  lwc1  $f10, 0xe0($s0)
/* 095680 7F060B50 0FC16642 */  jal   matrix_4x4_set_basis_and_position_target
/* 095684 7F060B54 E7AA0024 */   swc1  $f10, 0x24($sp)
/* 095688 7F060B58 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 09568C 7F060B5C 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095690 7F060B60 27A50154 */   addiu $a1, $sp, 0x154
/* 095694 7F060B64 C7B20194 */  lwc1  $f18, 0x194($sp)
/* 095698 7F060B68 C60801C8 */  lwc1  $f8, 0x1c8($s0)
/* 09569C 7F060B6C C7B00198 */  lwc1  $f16, 0x198($sp)
/* 0956A0 7F060B70 C60601CC */  lwc1  $f6, 0x1cc($s0)
/* 0956A4 7F060B74 46089101 */  sub.s $f4, $f18, $f8
/* 0956A8 7F060B78 C60801D0 */  lwc1  $f8, 0x1d0($s0)
/* 0956AC 7F060B7C C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 0956B0 7F060B80 46068281 */  sub.s $f10, $f16, $f6
/* 0956B4 7F060B84 44062000 */  mfc1  $a2, $f4
/* 0956B8 7F060B88 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 0956BC 7F060B8C 46089101 */  sub.s $f4, $f18, $f8
/* 0956C0 7F060B90 44075000 */  mfc1  $a3, $f10
/* 0956C4 7F060B94 24050000 */  li    $a1, 0
/* 0956C8 7F060B98 0FC1673A */  jal   matrix_4x4_align
/* 0956CC 7F060B9C E7A40010 */   swc1  $f4, 0x10($sp)
/* 0956D0 7F060BA0 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 0956D4 7F060BA4 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 0956D8 7F060BA8 27A50154 */   addiu $a1, $sp, 0x154
/* 0956DC 7F060BAC 27A40154 */  addiu $a0, $sp, 0x154
/* 0956E0 7F060BB0 0FC16008 */  jal   matrix_4x4_copy
/* 0956E4 7F060BB4 27A50264 */   addiu $a1, $sp, 0x264
/* 0956E8 7F060BB8 27A40194 */  addiu $a0, $sp, 0x194
/* 0956EC 7F060BBC 0FC16266 */  jal   matrix_4x4_set_position
/* 0956F0 7F060BC0 27A50264 */   addiu $a1, $sp, 0x264
/* 0956F4 7F060BC4 26050228 */  addiu $a1, $s0, 0x228
/* 0956F8 7F060BC8 AFA50044 */  sw    $a1, 0x44($sp)
/* 0956FC 7F060BCC 0FC16008 */  jal   matrix_4x4_copy
/* 095700 7F060BD0 27A40264 */   addiu $a0, $sp, 0x264
/* 095704 7F060BD4 26040268 */  addiu $a0, $s0, 0x268
/* 095708 7F060BD8 AFA40040 */  sw    $a0, 0x40($sp)
/* 09570C 7F060BDC 0FC16008 */  jal   matrix_4x4_copy
/* 095710 7F060BE0 260502A8 */   addiu $a1, $s0, 0x2a8
/* 095714 7F060BE4 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 095718 7F060BE8 00000000 */   nop
/* 09571C 7F060BEC 00402025 */  move  $a0, $v0
/* 095720 7F060BF0 8FA50044 */  lw    $a1, 0x44($sp)
/* 095724 7F060BF4 0FC16063 */  jal   matrix_4x4_multiply_homogeneous
/* 095728 7F060BF8 8FA60040 */   lw    $a2, 0x40($sp)
/* 09572C 7F060BFC 240F0001 */  li    $t7, 1
/* 095730 7F060C00 A20F000F */  sb    $t7, 0xf($s0)
/* 095734 7F060C04 0FC17412 */  jal   get_ptr_weapon_model_header_line
/* 095738 7F060C08 8FA400FC */   lw    $a0, 0xfc($sp)
/* 09573C 7F060C0C 10400017 */  beqz  $v0, .L7F060C6C
/* 095740 7F060C10 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095744 7F060C14 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 095748 7F060C18 24050800 */   li    $a1, 2048
/* 09574C 7F060C1C 10400013 */  beqz  $v0, .L7F060C6C
/* 095750 7F060C20 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095754 7F060C24 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 095758 7F060C28 24052000 */   li    $a1, 8192
/* 09575C 7F060C2C 54400010 */  bnezl $v0, .L7F060C70
/* 095760 7F060C30 A200000F */   sb    $zero, 0xf($s0)
/* 095764 7F060C34 8E020024 */  lw    $v0, 0x24($s0)
/* 095768 7F060C38 24010006 */  li    $at, 6
/* 09576C 7F060C3C 1041000B */  beq   $v0, $at, .L7F060C6C
/* 095770 7F060C40 24010007 */   li    $at, 7
/* 095774 7F060C44 5041000A */  beql  $v0, $at, .L7F060C70
/* 095778 7F060C48 A200000F */   sb    $zero, 0xf($s0)
/* 09577C 7F060C4C 0FC173AF */  jal   Gun_hand_without_item
/* 095780 7F060C50 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 095784 7F060C54 50400006 */  beql  $v0, $zero, .L7F060C70
/* 095788 7F060C58 A200000F */   sb    $zero, 0xf($s0)
/* 09578C 7F060C5C 0FC173C0 */  jal   get_itemtype_in_hand
/* 095790 7F060C60 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 095794 7F060C64 54400003 */  bnezl $v0, .L7F060C74
/* 095798 7F060C68 8E18002C */   lw    $t8, 0x2c($s0)
.L7F060C6C:
/* 09579C 7F060C6C A200000F */  sb    $zero, 0xf($s0)
.L7F060C70:
/* 0957A0 7F060C70 8E18002C */  lw    $t8, 0x2c($s0)
.L7F060C74:
/* 0957A4 7F060C74 8FA400FC */  lw    $a0, 0xfc($sp)
/* 0957A8 7F060C78 5F000007 */  bgtzl $t8, .L7F060C98
/* 0957AC 7F060C7C 8219000F */   lb    $t9, 0xf($s0)
/* 0957B0 7F060C80 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 0957B4 7F060C84 24050002 */   li    $a1, 2
/* 0957B8 7F060C88 50400003 */  beql  $v0, $zero, .L7F060C98
/* 0957BC 7F060C8C 8219000F */   lb    $t9, 0xf($s0)
/* 0957C0 7F060C90 A200000F */  sb    $zero, 0xf($s0)
/* 0957C4 7F060C94 8219000F */  lb    $t9, 0xf($s0)
.L7F060C98:
/* 0957C8 7F060C98 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer)
/* 0957CC 7F060C9C 8FAD02A8 */  lw    $t5, 0x2a8($sp)
/* 0957D0 7F060CA0 532002CD */  beql  $t9, $zero, .L7F0617D8
/* 0957D4 7F060CA4 8FAF00FC */   lw    $t7, 0xfc($sp)
/* 0957D8 7F060CA8 8DCEA0B0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 0957DC 7F060CAC 000D7940 */  sll   $t7, $t5, 5
/* 0957E0 7F060CB0 00001825 */  move  $v1, $zero
/* 0957E4 7F060CB4 01CF1021 */  addu  $v0, $t6, $t7
/* 0957E8 7F060CB8 8444081E */  lh    $a0, 0x81e($v0)
/* 0957EC 7F060CBC 24420810 */  addiu $v0, $v0, 0x810
/* 0957F0 7F060CC0 AFA201A0 */  sw    $v0, 0x1a0($sp)
/* 0957F4 7F060CC4 0004C180 */  sll   $t8, $a0, 6
/* 0957F8 7F060CC8 03002025 */  move  $a0, $t8
/* 0957FC 7F060CCC 0FC2F5C5 */  jal   dynAllocate
/* 095800 7F060CD0 AFA00100 */   sw    $zero, 0x100($sp)
/* 095804 7F060CD4 8FB901A0 */  lw    $t9, 0x1a0($sp)
/* 095808 7F060CD8 AFA202A4 */  sw    $v0, 0x2a4($sp)
/* 09580C 7F060CDC 8FA30100 */  lw    $v1, 0x100($sp)
/* 095810 7F060CE0 872D000E */  lh    $t5, 0xe($t9)
/* 095814 7F060CE4 19A0000D */  blez  $t5, .L7F060D1C
/* 095818 7F060CE8 00402025 */   move  $a0, $v0
/* 09581C 7F060CEC AFA30100 */  sw    $v1, 0x100($sp)
.L7F060CF0:
/* 095820 7F060CF0 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 095824 7F060CF4 AFA40044 */   sw    $a0, 0x44($sp)
/* 095828 7F060CF8 8FAE01A0 */  lw    $t6, 0x1a0($sp)
/* 09582C 7F060CFC 8FA30100 */  lw    $v1, 0x100($sp)
/* 095830 7F060D00 8FA40044 */  lw    $a0, 0x44($sp)
/* 095834 7F060D04 85CF000E */  lh    $t7, 0xe($t6)
/* 095838 7F060D08 24630001 */  addiu $v1, $v1, 1
/* 09583C 7F060D0C 24840040 */  addiu $a0, $a0, 0x40
/* 095840 7F060D10 006F082A */  slt   $at, $v1, $t7
/* 095844 7F060D14 5420FFF6 */  bnezl $at, .L7F060CF0
/* 095848 7F060D18 AFA30100 */   sw    $v1, 0x100($sp)
.L7F060D1C:
/* 09584C 7F060D1C 0FC1D73D */  jal   modelCalculateRwDataLen
/* 095850 7F060D20 8FA401A0 */   lw    $a0, 0x1a0($sp)
/* 095854 7F060D24 260402F8 */  addiu $a0, $s0, 0x2f8
/* 095858 7F060D28 8FA501A0 */  lw    $a1, 0x1a0($sp)
/* 09585C 7F060D2C AFA40044 */  sw    $a0, 0x44($sp)
/* 095860 7F060D30 0FC1D7DA */  jal   modelInit
/* 095864 7F060D34 26060318 */   addiu $a2, $s0, 0x318
/* 095868 7F060D38 8FA40044 */  lw    $a0, 0x44($sp)
/* 09586C 7F060D3C 0FC17A5E */  jal   sub_GAME_7F05E978
/* 095870 7F060D40 24050001 */   li    $a1, 1
/* 095874 7F060D44 8FA40044 */  lw    $a0, 0x44($sp)
/* 095878 7F060D48 0FC17AA5 */  jal   sub_GAME_7F05EA94
/* 09587C 7F060D4C 8205000E */   lb    $a1, 0xe($s0)
/* 095880 7F060D50 8FB801A0 */  lw    $t8, 0x1a0($sp)
/* 095884 7F060D54 8F020008 */  lw    $v0, 8($t8)
/* 095888 7F060D58 8C440004 */  lw    $a0, 4($v0)
/* 09588C 7F060D5C 50800008 */  beql  $a0, $zero, .L7F060D80
/* 095890 7F060D60 8C43000C */   lw    $v1, 0xc($v0)
/* 095894 7F060D64 8C830004 */  lw    $v1, 4($a0)
/* 095898 7F060D68 94790004 */  lhu   $t9, 4($v1)
/* 09589C 7F060D6C 00196880 */  sll   $t5, $t9, 2
/* 0958A0 7F060D70 020D7021 */  addu  $t6, $s0, $t5
/* 0958A4 7F060D74 25CF0318 */  addiu $t7, $t6, 0x318
/* 0958A8 7F060D78 AFAF010C */  sw    $t7, 0x10c($sp)
/* 0958AC 7F060D7C 8C43000C */  lw    $v1, 0xc($v0)
.L7F060D80:
/* 0958B0 7F060D80 50600004 */  beql  $v1, $zero, .L7F060D94
/* 0958B4 7F060D84 8FB902A4 */   lw    $t9, 0x2a4($sp)
/* 0958B8 7F060D88 8C780004 */  lw    $t8, 4($v1)
/* 0958BC 7F060D8C AFB80108 */  sw    $t8, 0x108($sp)
/* 0958C0 7F060D90 8FB902A4 */  lw    $t9, 0x2a4($sp)
.L7F060D94:
/* 0958C4 7F060D94 24050400 */  li    $a1, 1024
/* 0958C8 7F060D98 AE190304 */  sw    $t9, 0x304($s0)
/* 0958CC 7F060D9C 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 0958D0 7F060DA0 8FA400FC */   lw    $a0, 0xfc($sp)
/* 0958D4 7F060DA4 10400008 */  beqz  $v0, .L7F060DC8
/* 0958D8 7F060DA8 00000000 */   nop
/* 0958DC 7F060DAC 8FAD02A8 */  lw    $t5, 0x2a8($sp)
/* 0958E0 7F060DB0 24010001 */  li    $at, 1
/* 0958E4 7F060DB4 15A10004 */  bne   $t5, $at, .L7F060DC8
/* 0958E8 7F060DB8 3C01BF80 */   li    $at, 0xBF800000 # -1.000000
/* 0958EC 7F060DBC 44816000 */  mtc1  $at, $f12
/* 0958F0 7F060DC0 0FC1626D */  jal   matrix_column_1_scalar_multiply
/* 0958F4 7F060DC4 27A50264 */   addiu $a1, $sp, 0x264
.L7F060DC8:
/* 0958F8 7F060DC8 3C018005 */  lui   $at, %hi(D_80053E04)
/* 0958FC 7F060DCC C42C3E04 */  lwc1  $f12, %lo(D_80053E04)($at)
/* 095900 7F060DD0 0FC1629F */  jal   matrix_scalar_multiply
/* 095904 7F060DD4 27A50264 */   addiu $a1, $sp, 0x264
/* 095908 7F060DD8 27A40264 */  addiu $a0, $sp, 0x264
/* 09590C 7F060DDC 0FC16008 */  jal   matrix_4x4_copy
/* 095910 7F060DE0 8FA502A4 */   lw    $a1, 0x2a4($sp)
/* 095914 7F060DE4 8FAF01A0 */  lw    $t7, 0x1a0($sp)
/* 095918 7F060DE8 3C0E8004 */  lui   $t6, %hi(skeleton_gun_revolver)
/* 09591C 7F060DEC 25CEC76C */  addiu $t6, %lo(skeleton_gun_revolver) # addiu $t6, $t6, -0x3894
/* 095920 7F060DF0 8DF80004 */  lw    $t8, 4($t7)
/* 095924 7F060DF4 55D80078 */  bnel  $t6, $t8, .L7F060FD8
/* 095928 7F060DF8 8FA2010C */   lw    $v0, 0x10c($sp)
/* 09592C 7F060DFC 8DE20008 */  lw    $v0, 8($t7)
/* 095930 7F060E00 8FB900FC */  lw    $t9, 0xfc($sp)
/* 095934 7F060E04 24010012 */  li    $at, 18
/* 095938 7F060E08 8C430010 */  lw    $v1, 0x10($v0)
/* 09593C 7F060E0C 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095940 7F060E10 50600041 */  beql  $v1, $zero, .L7F060F18
/* 095944 7F060E14 8C430014 */   lw    $v1, 0x14($v0)
/* 095948 7F060E18 44806000 */  mtc1  $zero, $f12
/* 09594C 7F060E1C 17210021 */  bne   $t9, $at, .L7F060EA4
/* 095950 7F060E20 8C640004 */   lw    $a0, 4($v1)
/* 095954 7F060E24 8E0D0024 */  lw    $t5, 0x24($s0)
/* 095958 7F060E28 24010001 */  li    $at, 1
/* 09595C 7F060E2C 55A10012 */  bnel  $t5, $at, .L7F060E78
/* 095960 7F060E30 8E18002C */   lw    $t8, 0x2c($s0)
/* 095964 7F060E34 8E18002C */  lw    $t8, 0x2c($s0)
/* 095968 7F060E38 8E0E0020 */  lw    $t6, 0x20($s0)
/* 09596C 7F060E3C 3C018005 */  lui   $at, %hi(D_80053E08)
/* 095970 7F060E40 00187880 */  sll   $t7, $t8, 2
/* 095974 7F060E44 01F87823 */  subu  $t7, $t7, $t8
/* 095978 7F060E48 000F7840 */  sll   $t7, $t7, 1
/* 09597C 7F060E4C 01CFC823 */  subu  $t9, $t6, $t7
/* 095980 7F060E50 272D001E */  addiu $t5, $t9, 0x1e
/* 095984 7F060E54 448D8000 */  mtc1  $t5, $f16
/* 095988 7F060E58 C42A3E08 */  lwc1  $f10, %lo(D_80053E08)($at)
/* 09598C 7F060E5C 3C014210 */  li    $at, 0x42100000 # 36.000000
/* 095990 7F060E60 468081A0 */  cvt.s.w $f6, $f16
/* 095994 7F060E64 44814000 */  mtc1  $at, $f8
/* 095998 7F060E68 460A3482 */  mul.s $f18, $f6, $f10
/* 09599C 7F060E6C 1000001D */  b     .L7F060EE4
/* 0959A0 7F060E70 46089303 */   div.s $f12, $f18, $f8
/* 0959A4 7F060E74 8E18002C */  lw    $t8, 0x2c($s0)
.L7F060E78:
/* 0959A8 7F060E78 240E0006 */  li    $t6, 6
/* 0959AC 7F060E7C 3C018005 */  lui   $at, %hi(D_80053E0C)
/* 0959B0 7F060E80 01D87823 */  subu  $t7, $t6, $t8
/* 0959B4 7F060E84 448F2000 */  mtc1  $t7, $f4
/* 0959B8 7F060E88 C4263E0C */  lwc1  $f6, %lo(D_80053E0C)($at)
/* 0959BC 7F060E8C 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 0959C0 7F060E90 46802420 */  cvt.s.w $f16, $f4
/* 0959C4 7F060E94 44819000 */  mtc1  $at, $f18
/* 0959C8 7F060E98 46068282 */  mul.s $f10, $f16, $f6
/* 0959CC 7F060E9C 10000011 */  b     .L7F060EE4
/* 0959D0 7F060EA0 46125303 */   div.s $f12, $f10, $f18
.L7F060EA4:
/* 0959D4 7F060EA4 8E190024 */  lw    $t9, 0x24($s0)
/* 0959D8 7F060EA8 24010001 */  li    $at, 1
/* 0959DC 7F060EAC 1721000D */  bne   $t9, $at, .L7F060EE4
/* 0959E0 7F060EB0 00000000 */   nop
/* 0959E4 7F060EB4 8E020020 */  lw    $v0, 0x20($s0)
/* 0959E8 7F060EB8 28410006 */  slti  $at, $v0, 6
/* 0959EC 7F060EBC 10200009 */  beqz  $at, .L7F060EE4
/* 0959F0 7F060EC0 00000000 */   nop
/* 0959F4 7F060EC4 44824000 */  mtc1  $v0, $f8
/* 0959F8 7F060EC8 3C018005 */  lui   $at, %hi(D_80053E10)
/* 0959FC 7F060ECC C4303E10 */  lwc1  $f16, %lo(D_80053E10)($at)
/* 095A00 7F060ED0 46804120 */  cvt.s.w $f4, $f8
/* 095A04 7F060ED4 3C014210 */  li    $at, 0x42100000 # 36.000000
/* 095A08 7F060ED8 44815000 */  mtc1  $at, $f10
/* 095A0C 7F060EDC 46102182 */  mul.s $f6, $f4, $f16
/* 095A10 7F060EE0 460A3303 */  div.s $f12, $f6, $f10
.L7F060EE4:
/* 095A14 7F060EE4 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 095A18 7F060EE8 AFA4009C */   sw    $a0, 0x9c($sp)
/* 095A1C 7F060EEC 8FA4009C */  lw    $a0, 0x9c($sp)
/* 095A20 7F060EF0 0FC16266 */  jal   matrix_4x4_set_position
/* 095A24 7F060EF4 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 095A28 7F060EF8 8FA602A4 */  lw    $a2, 0x2a4($sp)
/* 095A2C 7F060EFC 27A40264 */  addiu $a0, $sp, 0x264
/* 095A30 7F060F00 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095A34 7F060F04 0FC16032 */  jal   matrix_4x4_multiply
/* 095A38 7F060F08 24C600C0 */   addiu $a2, $a2, 0xc0
/* 095A3C 7F060F0C 8FAD01A0 */  lw    $t5, 0x1a0($sp)
/* 095A40 7F060F10 8DA20008 */  lw    $v0, 8($t5)
/* 095A44 7F060F14 8C430014 */  lw    $v1, 0x14($v0)
.L7F060F18:
/* 095A48 7F060F18 5060002F */  beql  $v1, $zero, .L7F060FD8
/* 095A4C 7F060F1C 8FA2010C */   lw    $v0, 0x10c($sp)
/* 095A50 7F060F20 8E0E0024 */  lw    $t6, 0x24($s0)
/* 095A54 7F060F24 24010001 */  li    $at, 1
/* 095A58 7F060F28 8C640004 */  lw    $a0, 4($v1)
/* 095A5C 7F060F2C 15C10022 */  bne   $t6, $at, .L7F060FB8
/* 095A60 7F060F30 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 095A64 7F060F34 8E020020 */  lw    $v0, 0x20($s0)
/* 095A68 7F060F38 24180006 */  li    $t8, 6
/* 095A6C 7F060F3C 28410003 */  slti  $at, $v0, 3
/* 095A70 7F060F40 1020000C */  beqz  $at, .L7F060F74
/* 095A74 7F060F44 03027823 */   subu  $t7, $t8, $v0
/* 095A78 7F060F48 44829000 */  mtc1  $v0, $f18
/* 095A7C 7F060F4C 3C018005 */  lui   $at, %hi(D_80053E14)
/* 095A80 7F060F50 C4303E14 */  lwc1  $f16, %lo(D_80053E14)($at)
/* 095A84 7F060F54 46809220 */  cvt.s.w $f8, $f18
/* 095A88 7F060F58 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 095A8C 7F060F5C 44815000 */  mtc1  $at, $f10
/* 095A90 7F060F60 46004107 */  neg.s $f4, $f8
/* 095A94 7F060F64 46102002 */  mul.s $f0, $f4, $f16
/* 095A98 7F060F68 46000180 */  add.s $f6, $f0, $f0
/* 095A9C 7F060F6C 1000000B */  b     .L7F060F9C
/* 095AA0 7F060F70 460A3303 */   div.s $f12, $f6, $f10
.L7F060F74:
/* 095AA4 7F060F74 448F9000 */  mtc1  $t7, $f18
/* 095AA8 7F060F78 3C018005 */  lui   $at, %hi(D_80053E18)
/* 095AAC 7F060F7C C4303E18 */  lwc1  $f16, %lo(D_80053E18)($at)
/* 095AB0 7F060F80 46809220 */  cvt.s.w $f8, $f18
/* 095AB4 7F060F84 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 095AB8 7F060F88 44815000 */  mtc1  $at, $f10
/* 095ABC 7F060F8C 46004107 */  neg.s $f4, $f8
/* 095AC0 7F060F90 46102002 */  mul.s $f0, $f4, $f16
/* 095AC4 7F060F94 46000180 */  add.s $f6, $f0, $f0
/* 095AC8 7F060F98 460A3303 */  div.s $f12, $f6, $f10
.L7F060F9C:
/* 095ACC 7F060F9C 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 095AD0 7F060FA0 AFA40094 */   sw    $a0, 0x94($sp)
/* 095AD4 7F060FA4 8FA40094 */  lw    $a0, 0x94($sp)
/* 095AD8 7F060FA8 0FC16266 */  jal   matrix_4x4_set_position
/* 095ADC 7F060FAC 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 095AE0 7F060FB0 10000004 */  b     .L7F060FC4
/* 095AE4 7F060FB4 8FA602A4 */   lw    $a2, 0x2a4($sp)
.L7F060FB8:
/* 095AE8 7F060FB8 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 095AEC 7F060FBC 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 095AF0 7F060FC0 8FA602A4 */  lw    $a2, 0x2a4($sp)
.L7F060FC4:
/* 095AF4 7F060FC4 27A40264 */  addiu $a0, $sp, 0x264
/* 095AF8 7F060FC8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095AFC 7F060FCC 0FC16032 */  jal   matrix_4x4_multiply
/* 095B00 7F060FD0 24C60100 */   addiu $a2, $a2, 0x100
/* 095B04 7F060FD4 8FA2010C */  lw    $v0, 0x10c($sp)
.L7F060FD8:
/* 095B08 7F060FD8 50400003 */  beql  $v0, $zero, .L7F060FE8
/* 095B0C 7F060FDC 8FB90108 */   lw    $t9, 0x108($sp)
/* 095B10 7F060FE0 AC400000 */  sw    $zero, ($v0)
/* 095B14 7F060FE4 8FB90108 */  lw    $t9, 0x108($sp)
.L7F060FE8:
/* 095B18 7F060FE8 53200142 */  beql  $t9, $zero, .L7F0614F4
/* 095B1C 7F060FEC C6100260 */   lwc1  $f16, 0x260($s0)
/* 095B20 7F060FF0 0C002914 */  jal   randomGetNext
/* 095B24 7F060FF4 00000000 */   nop
/* 095B28 7F060FF8 44829000 */  mtc1  $v0, $f18
/* 095B2C 7F060FFC 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095B30 7F061000 04410004 */  bgez  $v0, .L7F061014
/* 095B34 7F061004 46809220 */   cvt.s.w $f8, $f18
/* 095B38 7F061008 44812000 */  mtc1  $at, $f4
/* 095B3C 7F06100C 00000000 */  nop
/* 095B40 7F061010 46044200 */  add.s $f8, $f8, $f4
.L7F061014:
/* 095B44 7F061014 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 095B48 7F061018 44818000 */  mtc1  $at, $f16
/* 095B4C 7F06101C 3C013E80 */  li    $at, 0x3E800000 # 0.250000
/* 095B50 7F061020 44815000 */  mtc1  $at, $f10
/* 095B54 7F061024 46104182 */  mul.s $f6, $f8, $f16
/* 095B58 7F061028 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 095B5C 7F06102C 44812000 */  mtc1  $at, $f4
/* 095B60 7F061030 8FAD00F8 */  lw    $t5, 0xf8($sp)
/* 095B64 7F061034 8FA400FC */  lw    $a0, 0xfc($sp)
/* 095B68 7F061038 24050001 */  li    $a1, 1
/* 095B6C 7F06103C 460A3482 */  mul.s $f18, $f6, $f10
/* 095B70 7F061040 46049200 */  add.s $f8, $f18, $f4
/* 095B74 7F061044 E7A80080 */  swc1  $f8, 0x80($sp)
/* 095B78 7F061048 C5B00000 */  lwc1  $f16, ($t5)
/* 095B7C 7F06104C 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 095B80 7F061050 E7B0007C */   swc1  $f16, 0x7c($sp)
/* 095B84 7F061054 10400018 */  beqz  $v0, .L7F0610B8
/* 095B88 7F061058 8FA40108 */   lw    $a0, 0x108($sp)
/* 095B8C 7F06105C 0C002914 */  jal   randomGetNext
/* 095B90 7F061060 00000000 */   nop
/* 095B94 7F061064 44823000 */  mtc1  $v0, $f6
/* 095B98 7F061068 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095B9C 7F06106C 04410004 */  bgez  $v0, .L7F061080
/* 095BA0 7F061070 468032A0 */   cvt.s.w $f10, $f6
/* 095BA4 7F061074 44819000 */  mtc1  $at, $f18
/* 095BA8 7F061078 00000000 */  nop
/* 095BAC 7F06107C 46125280 */  add.s $f10, $f10, $f18
.L7F061080:
/* 095BB0 7F061080 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 095BB4 7F061084 44812000 */  mtc1  $at, $f4
/* 095BB8 7F061088 3C018005 */  lui   $at, %hi(D_80053E1C)
/* 095BBC 7F06108C C4303E1C */  lwc1  $f16, %lo(D_80053E1C)($at)
/* 095BC0 7F061090 46045202 */  mul.s $f8, $f10, $f4
/* 095BC4 7F061094 27A50224 */  addiu $a1, $sp, 0x224
/* 095BC8 7F061098 46104302 */  mul.s $f12, $f8, $f16
/* 095BCC 7F06109C 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 095BD0 7F0610A0 00000000 */   nop
/* 095BD4 7F0610A4 8FA40108 */  lw    $a0, 0x108($sp)
/* 095BD8 7F0610A8 0FC16266 */  jal   matrix_4x4_set_position
/* 095BDC 7F0610AC 27A50224 */   addiu $a1, $sp, 0x224
/* 095BE0 7F0610B0 10000004 */  b     .L7F0610C4
/* 095BE4 7F0610B4 C7AC0080 */   lwc1  $f12, 0x80($sp)
.L7F0610B8:
/* 095BE8 7F0610B8 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 095BEC 7F0610BC 27A50224 */   addiu $a1, $sp, 0x224
/* 095BF0 7F0610C0 C7AC0080 */  lwc1  $f12, 0x80($sp)
.L7F0610C4:
/* 095BF4 7F0610C4 0FC1629F */  jal   matrix_scalar_multiply
/* 095BF8 7F0610C8 27A50224 */   addiu $a1, $sp, 0x224
/* 095BFC 7F0610CC C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 095C00 7F0610D0 0FC16285 */  jal   matrix_column_3_scalar_multiply
/* 095C04 7F0610D4 27A50224 */   addiu $a1, $sp, 0x224
/* 095C08 7F0610D8 27A40264 */  addiu $a0, $sp, 0x264
/* 095C0C 7F0610DC 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 095C10 7F0610E0 27A50224 */   addiu $a1, $sp, 0x224
/* 095C14 7F0610E4 8FA502A4 */  lw    $a1, 0x2a4($sp)
/* 095C18 7F0610E8 27A40224 */  addiu $a0, $sp, 0x224
/* 095C1C 7F0610EC 0FC16008 */  jal   matrix_4x4_copy
/* 095C20 7F0610F0 24A50040 */   addiu $a1, $a1, 0x40
/* 095C24 7F0610F4 C7A60254 */  lwc1  $f6, 0x254($sp)
/* 095C28 7F0610F8 E60602E8 */  swc1  $f6, 0x2e8($s0)
/* 095C2C 7F0610FC C7B20258 */  lwc1  $f18, 0x258($sp)
/* 095C30 7F061100 E61202EC */  swc1  $f18, 0x2ec($s0)
/* 095C34 7F061104 C7AA025C */  lwc1  $f10, 0x25c($sp)
/* 095C38 7F061108 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 095C3C 7F06110C E60A02F0 */   swc1  $f10, 0x2f0($s0)
/* 095C40 7F061110 00402025 */  move  $a0, $v0
/* 095C44 7F061114 0FC1611D */  jal   mtx4TransformVecInPlace
/* 095C48 7F061118 260502E8 */   addiu $a1, $s0, 0x2e8
/* 095C4C 7F06111C C7A4025C */  lwc1  $f4, 0x25c($sp)
/* 095C50 7F061120 820E000D */  lb    $t6, 0xd($s0)
/* 095C54 7F061124 46002207 */  neg.s $f8, $f4
/* 095C58 7F061128 11C000EE */  beqz  $t6, .L7F0614E4
/* 095C5C 7F06112C E60802F4 */   swc1  $f8, 0x2f4($s0)
/* 095C60 7F061130 8FB8010C */  lw    $t8, 0x10c($sp)
/* 095C64 7F061134 240F0001 */  li    $t7, 1
/* 095C68 7F061138 53000003 */  beql  $t8, $zero, .L7F061148
/* 095C6C 7F06113C 8FB901A0 */   lw    $t9, 0x1a0($sp)
/* 095C70 7F061140 AF0F0000 */  sw    $t7, ($t8)
/* 095C74 7F061144 8FB901A0 */  lw    $t9, 0x1a0($sp)
.L7F061148:
/* 095C78 7F061148 8F2D0008 */  lw    $t5, 8($t9)
/* 095C7C 7F06114C 8DA30008 */  lw    $v1, 8($t5)
/* 095C80 7F061150 5060006D */  beql  $v1, $zero, .L7F061308
/* 095C84 7F061154 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 095C88 7F061158 8C620004 */  lw    $v0, 4($v1)
/* 095C8C 7F06115C C7A60224 */  lwc1  $f6, 0x224($sp)
/* 095C90 7F061160 C7A40234 */  lwc1  $f4, 0x234($sp)
/* 095C94 7F061164 C4500000 */  lwc1  $f16, ($v0)
/* 095C98 7F061168 C44A0004 */  lwc1  $f10, 4($v0)
/* 095C9C 7F06116C 46068482 */  mul.s $f18, $f16, $f6
/* 095CA0 7F061170 C4460008 */  lwc1  $f6, 8($v0)
/* 095CA4 7F061174 46045202 */  mul.s $f8, $f10, $f4
/* 095CA8 7F061178 C7AA0244 */  lwc1  $f10, 0x244($sp)
/* 095CAC 7F06117C 460A3102 */  mul.s $f4, $f6, $f10
/* 095CB0 7F061180 46089400 */  add.s $f16, $f18, $f8
/* 095CB4 7F061184 C7A80254 */  lwc1  $f8, 0x254($sp)
/* 095CB8 7F061188 46048480 */  add.s $f18, $f16, $f4
/* 095CBC 7F06118C C7B00228 */  lwc1  $f16, 0x228($sp)
/* 095CC0 7F061190 46124180 */  add.s $f6, $f8, $f18
/* 095CC4 7F061194 C7B20238 */  lwc1  $f18, 0x238($sp)
/* 095CC8 7F061198 E7A60084 */  swc1  $f6, 0x84($sp)
/* 095CCC 7F06119C C44A0000 */  lwc1  $f10, ($v0)
/* 095CD0 7F0611A0 C4480004 */  lwc1  $f8, 4($v0)
/* 095CD4 7F0611A4 46105102 */  mul.s $f4, $f10, $f16
/* 095CD8 7F0611A8 C4500008 */  lwc1  $f16, 8($v0)
/* 095CDC 7F0611AC 46124182 */  mul.s $f6, $f8, $f18
/* 095CE0 7F0611B0 C7A80248 */  lwc1  $f8, 0x248($sp)
/* 095CE4 7F0611B4 46088482 */  mul.s $f18, $f16, $f8
/* 095CE8 7F0611B8 46062280 */  add.s $f10, $f4, $f6
/* 095CEC 7F0611BC C7A60258 */  lwc1  $f6, 0x258($sp)
/* 095CF0 7F0611C0 46125100 */  add.s $f4, $f10, $f18
/* 095CF4 7F0611C4 C7AA022C */  lwc1  $f10, 0x22c($sp)
/* 095CF8 7F0611C8 46043400 */  add.s $f16, $f6, $f4
/* 095CFC 7F0611CC C7A4023C */  lwc1  $f4, 0x23c($sp)
/* 095D00 7F0611D0 E7B00088 */  swc1  $f16, 0x88($sp)
/* 095D04 7F0611D4 C4480000 */  lwc1  $f8, ($v0)
/* 095D08 7F0611D8 C4460004 */  lwc1  $f6, 4($v0)
/* 095D0C 7F0611DC 460A4482 */  mul.s $f18, $f8, $f10
/* 095D10 7F0611E0 C44A0008 */  lwc1  $f10, 8($v0)
/* 095D14 7F0611E4 46043402 */  mul.s $f16, $f6, $f4
/* 095D18 7F0611E8 C7A6024C */  lwc1  $f6, 0x24c($sp)
/* 095D1C 7F0611EC 46065102 */  mul.s $f4, $f10, $f6
/* 095D20 7F0611F0 46109200 */  add.s $f8, $f18, $f16
/* 095D24 7F0611F4 C7B0025C */  lwc1  $f16, 0x25c($sp)
/* 095D28 7F0611F8 46044480 */  add.s $f18, $f8, $f4
/* 095D2C 7F0611FC 46128280 */  add.s $f10, $f16, $f18
/* 095D30 7F061200 0C002914 */  jal   randomGetNext
/* 095D34 7F061204 E7AA008C */   swc1  $f10, 0x8c($sp)
/* 095D38 7F061208 44823000 */  mtc1  $v0, $f6
/* 095D3C 7F06120C 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 095D40 7F061210 04410005 */  bgez  $v0, .L7F061228
/* 095D44 7F061214 46803220 */   cvt.s.w $f8, $f6
/* 095D48 7F061218 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095D4C 7F06121C 44812000 */  mtc1  $at, $f4
/* 095D50 7F061220 00000000 */  nop
/* 095D54 7F061224 46044200 */  add.s $f8, $f8, $f4
.L7F061228:
/* 095D58 7F061228 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 095D5C 7F06122C 44818000 */  mtc1  $at, $f16
/* 095D60 7F061230 3C018005 */  lui   $at, %hi(D_80053E20)
/* 095D64 7F061234 C42A3E20 */  lwc1  $f10, %lo(D_80053E20)($at)
/* 095D68 7F061238 46104482 */  mul.s $f18, $f8, $f16
/* 095D6C 7F06123C C7B00088 */  lwc1  $f16, 0x88($sp)
/* 095D70 7F061240 C7A40084 */  lwc1  $f4, 0x84($sp)
/* 095D74 7F061244 46002207 */  neg.s $f8, $f4
/* 095D78 7F061248 460A9182 */  mul.s $f6, $f18, $f10
/* 095D7C 7F06124C C7AA008C */  lwc1  $f10, 0x8c($sp)
/* 095D80 7F061250 46008487 */  neg.s $f18, $f16
/* 095D84 7F061254 44064000 */  mfc1  $a2, $f8
/* 095D88 7F061258 44079000 */  mfc1  $a3, $f18
/* 095D8C 7F06125C 44053000 */  mfc1  $a1, $f6
/* 095D90 7F061260 46005187 */  neg.s $f6, $f10
/* 095D94 7F061264 0FC1673A */  jal   matrix_4x4_align
/* 095D98 7F061268 E7A60010 */   swc1  $f6, 0x10($sp)
/* 095D9C 7F06126C 3C018005 */  lui   $at, %hi(D_80053E24)
/* 095DA0 7F061270 C4243E24 */  lwc1  $f4, %lo(D_80053E24)($at)
/* 095DA4 7F061274 C7A80080 */  lwc1  $f8, 0x80($sp)
/* 095DA8 7F061278 27A501E4 */  addiu $a1, $sp, 0x1e4
/* 095DAC 7F06127C 46082302 */  mul.s $f12, $f4, $f8
/* 095DB0 7F061280 0FC1629F */  jal   matrix_scalar_multiply
/* 095DB4 7F061284 00000000 */   nop
/* 095DB8 7F061288 C7B00194 */  lwc1  $f16, 0x194($sp)
/* 095DBC 7F06128C C61201C8 */  lwc1  $f18, 0x1c8($s0)
/* 095DC0 7F061290 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 095DC4 7F061294 C60401CC */  lwc1  $f4, 0x1cc($s0)
/* 095DC8 7F061298 46128281 */  sub.s $f10, $f16, $f18
/* 095DCC 7F06129C C61201D0 */  lwc1  $f18, 0x1d0($s0)
/* 095DD0 7F0612A0 C7B0019C */  lwc1  $f16, 0x19c($sp)
/* 095DD4 7F0612A4 46043201 */  sub.s $f8, $f6, $f4
/* 095DD8 7F0612A8 44065000 */  mfc1  $a2, $f10
/* 095DDC 7F0612AC 27A40114 */  addiu $a0, $sp, 0x114
/* 095DE0 7F0612B0 46128281 */  sub.s $f10, $f16, $f18
/* 095DE4 7F0612B4 44074000 */  mfc1  $a3, $f8
/* 095DE8 7F0612B8 24050000 */  li    $a1, 0
/* 095DEC 7F0612BC 0FC166D6 */  jal   matrix_4x4_set_rotation_axis_angle
/* 095DF0 7F0612C0 E7AA0010 */   swc1  $f10, 0x10($sp)
/* 095DF4 7F0612C4 27A40114 */  addiu $a0, $sp, 0x114
/* 095DF8 7F0612C8 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 095DFC 7F0612CC 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095E00 7F0612D0 C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 095E04 7F0612D4 0FC162E0 */  jal   matrix_row_3_scalar_multiply
/* 095E08 7F0612D8 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095E0C 7F0612DC 27A40154 */  addiu $a0, $sp, 0x154
/* 095E10 7F0612E0 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 095E14 7F0612E4 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095E18 7F0612E8 27A40084 */  addiu $a0, $sp, 0x84
/* 095E1C 7F0612EC 0FC16266 */  jal   matrix_4x4_set_position
/* 095E20 7F0612F0 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095E24 7F0612F4 8FA502A4 */  lw    $a1, 0x2a4($sp)
/* 095E28 7F0612F8 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 095E2C 7F0612FC 0FC16008 */  jal   matrix_4x4_copy
/* 095E30 7F061300 24A50080 */   addiu $a1, $a1, 0x80
/* 095E34 7F061304 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.L7F061308:
/* 095E38 7F061308 3C0E8004 */  lui   $t6, %hi(skeleton_gun_kf7)
/* 095E3C 7F06130C 25CEC7AC */  addiu $t6, %lo(skeleton_gun_kf7) # addiu $t6, $t6, -0x3854
/* 095E40 7F061310 8DF80004 */  lw    $t8, 4($t7)
/* 095E44 7F061314 55D80074 */  bnel  $t6, $t8, .L7F0614E8
/* 095E48 7F061318 8FB801A0 */   lw    $t8, 0x1a0($sp)
/* 095E4C 7F06131C 8DF90008 */  lw    $t9, 8($t7)
/* 095E50 7F061320 8F230010 */  lw    $v1, 0x10($t9)
/* 095E54 7F061324 50600070 */  beql  $v1, $zero, .L7F0614E8
/* 095E58 7F061328 8FB801A0 */   lw    $t8, 0x1a0($sp)
/* 095E5C 7F06132C 8C620004 */  lw    $v0, 4($v1)
/* 095E60 7F061330 C7A40224 */  lwc1  $f4, 0x224($sp)
/* 095E64 7F061334 C7B20234 */  lwc1  $f18, 0x234($sp)
/* 095E68 7F061338 C4460000 */  lwc1  $f6, ($v0)
/* 095E6C 7F06133C C4500004 */  lwc1  $f16, 4($v0)
/* 095E70 7F061340 3C018005 */  lui   $at, %hi(D_80053E28)
/* 095E74 7F061344 46043202 */  mul.s $f8, $f6, $f4
/* 095E78 7F061348 C4440008 */  lwc1  $f4, 8($v0)
/* 095E7C 7F06134C 8FAD02A4 */  lw    $t5, 0x2a4($sp)
/* 095E80 7F061350 46128282 */  mul.s $f10, $f16, $f18
/* 095E84 7F061354 C7B00244 */  lwc1  $f16, 0x244($sp)
/* 095E88 7F061358 25AE00C0 */  addiu $t6, $t5, 0xc0
/* 095E8C 7F06135C 46102482 */  mul.s $f18, $f4, $f16
/* 095E90 7F061360 460A4180 */  add.s $f6, $f8, $f10
/* 095E94 7F061364 C7AA0254 */  lwc1  $f10, 0x254($sp)
/* 095E98 7F061368 46123200 */  add.s $f8, $f6, $f18
/* 095E9C 7F06136C C7A60228 */  lwc1  $f6, 0x228($sp)
/* 095EA0 7F061370 46085100 */  add.s $f4, $f10, $f8
/* 095EA4 7F061374 C7A80238 */  lwc1  $f8, 0x238($sp)
/* 095EA8 7F061378 E7A40084 */  swc1  $f4, 0x84($sp)
/* 095EAC 7F06137C C4500000 */  lwc1  $f16, ($v0)
/* 095EB0 7F061380 C44A0004 */  lwc1  $f10, 4($v0)
/* 095EB4 7F061384 46068482 */  mul.s $f18, $f16, $f6
/* 095EB8 7F061388 C4460008 */  lwc1  $f6, 8($v0)
/* 095EBC 7F06138C 46085102 */  mul.s $f4, $f10, $f8
/* 095EC0 7F061390 C7AA0248 */  lwc1  $f10, 0x248($sp)
/* 095EC4 7F061394 460A3202 */  mul.s $f8, $f6, $f10
/* 095EC8 7F061398 46049400 */  add.s $f16, $f18, $f4
/* 095ECC 7F06139C C7A40258 */  lwc1  $f4, 0x258($sp)
/* 095ED0 7F0613A0 46088480 */  add.s $f18, $f16, $f8
/* 095ED4 7F0613A4 C7B0022C */  lwc1  $f16, 0x22c($sp)
/* 095ED8 7F0613A8 46122180 */  add.s $f6, $f4, $f18
/* 095EDC 7F0613AC C7B2023C */  lwc1  $f18, 0x23c($sp)
/* 095EE0 7F0613B0 E7A60088 */  swc1  $f6, 0x88($sp)
/* 095EE4 7F0613B4 C44A0000 */  lwc1  $f10, ($v0)
/* 095EE8 7F0613B8 C4440004 */  lwc1  $f4, 4($v0)
/* 095EEC 7F0613BC 46105202 */  mul.s $f8, $f10, $f16
/* 095EF0 7F0613C0 C4500008 */  lwc1  $f16, 8($v0)
/* 095EF4 7F0613C4 AFAE0040 */  sw    $t6, 0x40($sp)
/* 095EF8 7F0613C8 46122182 */  mul.s $f6, $f4, $f18
/* 095EFC 7F0613CC C7A4024C */  lwc1  $f4, 0x24c($sp)
/* 095F00 7F0613D0 46048482 */  mul.s $f18, $f16, $f4
/* 095F04 7F0613D4 C4243E28 */  lwc1  $f4, %lo(D_80053E28)($at)
/* 095F08 7F0613D8 46064280 */  add.s $f10, $f8, $f6
/* 095F0C 7F0613DC C7A6025C */  lwc1  $f6, 0x25c($sp)
/* 095F10 7F0613E0 46125200 */  add.s $f8, $f10, $f18
/* 095F14 7F0613E4 C7AA0080 */  lwc1  $f10, 0x80($sp)
/* 095F18 7F0613E8 460A2482 */  mul.s $f18, $f4, $f10
/* 095F1C 7F0613EC 46083400 */  add.s $f16, $f6, $f8
/* 095F20 7F0613F0 E7B0008C */  swc1  $f16, 0x8c($sp)
/* 095F24 7F0613F4 0C002914 */  jal   randomGetNext
/* 095F28 7F0613F8 E7B20038 */   swc1  $f18, 0x38($sp)
/* 095F2C 7F0613FC 44823000 */  mtc1  $v0, $f6
/* 095F30 7F061400 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 095F34 7F061404 04410005 */  bgez  $v0, .L7F06141C
/* 095F38 7F061408 46803220 */   cvt.s.w $f8, $f6
/* 095F3C 7F06140C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095F40 7F061410 44818000 */  mtc1  $at, $f16
/* 095F44 7F061414 00000000 */  nop
/* 095F48 7F061418 46104200 */  add.s $f8, $f8, $f16
.L7F06141C:
/* 095F4C 7F06141C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 095F50 7F061420 44812000 */  mtc1  $at, $f4
/* 095F54 7F061424 3C018005 */  lui   $at, %hi(D_80053E2C)
/* 095F58 7F061428 C4323E2C */  lwc1  $f18, %lo(D_80053E2C)($at)
/* 095F5C 7F06142C 46044282 */  mul.s $f10, $f8, $f4
/* 095F60 7F061430 C7A40088 */  lwc1  $f4, 0x88($sp)
/* 095F64 7F061434 C7B00084 */  lwc1  $f16, 0x84($sp)
/* 095F68 7F061438 46008207 */  neg.s $f8, $f16
/* 095F6C 7F06143C 46125182 */  mul.s $f6, $f10, $f18
/* 095F70 7F061440 C7B2008C */  lwc1  $f18, 0x8c($sp)
/* 095F74 7F061444 46002287 */  neg.s $f10, $f4
/* 095F78 7F061448 44064000 */  mfc1  $a2, $f8
/* 095F7C 7F06144C 44075000 */  mfc1  $a3, $f10
/* 095F80 7F061450 44053000 */  mfc1  $a1, $f6
/* 095F84 7F061454 46009187 */  neg.s $f6, $f18
/* 095F88 7F061458 0FC1673A */  jal   matrix_4x4_align
/* 095F8C 7F06145C E7A60010 */   swc1  $f6, 0x10($sp)
/* 095F90 7F061460 C7AC0038 */  lwc1  $f12, 0x38($sp)
/* 095F94 7F061464 0FC1629F */  jal   matrix_scalar_multiply
/* 095F98 7F061468 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095F9C 7F06146C C7B00194 */  lwc1  $f16, 0x194($sp)
/* 095FA0 7F061470 C60801C8 */  lwc1  $f8, 0x1c8($s0)
/* 095FA4 7F061474 C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 095FA8 7F061478 C61201CC */  lwc1  $f18, 0x1cc($s0)
/* 095FAC 7F06147C 46088101 */  sub.s $f4, $f16, $f8
/* 095FB0 7F061480 C60801D0 */  lwc1  $f8, 0x1d0($s0)
/* 095FB4 7F061484 C7B0019C */  lwc1  $f16, 0x19c($sp)
/* 095FB8 7F061488 46125181 */  sub.s $f6, $f10, $f18
/* 095FBC 7F06148C 44062000 */  mfc1  $a2, $f4
/* 095FC0 7F061490 27A40114 */  addiu $a0, $sp, 0x114
/* 095FC4 7F061494 46088101 */  sub.s $f4, $f16, $f8
/* 095FC8 7F061498 44073000 */  mfc1  $a3, $f6
/* 095FCC 7F06149C 24050000 */  li    $a1, 0
/* 095FD0 7F0614A0 0FC166D6 */  jal   matrix_4x4_set_rotation_axis_angle
/* 095FD4 7F0614A4 E7A40010 */   swc1  $f4, 0x10($sp)
/* 095FD8 7F0614A8 27A40114 */  addiu $a0, $sp, 0x114
/* 095FDC 7F0614AC 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 095FE0 7F0614B0 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095FE4 7F0614B4 C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 095FE8 7F0614B8 0FC162E0 */  jal   matrix_row_3_scalar_multiply
/* 095FEC 7F0614BC 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095FF0 7F0614C0 27A40154 */  addiu $a0, $sp, 0x154
/* 095FF4 7F0614C4 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 095FF8 7F0614C8 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 095FFC 7F0614CC 27A40084 */  addiu $a0, $sp, 0x84
/* 096000 7F0614D0 0FC16266 */  jal   matrix_4x4_set_position
/* 096004 7F0614D4 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096008 7F0614D8 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 09600C 7F0614DC 0FC16008 */  jal   matrix_4x4_copy
/* 096010 7F0614E0 8FA50040 */   lw    $a1, 0x40($sp)
.L7F0614E4:
/* 096014 7F0614E4 8FB801A0 */  lw    $t8, 0x1a0($sp)
.L7F0614E8:
/* 096018 7F0614E8 1000000C */  b     .L7F06151C
/* 09601C 7F0614EC 8F020008 */   lw    $v0, 8($t8)
/* 096020 7F0614F0 C6100260 */  lwc1  $f16, 0x260($s0)
.L7F0614F4:
/* 096024 7F0614F4 C60A0298 */  lwc1  $f10, 0x298($s0)
/* 096028 7F0614F8 C612029C */  lwc1  $f18, 0x29c($s0)
/* 09602C 7F0614FC C60602A0 */  lwc1  $f6, 0x2a0($s0)
/* 096030 7F061500 46008207 */  neg.s $f8, $f16
/* 096034 7F061504 E60A02E8 */  swc1  $f10, 0x2e8($s0)
/* 096038 7F061508 E60802F4 */  swc1  $f8, 0x2f4($s0)
/* 09603C 7F06150C E61202EC */  swc1  $f18, 0x2ec($s0)
/* 096040 7F061510 E60602F0 */  swc1  $f6, 0x2f0($s0)
/* 096044 7F061514 8FAF01A0 */  lw    $t7, 0x1a0($sp)
/* 096048 7F061518 8DE20008 */  lw    $v0, 8($t7)
.L7F06151C:
/* 09604C 7F06151C 8C440018 */  lw    $a0, 0x18($v0)
/* 096050 7F061520 50800043 */  beql  $a0, $zero, .L7F061630
/* 096054 7F061524 8FAD01A0 */   lw    $t5, 0x1a0($sp)
/* 096058 7F061528 8C990004 */  lw    $t9, 4($a0)
/* 09605C 7F06152C 00002825 */  move  $a1, $zero
/* 096060 7F061530 0FC1B15C */  jal   modelFindNodeMtxIndex
/* 096064 7F061534 AFB90070 */   sw    $t9, 0x70($sp)
/* 096068 7F061538 AFA2006C */  sw    $v0, 0x6c($sp)
/* 09606C 7F06153C 8E050010 */  lw    $a1, 0x10($s0)
/* 096070 7F061540 0FC179AD */  jal   sub_GAME_7F05E6B4
/* 096074 7F061544 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096078 7F061548 8FAD01A0 */  lw    $t5, 0x1a0($sp)
/* 09607C 7F06154C 8FA40070 */  lw    $a0, 0x70($sp)
/* 096080 7F061550 27A601A4 */  addiu $a2, $sp, 0x1a4
/* 096084 7F061554 85AE000C */  lh    $t6, 0xc($t5)
/* 096088 7F061558 29C1001D */  slti  $at, $t6, 0x1d
/* 09608C 7F06155C 1420002A */  bnez  $at, .L7F061608
/* 096090 7F061560 00000000 */   nop
/* 096094 7F061564 8DB80008 */  lw    $t8, 8($t5)
/* 096098 7F061568 8F030070 */  lw    $v1, 0x70($t8)
/* 09609C 7F06156C 10600026 */  beqz  $v1, .L7F061608
/* 0960A0 7F061570 00000000 */   nop
/* 0960A4 7F061574 8C620004 */  lw    $v0, 4($v1)
/* 0960A8 7F061578 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 0960AC 7F06157C 0FC17999 */  jal   get_value_if_watch_is_on_hand_or_not
/* 0960B0 7F061580 AFA20068 */   sw    $v0, 0x68($sp)
/* 0960B4 7F061584 3C018005 */  lui   $at, %hi(D_80053E30)
/* 0960B8 7F061588 C42A3E30 */  lwc1  $f10, %lo(D_80053E30)($at)
/* 0960BC 7F06158C C6040214 */  lwc1  $f4, 0x214($s0)
/* 0960C0 7F061590 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 0960C4 7F061594 44818000 */  mtc1  $at, $f16
/* 0960C8 7F061598 460A2480 */  add.s $f18, $f4, $f10
/* 0960CC 7F06159C 3C018005 */  lui   $at, %hi(D_80053E34)
/* 0960D0 7F0615A0 C4243E34 */  lwc1  $f4, %lo(D_80053E34)($at)
/* 0960D4 7F0615A4 8FA20068 */  lw    $v0, 0x68($sp)
/* 0960D8 7F0615A8 46009181 */  sub.s $f6, $f18, $f0
/* 0960DC 7F0615AC 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 0960E0 7F0615B0 C4520000 */  lwc1  $f18, ($v0)
/* 0960E4 7F0615B4 46103202 */  mul.s $f8, $f6, $f16
/* 0960E8 7F0615B8 C446000C */  lwc1  $f6, 0xc($v0)
/* 0960EC 7F0615BC 46069401 */  sub.s $f16, $f18, $f6
/* 0960F0 7F0615C0 C4460014 */  lwc1  $f6, 0x14($v0)
/* 0960F4 7F0615C4 C4520008 */  lwc1  $f18, 8($v0)
/* 0960F8 7F0615C8 46044283 */  div.s $f10, $f8, $f4
/* 0960FC 7F0615CC C4440010 */  lwc1  $f4, 0x10($v0)
/* 096100 7F0615D0 C4480004 */  lwc1  $f8, 4($v0)
/* 096104 7F0615D4 44068000 */  mfc1  $a2, $f16
/* 096108 7F0615D8 46069401 */  sub.s $f16, $f18, $f6
/* 09610C 7F0615DC E7B00010 */  swc1  $f16, 0x10($sp)
/* 096110 7F0615E0 44055000 */  mfc1  $a1, $f10
/* 096114 7F0615E4 46044281 */  sub.s $f10, $f8, $f4
/* 096118 7F0615E8 44075000 */  mfc1  $a3, $f10
/* 09611C 7F0615EC 0C005DC8 */  jal   guRotateF
/* 096120 7F0615F0 00000000 */   nop
/* 096124 7F0615F4 8FA40070 */  lw    $a0, 0x70($sp)
/* 096128 7F0615F8 0FC16266 */  jal   matrix_4x4_set_position
/* 09612C 7F0615FC 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 096130 7F061600 10000004 */  b     .L7F061614
/* 096134 7F061604 8FAF006C */   lw    $t7, 0x6c($sp)
.L7F061608:
/* 096138 7F061608 0FC16134 */  jal   matrix_4x4_set_position_and_rotation_around_y
/* 09613C 7F06160C 8E050214 */   lw    $a1, 0x214($s0)
/* 096140 7F061610 8FAF006C */  lw    $t7, 0x6c($sp)
.L7F061614:
/* 096144 7F061614 8FAE02A4 */  lw    $t6, 0x2a4($sp)
/* 096148 7F061618 27A40264 */  addiu $a0, $sp, 0x264
/* 09614C 7F06161C 000FC980 */  sll   $t9, $t7, 6
/* 096150 7F061620 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 096154 7F061624 0FC16063 */  jal   matrix_4x4_multiply_homogeneous
/* 096158 7F061628 032E3021 */   addu  $a2, $t9, $t6
/* 09615C 7F06162C 8FAD01A0 */  lw    $t5, 0x1a0($sp)
.L7F061630:
/* 096160 7F061630 8FA40044 */  lw    $a0, 0x44($sp)
/* 096164 7F061634 85B8000C */  lh    $t8, 0xc($t5)
/* 096168 7F061638 01A02825 */  move  $a1, $t5
/* 09616C 7F06163C 2B01001E */  slti  $at, $t8, 0x1e
/* 096170 7F061640 54200004 */  bnezl $at, .L7F061654
/* 096174 7F061644 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 096178 7F061648 0FC21F05 */  jal   seems_to_load_cuff_microcode
/* 09617C 7F06164C 2406001D */   li    $a2, 29
/* 096180 7F061650 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.L7F061654:
/* 096184 7F061654 8DF90008 */  lw    $t9, 8($t7)
/* 096188 7F061658 8F24001C */  lw    $a0, 0x1c($t9)
/* 09618C 7F06165C 50800017 */  beql  $a0, $zero, .L7F0616BC
/* 096190 7F061660 8FB901A0 */   lw    $t9, 0x1a0($sp)
/* 096194 7F061664 8C8E0004 */  lw    $t6, 4($a0)
/* 096198 7F061668 00002825 */  move  $a1, $zero
/* 09619C 7F06166C 0FC1B15C */  jal   modelFindNodeMtxIndex
/* 0961A0 7F061670 AFAE0064 */   sw    $t6, 0x64($sp)
/* 0961A4 7F061674 AFA20060 */  sw    $v0, 0x60($sp)
/* 0961A8 7F061678 0FC17A0F */  jal   sub_GAME_7F05E83C
/* 0961AC 7F06167C 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0961B0 7F061680 8FA40064 */  lw    $a0, 0x64($sp)
/* 0961B4 7F061684 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 0961B8 7F061688 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 0961BC 7F06168C C7A801DC */  lwc1  $f8, 0x1dc($sp)
/* 0961C0 7F061690 C6040218 */  lwc1  $f4, 0x218($s0)
/* 0961C4 7F061694 8FB80060 */  lw    $t8, 0x60($sp)
/* 0961C8 7F061698 8FAF02A4 */  lw    $t7, 0x2a4($sp)
/* 0961CC 7F06169C 46044281 */  sub.s $f10, $f8, $f4
/* 0961D0 7F0616A0 00186980 */  sll   $t5, $t8, 6
/* 0961D4 7F0616A4 27A40264 */  addiu $a0, $sp, 0x264
/* 0961D8 7F0616A8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 0961DC 7F0616AC E7AA01DC */  swc1  $f10, 0x1dc($sp)
/* 0961E0 7F0616B0 0FC16032 */  jal   matrix_4x4_multiply
/* 0961E4 7F0616B4 01AF3021 */   addu  $a2, $t5, $t7
/* 0961E8 7F0616B8 8FB901A0 */  lw    $t9, 0x1a0($sp)
.L7F0616BC:
/* 0961EC 7F0616BC 00001825 */  move  $v1, $zero
/* 0961F0 7F0616C0 00003025 */  move  $a2, $zero
/* 0961F4 7F0616C4 872E000C */  lh    $t6, 0xc($t9)
/* 0961F8 7F0616C8 24070005 */  li    $a3, 5
/* 0961FC 7F0616CC 29C10013 */  slti  $at, $t6, 0x13
/* 096200 7F0616D0 1420002A */  bnez  $at, .L7F06177C
/* 096204 7F0616D4 00000000 */   nop
.L7F0616D8:
/* 096208 7F0616D8 8FB801A0 */  lw    $t8, 0x1a0($sp)
/* 09620C 7F0616DC 8FA40044 */  lw    $a0, 0x44($sp)
/* 096210 7F0616E0 8F0D0008 */  lw    $t5, 8($t8)
/* 096214 7F0616E4 01A67821 */  addu  $t7, $t5, $a2
/* 096218 7F0616E8 8DE50048 */  lw    $a1, 0x48($t7)
/* 09621C 7F0616EC 50A0000E */  beql  $a1, $zero, .L7F061728
/* 096220 7F0616F0 8FAD01A0 */   lw    $t5, 0x1a0($sp)
/* 096224 7F0616F4 AFA3005C */  sw    $v1, 0x5c($sp)
/* 096228 7F0616F8 0FC1B1E7 */  jal   modelGetNodeRwData
/* 09622C 7F0616FC AFA60040 */   sw    $a2, 0x40($sp)
/* 096230 7F061700 8FA3005C */  lw    $v1, 0x5c($sp)
/* 096234 7F061704 8FA60040 */  lw    $a2, 0x40($sp)
/* 096238 7F061708 10400006 */  beqz  $v0, .L7F061724
/* 09623C 7F06170C 24070005 */   li    $a3, 5
/* 096240 7F061710 8E190034 */  lw    $t9, 0x34($s0)
/* 096244 7F061714 00E37023 */  subu  $t6, $a3, $v1
/* 096248 7F061718 032EC02A */  slt   $t8, $t9, $t6
/* 09624C 7F06171C 3B180001 */  xori  $t8, $t8, 1
/* 096250 7F061720 AC580000 */  sw    $t8, ($v0)
.L7F061724:
/* 096254 7F061724 8FAD01A0 */  lw    $t5, 0x1a0($sp)
.L7F061728:
/* 096258 7F061728 8FA40044 */  lw    $a0, 0x44($sp)
/* 09625C 7F06172C 8DAF0008 */  lw    $t7, 8($t5)
/* 096260 7F061730 01E6C821 */  addu  $t9, $t7, $a2
/* 096264 7F061734 8F25005C */  lw    $a1, 0x5c($t9)
/* 096268 7F061738 50A0000E */  beql  $a1, $zero, .L7F061774
/* 09626C 7F06173C 24630001 */   addiu $v1, $v1, 1
/* 096270 7F061740 AFA3005C */  sw    $v1, 0x5c($sp)
/* 096274 7F061744 0FC1B1E7 */  jal   modelGetNodeRwData
/* 096278 7F061748 AFA60040 */   sw    $a2, 0x40($sp)
/* 09627C 7F06174C 8FA3005C */  lw    $v1, 0x5c($sp)
/* 096280 7F061750 8FA60040 */  lw    $a2, 0x40($sp)
/* 096284 7F061754 10400006 */  beqz  $v0, .L7F061770
/* 096288 7F061758 24070005 */   li    $a3, 5
/* 09628C 7F06175C 8E0E0034 */  lw    $t6, 0x34($s0)
/* 096290 7F061760 00E3C023 */  subu  $t8, $a3, $v1
/* 096294 7F061764 01D8682A */  slt   $t5, $t6, $t8
/* 096298 7F061768 39AD0001 */  xori  $t5, $t5, 1
/* 09629C 7F06176C AC4D0000 */  sw    $t5, ($v0)
.L7F061770:
/* 0962A0 7F061770 24630001 */  addiu $v1, $v1, 1
.L7F061774:
/* 0962A4 7F061774 1467FFD8 */  bne   $v1, $a3, .L7F0616D8
/* 0962A8 7F061778 24C60004 */   addiu $a2, $a2, 4
.L7F06177C:
/* 0962AC 7F06177C 0FC1BBF1 */  jal   modelUpdateNodeRelations
/* 0962B0 7F061780 8FA40044 */   lw    $a0, 0x44($sp)
/* 0962B4 7F061784 820F000C */  lb    $t7, 0xc($s0)
/* 0962B8 7F061788 8FB900FC */  lw    $t9, 0xfc($sp)
/* 0962BC 7F06178C 11E00011 */  beqz  $t7, .L7F0617D4
/* 0962C0 7F061790 272EFFFC */   addiu $t6, $t9, -4
/* 0962C4 7F061794 2DC10014 */  sltiu $at, $t6, 0x14
/* 0962C8 7F061798 1020000E */  beqz  $at, .L7F0617D4
/* 0962CC 7F06179C 000E7080 */   sll   $t6, $t6, 2
/* 0962D0 7F0617A0 3C018005 */  lui   $at, %hi(jpt_weapon_bullet_type)
/* 0962D4 7F0617A4 002E0821 */  addu  $at, $at, $t6
/* 0962D8 7F0617A8 8C2E3E38 */  lw    $t6, %lo(jpt_weapon_bullet_type)($at)
/* 0962DC 7F0617AC 01C00008 */  jr    $t6
/* 0962E0 7F0617B0 00000000 */   nop
weapon_bullet_type_pistol:
/* 0962E4 7F0617B4 0FC186FD */  jal   sub_GAME_7F061BF4
/* 0962E8 7F0617B8 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0962EC 7F0617BC 8E180030 */  lw    $t8, 0x30($s0)
/* 0962F0 7F0617C0 270D0001 */  addiu $t5, $t8, 1
/* 0962F4 7F0617C4 10000003 */  b     .L7F0617D4
/* 0962F8 7F0617C8 AE0D0030 */   sw    $t5, 0x30($s0)
weapon_bullet_type_none:
/* 0962FC 7F0617CC 0FC186FD */  jal   sub_GAME_7F061BF4
/* 096300 7F0617D0 8FA402A8 */   lw    $a0, 0x2a8($sp)
weapon_bullet_type_shotgun_mine:
.L7F0617D4:
/* 096304 7F0617D4 8FAF00FC */  lw    $t7, 0xfc($sp)
.L7F0617D8:
/* 096308 7F0617D8 24010019 */  li    $at, 25
/* 09630C 7F0617DC 55E10004 */  bnel  $t7, $at, .L7F0617F0
/* 096310 7F0617E0 8219000C */   lb    $t9, 0xc($s0)
/* 096314 7F0617E4 0FC17E4A */  jal   sub_GAME_7F05F928
/* 096318 7F0617E8 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 09631C 7F0617EC 8219000C */  lb    $t9, 0xc($s0)
.L7F0617F0:
/* 096320 7F0617F0 3C048008 */  lui   $a0, %hi(g_CurrentPlayer)
/* 096324 7F0617F4 53200046 */  beql  $t9, $zero, .L7F061910
/* 096328 7F0617F8 8FBF0034 */   lw    $ra, 0x34($sp)
/* 09632C 7F0617FC 0FC225DE */  jal   bondviewGetPlayerStanHeight
/* 096330 7F061800 8C84A0B0 */   lw    $a0, %lo(g_CurrentPlayer)($a0)
/* 096334 7F061804 44050000 */  mfc1  $a1, $f0
/* 096338 7F061808 0FC1A142 */  jal   sub_GAME_7F068508
/* 09633C 7F06180C 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096340 7F061810 8FAE00FC */  lw    $t6, 0xfc($sp)
/* 096344 7F061814 24010018 */  li    $at, 24
/* 096348 7F061818 8FB800FC */  lw    $t8, 0xfc($sp)
/* 09634C 7F06181C 55C10006 */  bnel  $t6, $at, .L7F061838
/* 096350 7F061820 2401001A */   li    $at, 26
/* 096354 7F061824 0FC17DCF */  jal   sub_GAME_7F05F73C
/* 096358 7F061828 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 09635C 7F06182C 10000038 */  b     .L7F061910
/* 096360 7F061830 8FBF0034 */   lw    $ra, 0x34($sp)
/* 096364 7F061834 2401001A */  li    $at, 26
.L7F061838:
/* 096368 7F061838 17010005 */  bne   $t8, $at, .L7F061850
/* 09636C 7F06183C 8FAD00FC */   lw    $t5, 0xfc($sp)
/* 096370 7F061840 0FC17B89 */  jal   generate_player_thrown_grenade
/* 096374 7F061844 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096378 7F061848 10000031 */  b     .L7F061910
/* 09637C 7F06184C 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061850:
/* 096380 7F061850 24010019 */  li    $at, 25
/* 096384 7F061854 15A10005 */  bne   $t5, $at, .L7F06186C
/* 096388 7F061858 8FAF00FC */   lw    $t7, 0xfc($sp)
/* 09638C 7F06185C 0FC17ED9 */  jal   gunFireTankShell
/* 096390 7F061860 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096394 7F061864 1000002A */  b     .L7F061910
/* 096398 7F061868 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F06186C:
/* 09639C 7F06186C 24010003 */  li    $at, 3
/* 0963A0 7F061870 15E10005 */  bne   $t7, $at, .L7F061888
/* 0963A4 7F061874 8FB900FC */   lw    $t9, 0xfc($sp)
/* 0963A8 7F061878 0FC17C27 */  jal   generate_player_thrown_knife
/* 0963AC 7F06187C 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0963B0 7F061880 10000023 */  b     .L7F061910
/* 0963B4 7F061884 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061888:
/* 0963B8 7F061888 2401001D */  li    $at, 29
/* 0963BC 7F06188C 1321000F */  beq   $t9, $at, .L7F0618CC
/* 0963C0 7F061890 2401001C */   li    $at, 28
/* 0963C4 7F061894 1321000D */  beq   $t9, $at, .L7F0618CC
/* 0963C8 7F061898 2401001B */   li    $at, 27
/* 0963CC 7F06189C 1321000B */  beq   $t9, $at, .L7F0618CC
/* 0963D0 7F0618A0 24010021 */   li    $at, 33
/* 0963D4 7F0618A4 13210009 */  beq   $t9, $at, .L7F0618CC
/* 0963D8 7F0618A8 2401002F */   li    $at, 47
/* 0963DC 7F0618AC 13210007 */  beq   $t9, $at, .L7F0618CC
/* 0963E0 7F0618B0 24010030 */   li    $at, 48
/* 0963E4 7F0618B4 13210005 */  beq   $t9, $at, .L7F0618CC
/* 0963E8 7F0618B8 2401003D */   li    $at, 61
/* 0963EC 7F0618BC 13210003 */  beq   $t9, $at, .L7F0618CC
/* 0963F0 7F0618C0 24010022 */   li    $at, 34
/* 0963F4 7F0618C4 17210005 */  bne   $t9, $at, .L7F0618DC
/* 0963F8 7F0618C8 8FAE00FC */   lw    $t6, 0xfc($sp)
.L7F0618CC:
/* 0963FC 7F0618CC 0FC17CD6 */  jal   generate_player_thrown_object
/* 096400 7F0618D0 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096404 7F0618D4 1000000E */  b     .L7F061910
/* 096408 7F0618D8 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F0618DC:
/* 09640C 7F0618DC 24010023 */  li    $at, 35
/* 096410 7F0618E0 15C10005 */  bne   $t6, $at, .L7F0618F8
/* 096414 7F0618E4 8FB800FC */   lw    $t8, 0xfc($sp)
/* 096418 7F0618E8 0FC17DCF */  jal   sub_GAME_7F05F73C
/* 09641C 7F0618EC 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096420 7F0618F0 10000007 */  b     .L7F061910
/* 096424 7F0618F4 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F0618F8:
/* 096428 7F0618F8 24010024 */  li    $at, 36
/* 09642C 7F0618FC 57010004 */  bnel  $t8, $at, .L7F061910
/* 096430 7F061900 8FBF0034 */   lw    $ra, 0x34($sp)
/* 096434 7F061904 0FC17DCF */  jal   sub_GAME_7F05F73C
/* 096438 7F061908 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 09643C 7F06190C 8FBF0034 */  lw    $ra, 0x34($sp)
.L7F061910:
/* 096440 7F061910 8FB00030 */  lw    $s0, 0x30($sp)
/* 096444 7F061914 27BD02A8 */  addiu $sp, $sp, 0x2a8
/* 096448 7F061918 03E00008 */  jr    $ra
/* 09644C 7F06191C 00000000 */   nop
)
#endif

#ifdef VERSION_JP
GLOBAL_ASM(
.late_rodata
glabel D_80053DE0
.word 0x3f733333 /*0.94999999*/
glabel D_80053DE4
.word 0x3d4cccd0 /*0.050000012*/
glabel D_80053DE8
.word 0x3f19999a /*0.60000002*/
glabel D_80053DEC
.word 0x3e99999a /*0.30000001*/
glabel D_80053DF0
.word 0x3f19999a /*0.60000002*/
glabel D_80053DF4
.word 0x3e99999a /*0.30000001*/
glabel D_80053DF8
.word 0x3f19999a /*0.60000002*/
glabel D_80053DFC
.word 0x3e99999a /*0.30000001*/
glabel D_80053E00
.word 0x41de6666 /*27.799999*/
glabel D_80053E04
.word 0x3dccccce /*0.10000001*/
glabel D_80053E08
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E0C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E10
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E14
.word 0x3f060a92 /*0.52359879*/
glabel D_80053E18
.word 0x3f060a92 /*0.52359879*/
glabel D_80053E1C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E20
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E24
.word 0x3dccccce /*0.10000001*/
glabel D_80053E28
.word 0x3dccccce /*0.10000001*/
glabel D_80053E2C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E30
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E34
.word 0x40c90fdb /*6.2831855*/

/*D:80053E38*/
glabel jpt_weapon_bullet_type
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_shotgun_mine
.word weapon_bullet_type_shotgun_mine
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_none
.word weapon_bullet_type_none
.size jpt_weapon_bullet_type, . - jpt_weapon_bullet_type

.text
glabel handles_firing_or_throwing_weapon_in_hand
/* 095058 7F0604E8 27BDFD58 */  addiu $sp, $sp, -0x2a8
/* 09505C 7F0604EC 3C0F8003 */  lui   $t7, %hi(D_80035C40) # $t7, 0x8003
/* 095060 7F0604F0 AFBF0034 */  sw    $ra, 0x34($sp)
/* 095064 7F0604F4 AFB00030 */  sw    $s0, 0x30($sp)
/* 095068 7F0604F8 25EF5C80 */  addiu $t7, %lo(D_80035C40) # addiu $t7, $t7, 0x5c80
/* 09506C 7F0604FC 8DE10000 */  lw    $at, ($t7)
/* 095070 7F060500 27AE0194 */  addiu $t6, $sp, 0x194
/* 095074 7F060504 8DED0004 */  lw    $t5, 4($t7)
/* 095078 7F060508 ADC10000 */  sw    $at, ($t6)
/* 09507C 7F06050C 8DE10008 */  lw    $at, 8($t7)
/* 095080 7F060510 0004C0C0 */  sll   $t8, $a0, 3
/* 095084 7F060514 0304C023 */  subu  $t8, $t8, $a0
/* 095088 7F060518 0018C080 */  sll   $t8, $t8, 2
/* 09508C 7F06051C 0304C021 */  addu  $t8, $t8, $a0
/* 095090 7F060520 3C198008 */  lui   $t9, %hi(g_CurrentPlayer) # $t9, 0x8008
/* 095094 7F060524 ADCD0004 */  sw    $t5, 4($t6)
/* 095098 7F060528 ADC10008 */  sw    $at, 8($t6)
/* 09509C 7F06052C 8F39A120 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 0950A0 7F060530 0018C080 */  sll   $t8, $t8, 2
/* 0950A4 7F060534 0304C021 */  addu  $t8, $t8, $a0
/* 0950A8 7F060538 0018C0C0 */  sll   $t8, $t8, 3
/* 0950AC 7F06053C 03388021 */  addu  $s0, $t9, $t8
/* 0950B0 7F060540 AFA0010C */  sw    $zero, 0x10c($sp)
/* 0950B4 7F060544 AFA00108 */  sw    $zero, 0x108($sp)
/* 0950B8 7F060548 26100870 */  addiu $s0, $s0, 0x870
/* 0950BC 7F06054C 0FC177D9 */  jal   get_item_in_hand_or_watch_menu
/* 0950C0 7F060550 AFA402A8 */   sw    $a0, 0x2a8($sp)
/* 0950C4 7F060554 AFA200FC */  sw    $v0, 0xfc($sp)
/* 0950C8 7F060558 0FC17375 */  jal   get_ptr_item_statistics
/* 0950CC 7F06055C 00402025 */   move  $a0, $v0
/* 0950D0 7F060560 8FAE02A8 */  lw    $t6, 0x2a8($sp)
/* 0950D4 7F060564 AFA200F8 */  sw    $v0, 0xf8($sp)
/* 0950D8 7F060568 15C0002D */  bnez  $t6, .Ljp7F060620
/* 0950DC 7F06056C 00000000 */   nop
/* 0950E0 7F060570 0FC177D9 */  jal   get_item_in_hand_or_watch_menu
/* 0950E4 7F060574 24040001 */   li    $a0, 1
/* 0950E8 7F060578 00402025 */  move  $a0, $v0
/* 0950EC 7F06057C 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 0950F0 7F060580 24050800 */   li    $a1, 2048
/* 0950F4 7F060584 10400015 */  beqz  $v0, .Ljp7F0605DC
/* 0950F8 7F060588 3C018005 */   lui   $at, %hi(g_GlobalTimerDelta)
/* 0950FC 7F06058C 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8005
/* 095100 7F060590 C42083B4 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 095104 7F060594 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 095108 7F060598 44813000 */  mtc1  $at, $f6
/* 09510C 7F06059C 46000100 */  add.s $f4, $f0, $f0
/* 095110 7F0605A0 C60A01C4 */  lwc1  $f10, 0x1c4($s0)
/* 095114 7F0605A4 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 095118 7F0605A8 44819000 */  mtc1  $at, $f18
/* 09511C 7F0605AC 46062203 */  div.s $f8, $f4, $f6
/* 095120 7F0605B0 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 095124 7F0605B4 46085400 */  add.s $f16, $f10, $f8
/* 095128 7F0605B8 E61001C4 */  swc1  $f16, 0x1c4($s0)
/* 09512C 7F0605BC C60401C4 */  lwc1  $f4, 0x1c4($s0)
/* 095130 7F0605C0 4604903C */  c.lt.s $f18, $f4
/* 095134 7F0605C4 00000000 */  nop
/* 095138 7F0605C8 4500003F */  bc1f  .Ljp7F0606C8
/* 09513C 7F0605CC 00000000 */   nop
/* 095140 7F0605D0 44813000 */  mtc1  $at, $f6
/* 095144 7F0605D4 1000003C */  b     .Ljp7F0606C8
/* 095148 7F0605D8 E60601C4 */   swc1  $f6, 0x1c4($s0)
.Ljp7F0605DC:
/* 09514C 7F0605DC C42083B4 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 095150 7F0605E0 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 095154 7F0605E4 44814000 */  mtc1  $at, $f8
/* 095158 7F0605E8 46000280 */  add.s $f10, $f0, $f0
/* 09515C 7F0605EC C61201C4 */  lwc1  $f18, 0x1c4($s0)
/* 095160 7F0605F0 46085403 */  div.s $f16, $f10, $f8
/* 095164 7F0605F4 44805000 */  mtc1  $zero, $f10
/* 095168 7F0605F8 46109101 */  sub.s $f4, $f18, $f16
/* 09516C 7F0605FC E60401C4 */  swc1  $f4, 0x1c4($s0)
/* 095170 7F060600 C60601C4 */  lwc1  $f6, 0x1c4($s0)
/* 095174 7F060604 460A303C */  c.lt.s $f6, $f10
/* 095178 7F060608 00000000 */  nop
/* 09517C 7F06060C 4500002E */  bc1f  .Ljp7F0606C8
/* 095180 7F060610 00000000 */   nop
/* 095184 7F060614 44804000 */  mtc1  $zero, $f8
/* 095188 7F060618 1000002B */  b     .Ljp7F0606C8
/* 09518C 7F06061C E60801C4 */   swc1  $f8, 0x1c4($s0)
.Ljp7F060620:
/* 095190 7F060620 0FC177D9 */  jal   get_item_in_hand_or_watch_menu
/* 095194 7F060624 00002025 */   move  $a0, $zero
/* 095198 7F060628 00402025 */  move  $a0, $v0
/* 09519C 7F06062C 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 0951A0 7F060630 24050800 */   li    $a1, 2048
/* 0951A4 7F060634 10400013 */  beqz  $v0, .Ljp7F060684
/* 0951A8 7F060638 3C018005 */   lui   $at, %hi(g_GlobalTimerDelta)
/* 0951AC 7F06063C 3C01C000 */  li    $at, 0xC0000000 # -2.000000
/* 0951B0 7F060640 44811000 */  mtc1  $at, $f2
/* 0951B4 7F060644 3C018005 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8005
/* 0951B8 7F060648 C42083B4 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 0951BC 7F06064C 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 0951C0 7F060650 44818000 */  mtc1  $at, $f16
/* 0951C4 7F060654 46000480 */  add.s $f18, $f0, $f0
/* 0951C8 7F060658 C60601C4 */  lwc1  $f6, 0x1c4($s0)
/* 0951CC 7F06065C 46109103 */  div.s $f4, $f18, $f16
/* 0951D0 7F060660 46043281 */  sub.s $f10, $f6, $f4
/* 0951D4 7F060664 E60A01C4 */  swc1  $f10, 0x1c4($s0)
/* 0951D8 7F060668 C60801C4 */  lwc1  $f8, 0x1c4($s0)
/* 0951DC 7F06066C 4602403C */  c.lt.s $f8, $f2
/* 0951E0 7F060670 00000000 */  nop
/* 0951E4 7F060674 45000014 */  bc1f  .Ljp7F0606C8
/* 0951E8 7F060678 00000000 */   nop
/* 0951EC 7F06067C 10000012 */  b     .Ljp7F0606C8
/* 0951F0 7F060680 E60201C4 */   swc1  $f2, 0x1c4($s0)
.Ljp7F060684:
/* 0951F4 7F060684 C42083B4 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 0951F8 7F060688 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 0951FC 7F06068C 44818000 */  mtc1  $at, $f16
/* 095200 7F060690 46000480 */  add.s $f18, $f0, $f0
/* 095204 7F060694 C60401C4 */  lwc1  $f4, 0x1c4($s0)
/* 095208 7F060698 44804000 */  mtc1  $zero, $f8
/* 09520C 7F06069C 46109183 */  div.s $f6, $f18, $f16
/* 095210 7F0606A0 46062280 */  add.s $f10, $f4, $f6
/* 095214 7F0606A4 E60A01C4 */  swc1  $f10, 0x1c4($s0)
/* 095218 7F0606A8 C61201C4 */  lwc1  $f18, 0x1c4($s0)
/* 09521C 7F0606AC 4612403C */  c.lt.s $f8, $f18
/* 095220 7F0606B0 00000000 */  nop
/* 095224 7F0606B4 45000004 */  bc1f  .Ljp7F0606C8
/* 095228 7F0606B8 00000000 */   nop
/* 09522C 7F0606BC 44808000 */  mtc1  $zero, $f16
/* 095230 7F0606C0 00000000 */  nop
/* 095234 7F0606C4 E61001C4 */  swc1  $f16, 0x1c4($s0)
.Ljp7F0606C8:
/* 095238 7F0606C8 3C0F8003 */  lui   $t7, %hi(D_80035C4C) # $t7, 0x8003
/* 09523C 7F0606CC 25EF5C8C */  addiu $t7, %lo(D_80035C4C) # addiu $t7, $t7, 0x5c8c
/* 095240 7F0606D0 8DE10000 */  lw    $at, ($t7)
/* 095244 7F0606D4 27AC00E0 */  addiu $t4, $sp, 0xe0
/* 095248 7F0606D8 3C0E8003 */  lui   $t6, %hi(D_80035C58) # $t6, 0x8003
/* 09524C 7F0606DC AD810000 */  sw    $at, ($t4)
/* 095250 7F0606E0 8DF90004 */  lw    $t9, 4($t7)
/* 095254 7F0606E4 25CE5C98 */  addiu $t6, %lo(D_80035C58) # addiu $t6, $t6, 0x5c98
/* 095258 7F0606E8 27B800D4 */  addiu $t8, $sp, 0xd4
/* 09525C 7F0606EC AD990004 */  sw    $t9, 4($t4)
/* 095260 7F0606F0 8DE10008 */  lw    $at, 8($t7)
/* 095264 7F0606F4 3C0D8003 */  lui   $t5, %hi(D_80035C64) # $t5, 0x8003
/* 095268 7F0606F8 25AD5CA4 */  addiu $t5, %lo(D_80035C64) # addiu $t5, $t5, 0x5ca4
/* 09526C 7F0606FC AD810008 */  sw    $at, 8($t4)
/* 095270 7F060700 8DC10000 */  lw    $at, ($t6)
/* 095274 7F060704 8DCF0004 */  lw    $t7, 4($t6)
/* 095278 7F060708 27B900C8 */  addiu $t9, $sp, 0xc8
/* 09527C 7F06070C AF010000 */  sw    $at, ($t8)
/* 095280 7F060710 8DC10008 */  lw    $at, 8($t6)
/* 095284 7F060714 AF0F0004 */  sw    $t7, 4($t8)
/* 095288 7F060718 2403000C */  li    $v1, 12
/* 09528C 7F06071C AF010008 */  sw    $at, 8($t8)
/* 095290 7F060720 8DA10000 */  lw    $at, ($t5)
/* 095294 7F060724 8DAE0004 */  lw    $t6, 4($t5)
/* 095298 7F060728 AF210000 */  sw    $at, ($t9)
/* 09529C 7F06072C 8DA10008 */  lw    $at, 8($t5)
/* 0952A0 7F060730 AF2E0004 */  sw    $t6, 4($t9)
/* 0952A4 7F060734 AF210008 */  sw    $at, 8($t9)
/* 0952A8 7F060738 8E020198 */  lw    $v0, 0x198($s0)
/* 0952AC 7F06073C C604019C */  lwc1  $f4, 0x19c($s0)
/* 0952B0 7F060740 AFAC0014 */  sw    $t4, 0x14($sp)
/* 0952B4 7F060744 244F0003 */  addiu $t7, $v0, 3
/* 0952B8 7F060748 05E10004 */  bgez  $t7, .Ljp7F06075C
/* 0952BC 7F06074C 31F80003 */   andi  $t8, $t7, 3
/* 0952C0 7F060750 13000002 */  beqz  $t8, .Ljp7F06075C
/* 0952C4 7F060754 00000000 */   nop
/* 0952C8 7F060758 2718FFFC */  addiu $t8, $t8, -4
.Ljp7F06075C:
/* 0952CC 7F06075C 03030019 */  multu $t8, $v1
/* 0952D0 7F060760 244E0001 */  addiu $t6, $v0, 1
/* 0952D4 7F060764 E7A40010 */  swc1  $f4, 0x10($sp)
/* 0952D8 7F060768 0000C812 */  mflo  $t9
/* 0952DC 7F06076C 02194021 */  addu  $t0, $s0, $t9
/* 0952E0 7F060770 24590002 */  addiu $t9, $v0, 2
/* 0952E4 7F060774 00430019 */  multu $v0, $v1
/* 0952E8 7F060778 25040108 */  addiu $a0, $t0, 0x108
/* 0952EC 7F06077C AFA80044 */  sw    $t0, 0x44($sp)
/* 0952F0 7F060780 00006812 */  mflo  $t5
/* 0952F4 7F060784 020D4821 */  addu  $t1, $s0, $t5
/* 0952F8 7F060788 25250108 */  addiu $a1, $t1, 0x108
/* 0952FC 7F06078C 05C10004 */  bgez  $t6, .Ljp7F0607A0
/* 095300 7F060790 31CF0003 */   andi  $t7, $t6, 3
/* 095304 7F060794 11E00002 */  beqz  $t7, .Ljp7F0607A0
/* 095308 7F060798 00000000 */   nop
/* 09530C 7F06079C 25EFFFFC */  addiu $t7, $t7, -4
.Ljp7F0607A0:
/* 095310 7F0607A0 01E30019 */  multu $t7, $v1
/* 095314 7F0607A4 AFA90040 */  sw    $t1, 0x40($sp)
/* 095318 7F0607A8 0000C012 */  mflo  $t8
/* 09531C 7F0607AC 02185021 */  addu  $t2, $s0, $t8
/* 095320 7F0607B0 25460108 */  addiu $a2, $t2, 0x108
/* 095324 7F0607B4 07210004 */  bgez  $t9, .Ljp7F0607C8
/* 095328 7F0607B8 332D0003 */   andi  $t5, $t9, 3
/* 09532C 7F0607BC 11A00002 */  beqz  $t5, .Ljp7F0607C8
/* 095330 7F0607C0 00000000 */   nop
/* 095334 7F0607C4 25ADFFFC */  addiu $t5, $t5, -4
.Ljp7F0607C8:
/* 095338 7F0607C8 01A30019 */  multu $t5, $v1
/* 09533C 7F0607CC AFAA003C */  sw    $t2, 0x3c($sp)
/* 095340 7F0607D0 00007012 */  mflo  $t6
/* 095344 7F0607D4 020E5821 */  addu  $t3, $s0, $t6
/* 095348 7F0607D8 25670108 */  addiu $a3, $t3, 0x108
/* 09534C 7F0607DC 0FC16D07 */  jal   sub_GAME_7F05AEFC
/* 095350 7F0607E0 AFAB0038 */   sw    $t3, 0x38($sp)
/* 095354 7F0607E4 8FA40044 */  lw    $a0, 0x44($sp)
/* 095358 7F0607E8 8FA50040 */  lw    $a1, 0x40($sp)
/* 09535C 7F0607EC 8FA6003C */  lw    $a2, 0x3c($sp)
/* 095360 7F0607F0 8FA70038 */  lw    $a3, 0x38($sp)
/* 095364 7F0607F4 C606019C */  lwc1  $f6, 0x19c($s0)
/* 095368 7F0607F8 27AF00D4 */  addiu $t7, $sp, 0xd4
/* 09536C 7F0607FC AFAF0014 */  sw    $t7, 0x14($sp)
/* 095370 7F060800 24840138 */  addiu $a0, $a0, 0x138
/* 095374 7F060804 24A50138 */  addiu $a1, $a1, 0x138
/* 095378 7F060808 24C60138 */  addiu $a2, $a2, 0x138
/* 09537C 7F06080C 24E70138 */  addiu $a3, $a3, 0x138
/* 095380 7F060810 0FC16D07 */  jal   sub_GAME_7F05AEFC
/* 095384 7F060814 E7A60010 */   swc1  $f6, 0x10($sp)
/* 095388 7F060818 8FA40044 */  lw    $a0, 0x44($sp)
/* 09538C 7F06081C 8FA50040 */  lw    $a1, 0x40($sp)
/* 095390 7F060820 8FA6003C */  lw    $a2, 0x3c($sp)
/* 095394 7F060824 8FA70038 */  lw    $a3, 0x38($sp)
/* 095398 7F060828 C60A019C */  lwc1  $f10, 0x19c($s0)
/* 09539C 7F06082C 27B800C8 */  addiu $t8, $sp, 0xc8
/* 0953A0 7F060830 AFB80014 */  sw    $t8, 0x14($sp)
/* 0953A4 7F060834 24840168 */  addiu $a0, $a0, 0x168
/* 0953A8 7F060838 24A50168 */  addiu $a1, $a1, 0x168
/* 0953AC 7F06083C 24C60168 */  addiu $a2, $a2, 0x168
/* 0953B0 7F060840 24E70168 */  addiu $a3, $a3, 0x168
/* 0953B4 7F060844 0FC16D07 */  jal   sub_GAME_7F05AEFC
/* 0953B8 7F060848 E7AA0010 */   swc1  $f10, 0x10($sp)
/* 0953BC 7F06084C 3C028008 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8008
/* 0953C0 7F060850 8C42A120 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 0953C4 7F060854 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 0953C8 7F060858 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 0953CC 7F06085C C4520FC0 */  lwc1  $f18, 0xfc0($v0)
/* 0953D0 7F060860 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 0953D4 7F060864 46124402 */  mul.s $f16, $f8, $f18
/* 0953D8 7F060868 C7A800E8 */  lwc1  $f8, 0xe8($sp)
/* 0953DC 7F06086C E7B000E0 */  swc1  $f16, 0xe0($sp)
/* 0953E0 7F060870 C4460FC0 */  lwc1  $f6, 0xfc0($v0)
/* 0953E4 7F060874 46062282 */  mul.s $f10, $f4, $f6
/* 0953E8 7F060878 E7AA00E4 */  swc1  $f10, 0xe4($sp)
/* 0953EC 7F06087C C4520FC0 */  lwc1  $f18, 0xfc0($v0)
/* 0953F0 7F060880 46124102 */  mul.s $f4, $f8, $f18
/* 0953F4 7F060884 E7A400E8 */  swc1  $f4, 0xe8($sp)
/* 0953F8 7F060888 C60601AC */  lwc1  $f6, 0x1ac($s0)
/* 0953FC 7F06088C 46068200 */  add.s $f8, $f16, $f6
/* 095400 7F060890 E7A800E0 */  swc1  $f8, 0xe0($sp)
/* 095404 7F060894 C61201B0 */  lwc1  $f18, 0x1b0($s0)
/* 095408 7F060898 46125100 */  add.s $f4, $f10, $f18
/* 09540C 7F06089C 0FC17876 */  jal   sub_GAME_7F05DCB8
/* 095410 7F0608A0 E7A400E4 */   swc1  $f4, 0xe4($sp)
/* 095414 7F0608A4 C7B000E0 */  lwc1  $f16, 0xe0($sp)
/* 095418 7F0608A8 3C028005 */  lui   $v0, %hi(g_ClockTimer) # $v0, 0x8005
/* 09541C 7F0608AC 244283A4 */  addiu $v0, %lo(g_ClockTimer) # addiu $v0, $v0, -0x7c5c
/* 095420 7F0608B0 46008180 */  add.s $f6, $f16, $f0
/* 095424 7F0608B4 8C590000 */  lw    $t9, ($v0)
/* 095428 7F0608B8 00001825 */  move  $v1, $zero
/* 09542C 7F0608BC 1B200035 */  blez  $t9, .Ljp7F060994
/* 095430 7F0608C0 E7A600E0 */   swc1  $f6, 0xe0($sp)
/* 095434 7F0608C4 3C018005 */  lui   $at, %hi(D_80053DE0) # $at, 0x8005
/* 095438 7F0608C8 C4203E10 */  lwc1  $f0, %lo(D_80053DE0)($at)
/* 09543C 7F0608CC C60A00E4 */  lwc1  $f10, 0xe4($s0)
.Ljp7F0608D0:
/* 095440 7F0608D0 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 095444 7F0608D4 C60600E8 */  lwc1  $f6, 0xe8($s0)
/* 095448 7F0608D8 460A0482 */  mul.s $f18, $f0, $f10
/* 09544C 7F0608DC 24630001 */  addiu $v1, $v1, 1
/* 095450 7F0608E0 46060282 */  mul.s $f10, $f0, $f6
/* 095454 7F0608E4 46124100 */  add.s $f4, $f8, $f18
/* 095458 7F0608E8 E60400E4 */  swc1  $f4, 0xe4($s0)
/* 09545C 7F0608EC C7B000E4 */  lwc1  $f16, 0xe4($sp)
/* 095460 7F0608F0 C60400EC */  lwc1  $f4, 0xec($s0)
/* 095464 7F0608F4 460A8200 */  add.s $f8, $f16, $f10
/* 095468 7F0608F8 46040182 */  mul.s $f6, $f0, $f4
/* 09546C 7F0608FC E60800E8 */  swc1  $f8, 0xe8($s0)
/* 095470 7F060900 C7B200E8 */  lwc1  $f18, 0xe8($sp)
/* 095474 7F060904 C60800F0 */  lwc1  $f8, 0xf0($s0)
/* 095478 7F060908 46069400 */  add.s $f16, $f18, $f6
/* 09547C 7F06090C 46080102 */  mul.s $f4, $f0, $f8
/* 095480 7F060910 E61000EC */  swc1  $f16, 0xec($s0)
/* 095484 7F060914 C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 095488 7F060918 C61000F4 */  lwc1  $f16, 0xf4($s0)
/* 09548C 7F06091C 46045480 */  add.s $f18, $f10, $f4
/* 095490 7F060920 46100202 */  mul.s $f8, $f0, $f16
/* 095494 7F060924 E61200F0 */  swc1  $f18, 0xf0($s0)
/* 095498 7F060928 C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 09549C 7F06092C C61200F8 */  lwc1  $f18, 0xf8($s0)
/* 0954A0 7F060930 46083280 */  add.s $f10, $f6, $f8
/* 0954A4 7F060934 46120402 */  mul.s $f16, $f0, $f18
/* 0954A8 7F060938 E60A00F4 */  swc1  $f10, 0xf4($s0)
/* 0954AC 7F06093C C7A400DC */  lwc1  $f4, 0xdc($sp)
/* 0954B0 7F060940 C60A00FC */  lwc1  $f10, 0xfc($s0)
/* 0954B4 7F060944 46102180 */  add.s $f6, $f4, $f16
/* 0954B8 7F060948 460A0482 */  mul.s $f18, $f0, $f10
/* 0954BC 7F06094C E60600F8 */  swc1  $f6, 0xf8($s0)
/* 0954C0 7F060950 C7A800C8 */  lwc1  $f8, 0xc8($sp)
/* 0954C4 7F060954 C6060100 */  lwc1  $f6, 0x100($s0)
/* 0954C8 7F060958 46124100 */  add.s $f4, $f8, $f18
/* 0954CC 7F06095C 46060282 */  mul.s $f10, $f0, $f6
/* 0954D0 7F060960 E60400FC */  swc1  $f4, 0xfc($s0)
/* 0954D4 7F060964 C7B000CC */  lwc1  $f16, 0xcc($sp)
/* 0954D8 7F060968 C6040104 */  lwc1  $f4, 0x104($s0)
/* 0954DC 7F06096C 460A8200 */  add.s $f8, $f16, $f10
/* 0954E0 7F060970 46040182 */  mul.s $f6, $f0, $f4
/* 0954E4 7F060974 E6080100 */  swc1  $f8, 0x100($s0)
/* 0954E8 7F060978 C7B200D0 */  lwc1  $f18, 0xd0($sp)
/* 0954EC 7F06097C 46069400 */  add.s $f16, $f18, $f6
/* 0954F0 7F060980 E6100104 */  swc1  $f16, 0x104($s0)
/* 0954F4 7F060984 8C4D0000 */  lw    $t5, ($v0)
/* 0954F8 7F060988 006D082A */  slt   $at, $v1, $t5
/* 0954FC 7F06098C 5420FFD0 */  bnezl $at, .Ljp7F0608D0
/* 095500 7F060990 C60A00E4 */   lwc1  $f10, 0xe4($s0)
.Ljp7F060994:
/* 095504 7F060994 3C018005 */  lui   $at, %hi(D_80053DE4) # $at, 0x8005
/* 095508 7F060998 C4203E14 */  lwc1  $f0, %lo(D_80053DE4)($at)
/* 09550C 7F06099C C60A00E4 */  lwc1  $f10, 0xe4($s0)
/* 095510 7F0609A0 C60400E8 */  lwc1  $f4, 0xe8($s0)
/* 095514 7F0609A4 C60600EC */  lwc1  $f6, 0xec($s0)
/* 095518 7F0609A8 46005202 */  mul.s $f8, $f10, $f0
/* 09551C 7F0609AC C60A00F0 */  lwc1  $f10, 0xf0($s0)
/* 095520 7F0609B0 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 095524 7F0609B4 46002482 */  mul.s $f18, $f4, $f0
/* 095528 7F0609B8 C60400F4 */  lwc1  $f4, 0xf4($s0)
/* 09552C 7F0609BC 46003402 */  mul.s $f16, $f6, $f0
/* 095530 7F0609C0 E60800C0 */  swc1  $f8, 0xc0($s0)
/* 095534 7F0609C4 C60600F8 */  lwc1  $f6, 0xf8($s0)
/* 095538 7F0609C8 46005202 */  mul.s $f8, $f10, $f0
/* 09553C 7F0609CC E61200C4 */  swc1  $f18, 0xc4($s0)
/* 095540 7F0609D0 C60A00FC */  lwc1  $f10, 0xfc($s0)
/* 095544 7F0609D4 46002482 */  mul.s $f18, $f4, $f0
/* 095548 7F0609D8 E61000C8 */  swc1  $f16, 0xc8($s0)
/* 09554C 7F0609DC C6040100 */  lwc1  $f4, 0x100($s0)
/* 095550 7F0609E0 46003402 */  mul.s $f16, $f6, $f0
/* 095554 7F0609E4 E60800CC */  swc1  $f8, 0xcc($s0)
/* 095558 7F0609E8 C6060104 */  lwc1  $f6, 0x104($s0)
/* 09555C 7F0609EC 46005202 */  mul.s $f8, $f10, $f0
/* 095560 7F0609F0 E61200D0 */  swc1  $f18, 0xd0($s0)
/* 095564 7F0609F4 46002482 */  mul.s $f18, $f4, $f0
/* 095568 7F0609F8 E61000D4 */  swc1  $f16, 0xd4($s0)
/* 09556C 7F0609FC 46003402 */  mul.s $f16, $f6, $f0
/* 095570 7F060A00 E60800D8 */  swc1  $f8, 0xd8($s0)
/* 095574 7F060A04 E61200DC */  swc1  $f18, 0xdc($s0)
/* 095578 7F060A08 14800009 */  bnez  $a0, .Ljp7F060A30
/* 09557C 7F060A0C E61000E0 */   swc1  $f16, 0xe0($s0)
/* 095580 7F060A10 0FC17882 */  jal   gunSetHorizontalOffset
/* 095584 7F060A14 00000000 */   nop
/* 095588 7F060A18 C60800C0 */  lwc1  $f8, 0xc0($s0)
/* 09558C 7F060A1C C60A01B8 */  lwc1  $f10, 0x1b8($s0)
/* 095590 7F060A20 46080100 */  add.s $f4, $f0, $f8
/* 095594 7F060A24 46045480 */  add.s $f18, $f10, $f4
/* 095598 7F060A28 10000008 */  b     .Ljp7F060A4C
/* 09559C 7F060A2C E7B20194 */   swc1  $f18, 0x194($sp)
.Ljp7F060A30:
/* 0955A0 7F060A30 0FC17882 */  jal   gunSetHorizontalOffset
/* 0955A4 7F060A34 00000000 */   nop
/* 0955A8 7F060A38 C60600C0 */  lwc1  $f6, 0xc0($s0)
/* 0955AC 7F060A3C C60801B8 */  lwc1  $f8, 0x1b8($s0)
/* 0955B0 7F060A40 46060400 */  add.s $f16, $f0, $f6
/* 0955B4 7F060A44 46088281 */  sub.s $f10, $f16, $f8
/* 0955B8 7F060A48 E7AA0194 */  swc1  $f10, 0x194($sp)
.Ljp7F060A4C:
/* 0955BC 7F060A4C 8FAE00F8 */  lw    $t6, 0xf8($sp)
/* 0955C0 7F060A50 C61200C4 */  lwc1  $f18, 0xc4($s0)
/* 0955C4 7F060A54 C61001BC */  lwc1  $f16, 0x1bc($s0)
/* 0955C8 7F060A58 C5C40008 */  lwc1  $f4, 8($t6)
/* 0955CC 7F060A5C 8FA400FC */  lw    $a0, 0xfc($sp)
/* 0955D0 7F060A60 24010019 */  li    $at, 25
/* 0955D4 7F060A64 46122180 */  add.s $f6, $f4, $f18
/* 0955D8 7F060A68 46068200 */  add.s $f8, $f16, $f6
/* 0955DC 7F060A6C E7A80198 */  swc1  $f8, 0x198($sp)
/* 0955E0 7F060A70 C60400C8 */  lwc1  $f4, 0xc8($s0)
/* 0955E4 7F060A74 C5CA000C */  lwc1  $f10, 0xc($t6)
/* 0955E8 7F060A78 C61001C0 */  lwc1  $f16, 0x1c0($s0)
/* 0955EC 7F060A7C 46045480 */  add.s $f18, $f10, $f4
/* 0955F0 7F060A80 46128180 */  add.s $f6, $f16, $f18
/* 0955F4 7F060A84 10810005 */  beq   $a0, $at, .Ljp7F060A9C
/* 0955F8 7F060A88 E7A6019C */   swc1  $f6, 0x19c($sp)
/* 0955FC 7F060A8C 2401001E */  li    $at, 30
/* 095600 7F060A90 10810002 */  beq   $a0, $at, .Ljp7F060A9C
/* 095604 7F060A94 24010017 */   li    $at, 23
/* 095608 7F060A98 14810028 */  bne   $a0, $at, .Ljp7F060B3C
.Ljp7F060A9C:
/* 09560C 7F060A9C 3C028008 */   lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8008
/* 095610 7F060AA0 8C42A120 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 095614 7F060AA4 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 095618 7F060AA8 44810000 */  mtc1  $at, $f0
/* 09561C 7F060AAC C44A00A0 */  lwc1  $f10, 0xa0($v0)
/* 095620 7F060AB0 C7A80198 */  lwc1  $f8, 0x198($sp)
/* 095624 7F060AB4 3C014040 */  li    $at, 0x40400000 # 3.000000
/* 095628 7F060AB8 46005103 */  div.s $f4, $f10, $f0
/* 09562C 7F060ABC 44819000 */  mtc1  $at, $f18
/* 095630 7F060AC0 24010019 */  li    $at, 25
/* 095634 7F060AC4 46044400 */  add.s $f16, $f8, $f4
/* 095638 7F060AC8 C7A4019C */  lwc1  $f4, 0x19c($sp)
/* 09563C 7F060ACC E7B00198 */  swc1  $f16, 0x198($sp)
/* 095640 7F060AD0 C44600A0 */  lwc1  $f6, 0xa0($v0)
/* 095644 7F060AD4 46069282 */  mul.s $f10, $f18, $f6
/* 095648 7F060AD8 46005203 */  div.s $f8, $f10, $f0
/* 09564C 7F060ADC 46082400 */  add.s $f16, $f4, $f8
/* 095650 7F060AE0 14810014 */  bne   $a0, $at, .Ljp7F060B34
/* 095654 7F060AE4 E7B0019C */   swc1  $f16, 0x19c($sp)
/* 095658 7F060AE8 0FC2969A */  jal   cur_player_get_screen_setting
/* 09565C 7F060AEC 00000000 */   nop
/* 095660 7F060AF0 24010001 */  li    $at, 1
/* 095664 7F060AF4 5041000B */  beql  $v0, $at, .Ljp7F060B24
/* 095668 7F060AF8 3C014040 */   lui   $at, 0x4040
/* 09566C 7F060AFC 0FC2969A */  jal   cur_player_get_screen_setting
/* 095670 7F060B00 00000000 */   nop
/* 095674 7F060B04 24010002 */  li    $at, 2
/* 095678 7F060B08 50410006 */  beql  $v0, $at, .Ljp7F060B24
/* 09567C 7F060B0C 3C014040 */   lui   $at, 0x4040
/* 095680 7F060B10 0FC296A0 */  jal   get_screen_ratio
/* 095684 7F060B14 00000000 */   nop
/* 095688 7F060B18 24010001 */  li    $at, 1
/* 09568C 7F060B1C 14410005 */  bne   $v0, $at, .Ljp7F060B34
/* 095690 7F060B20 3C014040 */   li    $at, 0x40400000 # 3.000000
.Ljp7F060B24:
/* 095694 7F060B24 44813000 */  mtc1  $at, $f6
/* 095698 7F060B28 C7B20198 */  lwc1  $f18, 0x198($sp)
/* 09569C 7F060B2C 46069281 */  sub.s $f10, $f18, $f6
/* 0956A0 7F060B30 E7AA0198 */  swc1  $f10, 0x198($sp)
.Ljp7F060B34:
/* 0956A4 7F060B34 1000002C */  b     .Ljp7F060BE8
/* 0956A8 7F060B38 8FA400FC */   lw    $a0, 0xfc($sp)
.Ljp7F060B3C:
/* 0956AC 7F060B3C 2401001F */  li    $at, 31
/* 0956B0 7F060B40 14810016 */  bne   $a0, $at, .Ljp7F060B9C
/* 0956B4 7F060B44 3C028008 */   lui   $v0, %hi(g_CurrentPlayer)
/* 0956B8 7F060B48 3C028008 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8008
/* 0956BC 7F060B4C 8C42A120 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 0956C0 7F060B50 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 0956C4 7F060B54 44810000 */  mtc1  $at, $f0
/* 0956C8 7F060B58 3C014020 */  li    $at, 0x40200000 # 2.500000
/* 0956CC 7F060B5C 44812000 */  mtc1  $at, $f4
/* 0956D0 7F060B60 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 0956D4 7F060B64 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 0956D8 7F060B68 3C0140F0 */  li    $at, 0x40F00000 # 7.500000
/* 0956DC 7F060B6C 46082402 */  mul.s $f16, $f4, $f8
/* 0956E0 7F060B70 44812000 */  mtc1  $at, $f4
/* 0956E4 7F060B74 46008483 */  div.s $f18, $f16, $f0
/* 0956E8 7F060B78 46123280 */  add.s $f10, $f6, $f18
/* 0956EC 7F060B7C C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 0956F0 7F060B80 E7AA0198 */  swc1  $f10, 0x198($sp)
/* 0956F4 7F060B84 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 0956F8 7F060B88 46082402 */  mul.s $f16, $f4, $f8
/* 0956FC 7F060B8C 46008183 */  div.s $f6, $f16, $f0
/* 095700 7F060B90 46069280 */  add.s $f10, $f18, $f6
/* 095704 7F060B94 10000014 */  b     .Ljp7F060BE8
/* 095708 7F060B98 E7AA019C */   swc1  $f10, 0x19c($sp)
.Ljp7F060B9C:
/* 09570C 7F060B9C 8C42A120 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 095710 7F060BA0 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 095714 7F060BA4 44810000 */  mtc1  $at, $f0
/* 095718 7F060BA8 3C0140A0 */  li    $at, 0x40A00000 # 5.000000
/* 09571C 7F060BAC 44812000 */  mtc1  $at, $f4
/* 095720 7F060BB0 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 095724 7F060BB4 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 095728 7F060BB8 3C014170 */  li    $at, 0x41700000 # 15.000000
/* 09572C 7F060BBC 46082402 */  mul.s $f16, $f4, $f8
/* 095730 7F060BC0 44812000 */  mtc1  $at, $f4
/* 095734 7F060BC4 46008483 */  div.s $f18, $f16, $f0
/* 095738 7F060BC8 46123280 */  add.s $f10, $f6, $f18
/* 09573C 7F060BCC C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 095740 7F060BD0 E7AA0198 */  swc1  $f10, 0x198($sp)
/* 095744 7F060BD4 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 095748 7F060BD8 46082402 */  mul.s $f16, $f4, $f8
/* 09574C 7F060BDC 46008183 */  div.s $f6, $f16, $f0
/* 095750 7F060BE0 46069280 */  add.s $f10, $f18, $f6
/* 095754 7F060BE4 E7AA019C */  swc1  $f10, 0x19c($sp)
.Ljp7F060BE8:
/* 095758 7F060BE8 820F000C */  lb    $t7, 0xc($s0)
/* 09575C 7F060BEC 11E00047 */  beqz  $t7, .Ljp7F060D0C
/* 095760 7F060BF0 00000000 */   nop
/* 095764 7F060BF4 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 095768 7F060BF8 24050020 */   li    $a1, 32
/* 09576C 7F060BFC 10400043 */  beqz  $v0, .Ljp7F060D0C
/* 095770 7F060C00 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095774 7F060C04 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 095778 7F060C08 24050040 */   li    $a1, 64
/* 09577C 7F060C0C 10400016 */  beqz  $v0, .Ljp7F060C68
/* 095780 7F060C10 00000000 */   nop
/* 095784 7F060C14 0C002918 */  jal   randomGetNext
/* 095788 7F060C18 00000000 */   nop
/* 09578C 7F060C1C 44822000 */  mtc1  $v0, $f4
/* 095790 7F060C20 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095794 7F060C24 04410004 */  bgez  $v0, .Ljp7F060C38
/* 095798 7F060C28 46802220 */   cvt.s.w $f8, $f4
/* 09579C 7F060C2C 44818000 */  mtc1  $at, $f16
/* 0957A0 7F060C30 00000000 */  nop
/* 0957A4 7F060C34 46104200 */  add.s $f8, $f8, $f16
.Ljp7F060C38:
/* 0957A8 7F060C38 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0957AC 7F060C3C 44819000 */  mtc1  $at, $f18
/* 0957B0 7F060C40 3C018005 */  lui   $at, %hi(D_80053DE8) # $at, 0x8005
/* 0957B4 7F060C44 C42A3E18 */  lwc1  $f10, %lo(D_80053DE8)($at)
/* 0957B8 7F060C48 46124182 */  mul.s $f6, $f8, $f18
/* 0957BC 7F060C4C 3C018005 */  lui   $at, %hi(D_80053DEC) # $at, 0x8005
/* 0957C0 7F060C50 C4303E1C */  lwc1  $f16, %lo(D_80053DEC)($at)
/* 0957C4 7F060C54 C7B20194 */  lwc1  $f18, 0x194($sp)
/* 0957C8 7F060C58 460A3102 */  mul.s $f4, $f6, $f10
/* 0957CC 7F060C5C 46048201 */  sub.s $f8, $f16, $f4
/* 0957D0 7F060C60 46089180 */  add.s $f6, $f18, $f8
/* 0957D4 7F060C64 E7A60194 */  swc1  $f6, 0x194($sp)
.Ljp7F060C68:
/* 0957D8 7F060C68 0C002918 */  jal   randomGetNext
/* 0957DC 7F060C6C 00000000 */   nop
/* 0957E0 7F060C70 44825000 */  mtc1  $v0, $f10
/* 0957E4 7F060C74 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0957E8 7F060C78 04410004 */  bgez  $v0, .Ljp7F060C8C
/* 0957EC 7F060C7C 46805420 */   cvt.s.w $f16, $f10
/* 0957F0 7F060C80 44812000 */  mtc1  $at, $f4
/* 0957F4 7F060C84 00000000 */  nop
/* 0957F8 7F060C88 46048400 */  add.s $f16, $f16, $f4
.Ljp7F060C8C:
/* 0957FC 7F060C8C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 095800 7F060C90 44819000 */  mtc1  $at, $f18
/* 095804 7F060C94 3C018005 */  lui   $at, %hi(D_80053DF0) # $at, 0x8005
/* 095808 7F060C98 C4263E20 */  lwc1  $f6, %lo(D_80053DF0)($at)
/* 09580C 7F060C9C 46128202 */  mul.s $f8, $f16, $f18
/* 095810 7F060CA0 3C018005 */  lui   $at, %hi(D_80053DF4) # $at, 0x8005
/* 095814 7F060CA4 C4243E24 */  lwc1  $f4, %lo(D_80053DF4)($at)
/* 095818 7F060CA8 C7B20198 */  lwc1  $f18, 0x198($sp)
/* 09581C 7F060CAC 46064282 */  mul.s $f10, $f8, $f6
/* 095820 7F060CB0 460A2401 */  sub.s $f16, $f4, $f10
/* 095824 7F060CB4 46109200 */  add.s $f8, $f18, $f16
/* 095828 7F060CB8 0C002918 */  jal   randomGetNext
/* 09582C 7F060CBC E7A80198 */   swc1  $f8, 0x198($sp)
/* 095830 7F060CC0 44823000 */  mtc1  $v0, $f6
/* 095834 7F060CC4 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 095838 7F060CC8 04410004 */  bgez  $v0, .Ljp7F060CDC
/* 09583C 7F060CCC 46803120 */   cvt.s.w $f4, $f6
/* 095840 7F060CD0 44815000 */  mtc1  $at, $f10
/* 095844 7F060CD4 00000000 */  nop
/* 095848 7F060CD8 460A2100 */  add.s $f4, $f4, $f10
.Ljp7F060CDC:
/* 09584C 7F060CDC 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 095850 7F060CE0 44819000 */  mtc1  $at, $f18
/* 095854 7F060CE4 3C018005 */  lui   $at, %hi(D_80053DF8) # $at, 0x8005
/* 095858 7F060CE8 C4283E28 */  lwc1  $f8, %lo(D_80053DF8)($at)
/* 09585C 7F060CEC 46122402 */  mul.s $f16, $f4, $f18
/* 095860 7F060CF0 3C018005 */  lui    $at, %hi(D_80053DFC)
/* 095864 7F060CF4 C42A3E2C */  lwc1  $f10, %lo(D_80053DFC)($at)
/* 095868 7F060CF8 C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 09586C 7F060CFC 46088182 */  mul.s $f6, $f16, $f8
/* 095870 7F060D00 46065101 */  sub.s $f4, $f10, $f6
/* 095874 7F060D04 46049400 */  add.s $f16, $f18, $f4
/* 095878 7F060D08 E7B0019C */  swc1  $f16, 0x19c($sp)
.Ljp7F060D0C:
/* 09587C 7F060D0C 0FC1E2A5 */  jal   getPlayer_c_screenwidth
/* 095880 7F060D10 00000000 */   nop
/* 095884 7F060D14 0FC1E2A5 */  jal   getPlayer_c_screenwidth
/* 095888 7F060D18 E7A00048 */   swc1  $f0, 0x48($sp)
/* 09588C 7F060D1C 0FC1E2AD */  jal   getPlayer_c_screenleft
/* 095890 7F060D20 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 095894 7F060D24 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 095898 7F060D28 3C188008 */  lui   $t8, %hi(g_CurrentPlayer) # $t8, 0x8008
/* 09589C 7F060D2C 8F18A120 */  lw    $t8, %lo(g_CurrentPlayer)($t8)
/* 0958A0 7F060D30 44811000 */  mtc1  $at, $f2
/* 0958A4 7F060D34 C7A6004C */  lwc1  $f6, 0x4c($sp)
/* 0958A8 7F060D38 C7080FFC */  lwc1  $f8, 0xffc($t8)
/* 0958AC 7F060D3C 8FB900F8 */  lw    $t9, 0xf8($sp)
/* 0958B0 7F060D40 46023482 */  mul.s $f18, $f6, $f2
/* 0958B4 7F060D44 46004281 */  sub.s $f10, $f8, $f0
/* 0958B8 7F060D48 C7300018 */  lwc1  $f16, 0x18($t9)
/* 0958BC 7F060D4C C7A60048 */  lwc1  $f6, 0x48($sp)
/* 0958C0 7F060D50 46125101 */  sub.s $f4, $f10, $f18
/* 0958C4 7F060D54 46102202 */  mul.s $f8, $f4, $f16
/* 0958C8 7F060D58 C7A40194 */  lwc1  $f4, 0x194($sp)
/* 0958CC 7F060D5C 46023282 */  mul.s $f10, $f6, $f2
/* 0958D0 7F060D60 460A4483 */  div.s $f18, $f8, $f10
/* 0958D4 7F060D64 46122400 */  add.s $f16, $f4, $f18
/* 0958D8 7F060D68 0FC1E2B1 */  jal   getPlayer_c_screentop
/* 0958DC 7F060D6C E7B00194 */   swc1  $f16, 0x194($sp)
/* 0958E0 7F060D70 0FC1E2A9 */  jal   getPlayer_c_screenheight
/* 0958E4 7F060D74 E7A00050 */   swc1  $f0, 0x50($sp)
/* 0958E8 7F060D78 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 0958EC 7F060D7C 3C0D8008 */  lui   $t5, %hi(g_CurrentPlayer) # $t5, 0x8008
/* 0958F0 7F060D80 8DADA120 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 0958F4 7F060D84 44813000 */  mtc1  $at, $f6
/* 0958F8 7F060D88 C7A40050 */  lwc1  $f4, 0x50($sp)
/* 0958FC 7F060D8C C5AA1000 */  lwc1  $f10, 0x1000($t5)
/* 095900 7F060D90 46060202 */  mul.s $f8, $f0, $f6
/* 095904 7F060D94 46045481 */  sub.s $f18, $f10, $f4
/* 095908 7F060D98 4612403C */  c.lt.s $f8, $f18
/* 09590C 7F060D9C 00000000 */  nop
/* 095910 7F060DA0 4500001A */  bc1f  .Ljp7F060E0C
/* 095914 7F060DA4 00000000 */   nop
/* 095918 7F060DA8 0FC1E2A9 */  jal   getPlayer_c_screenheight
/* 09591C 7F060DAC 00000000 */   nop
/* 095920 7F060DB0 0FC1E2A9 */  jal   getPlayer_c_screenheight
/* 095924 7F060DB4 E7A00048 */   swc1  $f0, 0x48($sp)
/* 095928 7F060DB8 0FC1E2B1 */  jal   getPlayer_c_screentop
/* 09592C 7F060DBC E7A0004C */   swc1  $f0, 0x4c($sp)
/* 095930 7F060DC0 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 095934 7F060DC4 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer) # $t6, 0x8008
/* 095938 7F060DC8 8DCEA120 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 09593C 7F060DCC 44811000 */  mtc1  $at, $f2
/* 095940 7F060DD0 C7AA004C */  lwc1  $f10, 0x4c($sp)
/* 095944 7F060DD4 C5D01000 */  lwc1  $f16, 0x1000($t6)
/* 095948 7F060DD8 8FAF00F8 */  lw    $t7, 0xf8($sp)
/* 09594C 7F060DDC 46025102 */  mul.s $f4, $f10, $f2
/* 095950 7F060DE0 46008181 */  sub.s $f6, $f16, $f0
/* 095954 7F060DE4 C5F20014 */  lwc1  $f18, 0x14($t7)
/* 095958 7F060DE8 C7AA0048 */  lwc1  $f10, 0x48($sp)
/* 09595C 7F060DEC 46043201 */  sub.s $f8, $f6, $f4
/* 095960 7F060DF0 46124402 */  mul.s $f16, $f8, $f18
/* 095964 7F060DF4 C7A80198 */  lwc1  $f8, 0x198($sp)
/* 095968 7F060DF8 46025182 */  mul.s $f6, $f10, $f2
/* 09596C 7F060DFC 46068103 */  div.s $f4, $f16, $f6
/* 095970 7F060E00 46044481 */  sub.s $f18, $f8, $f4
/* 095974 7F060E04 1000001A */  b     .Ljp7F060E70
/* 095978 7F060E08 E7B20198 */   swc1  $f18, 0x198($sp)
.Ljp7F060E0C:
/* 09597C 7F060E0C 0FC1E2A9 */  jal   getPlayer_c_screenheight
/* 095980 7F060E10 00000000 */   nop
/* 095984 7F060E14 0FC1E2A9 */  jal   getPlayer_c_screenheight
/* 095988 7F060E18 E7A00048 */   swc1  $f0, 0x48($sp)
/* 09598C 7F060E1C 0FC1E2B1 */  jal   getPlayer_c_screentop
/* 095990 7F060E20 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 095994 7F060E24 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 095998 7F060E28 3C188008 */  lui   $t8, %hi(g_CurrentPlayer) # $t8, 0x8008
/* 09599C 7F060E2C 8F18A120 */  lw    $t8, %lo(g_CurrentPlayer)($t8)
/* 0959A0 7F060E30 44818000 */  mtc1  $at, $f16
/* 0959A4 7F060E34 C7AA004C */  lwc1  $f10, 0x4c($sp)
/* 0959A8 7F060E38 C7081000 */  lwc1  $f8, 0x1000($t8)
/* 0959AC 7F060E3C 8FB900F8 */  lw    $t9, 0xf8($sp)
/* 0959B0 7F060E40 46105182 */  mul.s $f6, $f10, $f16
/* 0959B4 7F060E44 46004101 */  sub.s $f4, $f8, $f0
/* 0959B8 7F060E48 C72A0010 */  lwc1  $f10, 0x10($t9)
/* 0959BC 7F060E4C C7A80048 */  lwc1  $f8, 0x48($sp)
/* 0959C0 7F060E50 46062481 */  sub.s $f18, $f4, $f6
/* 0959C4 7F060E54 44812000 */  mtc1  $at, $f4
/* 0959C8 7F060E58 460A9402 */  mul.s $f16, $f18, $f10
/* 0959CC 7F060E5C C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 0959D0 7F060E60 46044182 */  mul.s $f6, $f8, $f4
/* 0959D4 7F060E64 46068483 */  div.s $f18, $f16, $f6
/* 0959D8 7F060E68 46125201 */  sub.s $f8, $f10, $f18
/* 0959DC 7F060E6C E7A80198 */  swc1  $f8, 0x198($sp)
.Ljp7F060E70:
/* 0959E0 7F060E70 0FC172CD */  jal   sub_GAME_7F05C614
/* 0959E4 7F060E74 00000000 */   nop
/* 0959E8 7F060E78 0FC1613C */  jal   matrix_4x4_set_identity
/* 0959EC 7F060E7C 27A40154 */   addiu $a0, $sp, 0x154
/* 0959F0 7F060E80 8FA200FC */  lw    $v0, 0xfc($sp)
/* 0959F4 7F060E84 2401001E */  li    $at, 30
/* 0959F8 7F060E88 10410002 */  beq   $v0, $at, .Ljp7F060E94
/* 0959FC 7F060E8C 24010017 */   li    $at, 23
/* 095A00 7F060E90 14410010 */  bne   $v0, $at, .Ljp7F060ED4
.Ljp7F060E94:
/* 095A04 7F060E94 3C0D8003 */   lui   $t5, %hi(D_80035C70) # $t5, 0x8003
/* 095A08 7F060E98 25AD5CB0 */  addiu $t5, %lo(D_80035C70) # addiu $t5, $t5, 0x5cb0
/* 095A0C 7F060E9C 8DA10000 */  lw    $at, ($t5)
/* 095A10 7F060EA0 27A400B8 */  addiu $a0, $sp, 0xb8
/* 095A14 7F060EA4 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095A18 7F060EA8 AC810000 */  sw    $at, ($a0)
/* 095A1C 7F060EAC 8DAF0004 */  lw    $t7, 4($t5)
/* 095A20 7F060EB0 AC8F0004 */  sw    $t7, 4($a0)
/* 095A24 7F060EB4 8DA10008 */  lw    $at, 8($t5)
/* 095A28 7F060EB8 0FC1630D */  jal   matrix_4x4_set_rotation_around_xyz
/* 095A2C 7F060EBC AC810008 */   sw    $at, 8($a0)
/* 095A30 7F060EC0 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095A34 7F060EC4 0FC1616E */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095A38 7F060EC8 27A50154 */   addiu $a1, $sp, 0x154
/* 095A3C 7F060ECC 10000039 */  b     .Ljp7F060FB4
/* 095A40 7F060ED0 8E0D00BC */   lw    $t5, 0xbc($s0)
.Ljp7F060ED4:
/* 095A44 7F060ED4 2401001F */  li    $at, 31
/* 095A48 7F060ED8 14410010 */  bne   $v0, $at, .Ljp7F060F1C
/* 095A4C 7F060EDC 3C188003 */   lui   $t8, %hi(D_80035C7C) # $t8, 0x8003
/* 095A50 7F060EE0 27185CBC */  addiu $t8, %lo(D_80035C7C) # addiu $t8, $t8, 0x5cbc
/* 095A54 7F060EE4 8F010000 */  lw    $at, ($t8)
/* 095A58 7F060EE8 27A400AC */  addiu $a0, $sp, 0xac
/* 095A5C 7F060EEC 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095A60 7F060EF0 AC810000 */  sw    $at, ($a0)
/* 095A64 7F060EF4 8F0E0004 */  lw    $t6, 4($t8)
/* 095A68 7F060EF8 AC8E0004 */  sw    $t6, 4($a0)
/* 095A6C 7F060EFC 8F010008 */  lw    $at, 8($t8)
/* 095A70 7F060F00 0FC1630D */  jal   matrix_4x4_set_rotation_around_xyz
/* 095A74 7F060F04 AC810008 */   sw    $at, 8($a0)
/* 095A78 7F060F08 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095A7C 7F060F0C 0FC1616E */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095A80 7F060F10 27A50154 */   addiu $a1, $sp, 0x154
/* 095A84 7F060F14 10000027 */  b     .Ljp7F060FB4
/* 095A88 7F060F18 8E0D00BC */   lw    $t5, 0xbc($s0)
.Ljp7F060F1C:
/* 095A8C 7F060F1C 24010001 */  li    $at, 1
/* 095A90 7F060F20 14410023 */  bne   $v0, $at, .Ljp7F060FB0
/* 095A94 7F060F24 3C0D8008 */   lui   $t5, %hi(g_CurrentPlayer) # $t5, 0x8008
/* 095A98 7F060F28 8DADA120 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 095A9C 7F060F2C 24010011 */  li    $at, 17
/* 095AA0 7F060F30 3C198003 */  lui   $t9, %hi(D_80035C88) # $t9, 0x8003
/* 095AA4 7F060F34 8DAF2A38 */  lw    $t7, 0x2a38($t5)
/* 095AA8 7F060F38 27395CC8 */  addiu $t9, %lo(D_80035C88) # addiu $t9, $t9, 0x5cc8
/* 095AAC 7F060F3C 55E1001D */  bnel  $t7, $at, .Ljp7F060FB4
/* 095AB0 7F060F40 8E0D00BC */   lw    $t5, 0xbc($s0)
/* 095AB4 7F060F44 8F210000 */  lw    $at, ($t9)
/* 095AB8 7F060F48 27A400A0 */  addiu $a0, $sp, 0xa0
/* 095ABC 7F060F4C 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095AC0 7F060F50 AC810000 */  sw    $at, ($a0)
/* 095AC4 7F060F54 8F2E0004 */  lw    $t6, 4($t9)
/* 095AC8 7F060F58 AC8E0004 */  sw    $t6, 4($a0)
/* 095ACC 7F060F5C 8F210008 */  lw    $at, 8($t9)
/* 095AD0 7F060F60 0FC1630D */  jal   matrix_4x4_set_rotation_around_xyz
/* 095AD4 7F060F64 AC810008 */   sw    $at, 8($a0)
/* 095AD8 7F060F68 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095ADC 7F060F6C 0FC1616E */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095AE0 7F060F70 27A50154 */   addiu $a1, $sp, 0x154
/* 095AE4 7F060F74 3C01C020 */  li    $at, 0xC0200000 # -2.500000
/* 095AE8 7F060F78 44818000 */  mtc1  $at, $f16
/* 095AEC 7F060F7C C7A40194 */  lwc1  $f4, 0x194($sp)
/* 095AF0 7F060F80 3C018005 */  lui   $at, %hi(D_80053E00) # $at, 0x8005
/* 095AF4 7F060F84 C4323E30 */  lwc1  $f18, %lo(D_80053E00)($at)
/* 095AF8 7F060F88 46102180 */  add.s $f6, $f4, $f16
/* 095AFC 7F060F8C 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 095B00 7F060F90 C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 095B04 7F060F94 44818000 */  mtc1  $at, $f16
/* 095B08 7F060F98 C7A4019C */  lwc1  $f4, 0x19c($sp)
/* 095B0C 7F060F9C E7A60194 */  swc1  $f6, 0x194($sp)
/* 095B10 7F060FA0 46125200 */  add.s $f8, $f10, $f18
/* 095B14 7F060FA4 46102180 */  add.s $f6, $f4, $f16
/* 095B18 7F060FA8 E7A80198 */  swc1  $f8, 0x198($sp)
/* 095B1C 7F060FAC E7A6019C */  swc1  $f6, 0x19c($sp)
.Ljp7F060FB0:
/* 095B20 7F060FB0 8E0D00BC */  lw    $t5, 0xbc($s0)
.Ljp7F060FB4:
/* 095B24 7F060FB4 51A00017 */  beql  $t5, $zero, .Ljp7F061014
/* 095B28 7F060FB8 44802000 */   mtc1  $zero, $f4
/* 095B2C 7F060FBC C7AA0194 */  lwc1  $f10, 0x194($sp)
/* 095B30 7F060FC0 C61200AC */  lwc1  $f18, 0xac($s0)
/* 095B34 7F060FC4 C7A40198 */  lwc1  $f4, 0x198($sp)
/* 095B38 7F060FC8 2604007C */  addiu $a0, $s0, 0x7c
/* 095B3C 7F060FCC 46125200 */  add.s $f8, $f10, $f18
/* 095B40 7F060FD0 C7AA019C */  lwc1  $f10, 0x19c($sp)
/* 095B44 7F060FD4 27A50154 */  addiu $a1, $sp, 0x154
/* 095B48 7F060FD8 E7A80194 */  swc1  $f8, 0x194($sp)
/* 095B4C 7F060FDC C61000B0 */  lwc1  $f16, 0xb0($s0)
/* 095B50 7F060FE0 46102180 */  add.s $f6, $f4, $f16
/* 095B54 7F060FE4 E7A60198 */  swc1  $f6, 0x198($sp)
/* 095B58 7F060FE8 C61200B4 */  lwc1  $f18, 0xb4($s0)
/* 095B5C 7F060FEC 46125200 */  add.s $f8, $f10, $f18
/* 095B60 7F060FF0 0FC1616E */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095B64 7F060FF4 E7A8019C */   swc1  $f8, 0x19c($sp)
/* 095B68 7F060FF8 44800000 */  mtc1  $zero, $f0
/* 095B6C 7F060FFC 00000000 */  nop
/* 095B70 7F061000 E7A00184 */  swc1  $f0, 0x184($sp)
/* 095B74 7F061004 E7A00188 */  swc1  $f0, 0x188($sp)
/* 095B78 7F061008 1000000A */  b     .Ljp7F061034
/* 095B7C 7F06100C E7A0018C */   swc1  $f0, 0x18c($sp)
/* 095B80 7F061010 44802000 */  mtc1  $zero, $f4
.Ljp7F061014:
/* 095B84 7F061014 44808000 */  mtc1  $zero, $f16
/* 095B88 7F061018 44803000 */  mtc1  $zero, $f6
/* 095B8C 7F06101C 44805000 */  mtc1  $zero, $f10
/* 095B90 7F061020 44800000 */  mtc1  $zero, $f0
/* 095B94 7F061024 E6040078 */  swc1  $f4, 0x78($s0)
/* 095B98 7F061028 E610006C */  swc1  $f16, 0x6c($s0)
/* 095B9C 7F06102C E6060070 */  swc1  $f6, 0x70($s0)
/* 095BA0 7F061030 E60A0074 */  swc1  $f10, 0x74($s0)
.Ljp7F061034:
/* 095BA4 7F061034 C61200CC */  lwc1  $f18, 0xcc($s0)
/* 095BA8 7F061038 44050000 */  mfc1  $a1, $f0
/* 095BAC 7F06103C 44060000 */  mfc1  $a2, $f0
/* 095BB0 7F061040 E7B20010 */  swc1  $f18, 0x10($sp)
/* 095BB4 7F061044 C60800D0 */  lwc1  $f8, 0xd0($s0)
/* 095BB8 7F061048 44070000 */  mfc1  $a3, $f0
/* 095BBC 7F06104C 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095BC0 7F061050 E7A80014 */  swc1  $f8, 0x14($sp)
/* 095BC4 7F061054 C60400D4 */  lwc1  $f4, 0xd4($s0)
/* 095BC8 7F061058 E7A40018 */  swc1  $f4, 0x18($sp)
/* 095BCC 7F06105C C61000D8 */  lwc1  $f16, 0xd8($s0)
/* 095BD0 7F061060 E7B0001C */  swc1  $f16, 0x1c($sp)
/* 095BD4 7F061064 C60600DC */  lwc1  $f6, 0xdc($s0)
/* 095BD8 7F061068 E7A60020 */  swc1  $f6, 0x20($sp)
/* 095BDC 7F06106C C60A00E0 */  lwc1  $f10, 0xe0($s0)
/* 095BE0 7F061070 0FC1678A */  jal   matrix_4x4_set_basis_and_position_target
/* 095BE4 7F061074 E7AA0024 */   swc1  $f10, 0x24($sp)
/* 095BE8 7F061078 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095BEC 7F06107C 0FC1616E */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095BF0 7F061080 27A50154 */   addiu $a1, $sp, 0x154
/* 095BF4 7F061084 C7B20194 */  lwc1  $f18, 0x194($sp)
/* 095BF8 7F061088 C60801C8 */  lwc1  $f8, 0x1c8($s0)
/* 095BFC 7F06108C C7B00198 */  lwc1  $f16, 0x198($sp)
/* 095C00 7F061090 C60601CC */  lwc1  $f6, 0x1cc($s0)
/* 095C04 7F061094 46089101 */  sub.s $f4, $f18, $f8
/* 095C08 7F061098 C60801D0 */  lwc1  $f8, 0x1d0($s0)
/* 095C0C 7F06109C C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 095C10 7F0610A0 46068281 */  sub.s $f10, $f16, $f6
/* 095C14 7F0610A4 44062000 */  mfc1  $a2, $f4
/* 095C18 7F0610A8 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095C1C 7F0610AC 46089101 */  sub.s $f4, $f18, $f8
/* 095C20 7F0610B0 44075000 */  mfc1  $a3, $f10
/* 095C24 7F0610B4 24050000 */  li    $a1, 0
/* 095C28 7F0610B8 0FC16882 */  jal   matrix_4x4_align
/* 095C2C 7F0610BC E7A40010 */   swc1  $f4, 0x10($sp)
/* 095C30 7F0610C0 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 095C34 7F0610C4 0FC1616E */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095C38 7F0610C8 27A50154 */   addiu $a1, $sp, 0x154
/* 095C3C 7F0610CC 27A40154 */  addiu $a0, $sp, 0x154
/* 095C40 7F0610D0 0FC16150 */  jal   matrix_4x4_copy
/* 095C44 7F0610D4 27A50264 */   addiu $a1, $sp, 0x264
/* 095C48 7F0610D8 27A40194 */  addiu $a0, $sp, 0x194
/* 095C4C 7F0610DC 0FC163AE */  jal   matrix_4x4_set_position
/* 095C50 7F0610E0 27A50264 */   addiu $a1, $sp, 0x264
/* 095C54 7F0610E4 26050228 */  addiu $a1, $s0, 0x228
/* 095C58 7F0610E8 AFA50044 */  sw    $a1, 0x44($sp)
/* 095C5C 7F0610EC 0FC16150 */  jal   matrix_4x4_copy
/* 095C60 7F0610F0 27A40264 */   addiu $a0, $sp, 0x264
/* 095C64 7F0610F4 26040268 */  addiu $a0, $s0, 0x268
/* 095C68 7F0610F8 AFA40040 */  sw    $a0, 0x40($sp)
/* 095C6C 7F0610FC 0FC16150 */  jal   matrix_4x4_copy
/* 095C70 7F061100 260502A8 */   addiu $a1, $s0, 0x2a8
/* 095C74 7F061104 0FC1E28D */  jal   currentPlayerGetMatrix10D4
/* 095C78 7F061108 00000000 */   nop
/* 095C7C 7F06110C 00402025 */  move  $a0, $v0
/* 095C80 7F061110 8FA50044 */  lw    $a1, 0x44($sp)
/* 095C84 7F061114 0FC161AB */  jal   matrix_4x4_multiply_homogeneous
/* 095C88 7F061118 8FA60040 */   lw    $a2, 0x40($sp)
/* 095C8C 7F06111C 240F0001 */  li    $t7, 1
/* 095C90 7F061120 A20F000F */  sb    $t7, 0xf($s0)
/* 095C94 7F061124 0FC1755A */  jal   get_ptr_weapon_model_header_line
/* 095C98 7F061128 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095C9C 7F06112C 10400017 */  beqz  $v0, .Ljp7F06118C
/* 095CA0 7F061130 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095CA4 7F061134 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 095CA8 7F061138 24050800 */   li    $a1, 2048
/* 095CAC 7F06113C 10400013 */  beqz  $v0, .Ljp7F06118C
/* 095CB0 7F061140 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095CB4 7F061144 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 095CB8 7F061148 24052000 */   li    $a1, 8192
/* 095CBC 7F06114C 54400010 */  bnezl $v0, .Ljp7F061190
/* 095CC0 7F061150 A200000F */   sb    $zero, 0xf($s0)
/* 095CC4 7F061154 8E020024 */  lw    $v0, 0x24($s0)
/* 095CC8 7F061158 24010006 */  li    $at, 6
/* 095CCC 7F06115C 1041000B */  beq   $v0, $at, .Ljp7F06118C
/* 095CD0 7F061160 24010007 */   li    $at, 7
/* 095CD4 7F061164 5041000A */  beql  $v0, $at, .Ljp7F061190
/* 095CD8 7F061168 A200000F */   sb    $zero, 0xf($s0)
/* 095CDC 7F06116C 0FC174F7 */  jal   Gun_hand_without_item
/* 095CE0 7F061170 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 095CE4 7F061174 50400006 */  beql  $v0, $zero, .Ljp7F061190
/* 095CE8 7F061178 A200000F */   sb    $zero, 0xf($s0)
/* 095CEC 7F06117C 0FC17508 */  jal   get_itemtype_in_hand
/* 095CF0 7F061180 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 095CF4 7F061184 54400003 */  bnezl $v0, .Ljp7F061194
/* 095CF8 7F061188 8E18002C */   lw    $t8, 0x2c($s0)
.Ljp7F06118C:
/* 095CFC 7F06118C A200000F */  sb    $zero, 0xf($s0)
.Ljp7F061190:
/* 095D00 7F061190 8E18002C */  lw    $t8, 0x2c($s0)
.Ljp7F061194:
/* 095D04 7F061194 8FA400FC */  lw    $a0, 0xfc($sp)
/* 095D08 7F061198 5F000007 */  bgtzl $t8, .Ljp7F0611B8
/* 095D0C 7F06119C 8219000F */   lb    $t9, 0xf($s0)
/* 095D10 7F0611A0 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 095D14 7F0611A4 24050002 */   li    $a1, 2
/* 095D18 7F0611A8 50400003 */  beql  $v0, $zero, .Ljp7F0611B8
/* 095D1C 7F0611AC 8219000F */   lb    $t9, 0xf($s0)
/* 095D20 7F0611B0 A200000F */  sb    $zero, 0xf($s0)
/* 095D24 7F0611B4 8219000F */  lb    $t9, 0xf($s0)
.Ljp7F0611B8:
/* 095D28 7F0611B8 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer)
/* 095D2C 7F0611BC 8FAD02A8 */  lw    $t5, 0x2a8($sp)
/* 095D30 7F0611C0 532002D0 */  beql  $t9, $zero, .Ljp7F061D04
/* 095D34 7F0611C4 8FAE00FC */   lw    $t6, 0xfc($sp)
/* 095D38 7F0611C8 8DCEA120 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 095D3C 7F0611CC 000D7940 */  sll   $t7, $t5, 5
/* 095D40 7F0611D0 00001825 */  move  $v1, $zero
/* 095D44 7F0611D4 01CF1021 */  addu  $v0, $t6, $t7
/* 095D48 7F0611D8 8444081E */  lh    $a0, 0x81e($v0)
/* 095D4C 7F0611DC 24420810 */  addiu $v0, $v0, 0x810
/* 095D50 7F0611E0 AFA201A0 */  sw    $v0, 0x1a0($sp)
/* 095D54 7F0611E4 0004C180 */  sll   $t8, $a0, 6
/* 095D58 7F0611E8 03002025 */  move  $a0, $t8
/* 095D5C 7F0611EC 0FC2F8B1 */  jal   dynAllocate
/* 095D60 7F0611F0 AFA00100 */   sw    $zero, 0x100($sp)
/* 095D64 7F0611F4 8FB901A0 */  lw    $t9, 0x1a0($sp)
/* 095D68 7F0611F8 AFA202A4 */  sw    $v0, 0x2a4($sp)
/* 095D6C 7F0611FC 8FA30100 */  lw    $v1, 0x100($sp)
/* 095D70 7F061200 872D000E */  lh    $t5, 0xe($t9)
/* 095D74 7F061204 19A0000D */  blez  $t5, .Ljp7F06123C
/* 095D78 7F061208 00402025 */   move  $a0, $v0
/* 095D7C 7F06120C AFA30100 */  sw    $v1, 0x100($sp)
.Ljp7F061210:
/* 095D80 7F061210 0FC1613C */  jal   matrix_4x4_set_identity
/* 095D84 7F061214 AFA40044 */   sw    $a0, 0x44($sp)
/* 095D88 7F061218 8FAE01A0 */  lw    $t6, 0x1a0($sp)
/* 095D8C 7F06121C 8FA30100 */  lw    $v1, 0x100($sp)
/* 095D90 7F061220 8FA40044 */  lw    $a0, 0x44($sp)
/* 095D94 7F061224 85CF000E */  lh    $t7, 0xe($t6)
/* 095D98 7F061228 24630001 */  addiu $v1, $v1, 1
/* 095D9C 7F06122C 24840040 */  addiu $a0, $a0, 0x40
/* 095DA0 7F061230 006F082A */  slt   $at, $v1, $t7
/* 095DA4 7F061234 5420FFF6 */  bnezl $at, .Ljp7F061210
/* 095DA8 7F061238 AFA30100 */   sw    $v1, 0x100($sp)
.Ljp7F06123C:
/* 095DAC 7F06123C 0FC1D8B9 */  jal   modelCalculateRwDataLen
/* 095DB0 7F061240 8FA401A0 */   lw    $a0, 0x1a0($sp)
/* 095DB4 7F061244 260402F8 */  addiu $a0, $s0, 0x2f8
/* 095DB8 7F061248 8FA501A0 */  lw    $a1, 0x1a0($sp)
/* 095DBC 7F06124C AFA40044 */  sw    $a0, 0x44($sp)
/* 095DC0 7F061250 0FC1D956 */  jal   modelInit
/* 095DC4 7F061254 26060318 */   addiu $a2, $s0, 0x318
/* 095DC8 7F061258 8FA40044 */  lw    $a0, 0x44($sp)
/* 095DCC 7F06125C 0FC17BA6 */  jal   sub_GAME_7F05E978
/* 095DD0 7F061260 24050001 */   li    $a1, 1
/* 095DD4 7F061264 8FA40044 */  lw    $a0, 0x44($sp)
/* 095DD8 7F061268 0FC17BED */  jal   sub_GAME_7F05EA94
/* 095DDC 7F06126C 8205000E */   lb    $a1, 0xe($s0)
/* 095DE0 7F061270 8FB801A0 */  lw    $t8, 0x1a0($sp)
/* 095DE4 7F061274 8F020008 */  lw    $v0, 8($t8)
/* 095DE8 7F061278 8C440004 */  lw    $a0, 4($v0)
/* 095DEC 7F06127C 50800008 */  beql  $a0, $zero, .Ljp7F0612A0
/* 095DF0 7F061280 8C43000C */   lw    $v1, 0xc($v0)
/* 095DF4 7F061284 8C830004 */  lw    $v1, 4($a0)
/* 095DF8 7F061288 94790004 */  lhu   $t9, 4($v1)
/* 095DFC 7F06128C 00196880 */  sll   $t5, $t9, 2
/* 095E00 7F061290 020D7021 */  addu  $t6, $s0, $t5
/* 095E04 7F061294 25CF0318 */  addiu $t7, $t6, 0x318
/* 095E08 7F061298 AFAF010C */  sw    $t7, 0x10c($sp)
/* 095E0C 7F06129C 8C43000C */  lw    $v1, 0xc($v0)
.Ljp7F0612A0:
/* 095E10 7F0612A0 50600004 */  beql  $v1, $zero, .Ljp7F0612B4
/* 095E14 7F0612A4 8FB902A4 */   lw    $t9, 0x2a4($sp)
/* 095E18 7F0612A8 8C780004 */  lw    $t8, 4($v1)
/* 095E1C 7F0612AC AFB80108 */  sw    $t8, 0x108($sp)
/* 095E20 7F0612B0 8FB902A4 */  lw    $t9, 0x2a4($sp)
.Ljp7F0612B4:
/* 095E24 7F0612B4 24050400 */  li    $a1, 1024
/* 095E28 7F0612B8 AE190304 */  sw    $t9, 0x304($s0)
/* 095E2C 7F0612BC 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 095E30 7F0612C0 8FA400FC */   lw    $a0, 0xfc($sp)
/* 095E34 7F0612C4 10400008 */  beqz  $v0, .Ljp7F0612E8
/* 095E38 7F0612C8 00000000 */   nop
/* 095E3C 7F0612CC 8FAD02A8 */  lw    $t5, 0x2a8($sp)
/* 095E40 7F0612D0 24010001 */  li    $at, 1
/* 095E44 7F0612D4 15A10004 */  bne   $t5, $at, .Ljp7F0612E8
/* 095E48 7F0612D8 3C01BF80 */   li    $at, 0xBF800000 # -1.000000
/* 095E4C 7F0612DC 44816000 */  mtc1  $at, $f12
/* 095E50 7F0612E0 0FC163B5 */  jal   matrix_column_1_scalar_multiply
/* 095E54 7F0612E4 27A50264 */   addiu $a1, $sp, 0x264
.Ljp7F0612E8:
/* 095E58 7F0612E8 3C018005 */  lui   $at, %hi(D_80053E04) # $at, 0x8005
/* 095E5C 7F0612EC C42C3E34 */  lwc1  $f12, %lo(D_80053E04)($at)
/* 095E60 7F0612F0 0FC163E7 */  jal   matrix_scalar_multiply
/* 095E64 7F0612F4 27A50264 */   addiu $a1, $sp, 0x264
/* 095E68 7F0612F8 27A40264 */  addiu $a0, $sp, 0x264
/* 095E6C 7F0612FC 0FC16150 */  jal   matrix_4x4_copy
/* 095E70 7F061300 8FA502A4 */   lw    $a1, 0x2a4($sp)
/* 095E74 7F061304 8FAF01A0 */  lw    $t7, 0x1a0($sp)
/* 095E78 7F061308 3C0E8004 */  lui   $t6, %hi(skeleton_gun_revolver) # $t6, 0x8004
/* 095E7C 7F06130C 25CEC79C */  addiu $t6, %lo(skeleton_gun_revolver) # addiu $t6, $t6, -0x3864
/* 095E80 7F061310 8DF80004 */  lw    $t8, 4($t7)
/* 095E84 7F061314 55D80078 */  bnel  $t6, $t8, .Ljp7F0614F8
/* 095E88 7F061318 8FA2010C */   lw    $v0, 0x10c($sp)
/* 095E8C 7F06131C 8DE20008 */  lw    $v0, 8($t7)
/* 095E90 7F061320 8FB900FC */  lw    $t9, 0xfc($sp)
/* 095E94 7F061324 24010012 */  li    $at, 18
/* 095E98 7F061328 8C430010 */  lw    $v1, 0x10($v0)
/* 095E9C 7F06132C 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095EA0 7F061330 50600041 */  beql  $v1, $zero, .Ljp7F061438
/* 095EA4 7F061334 8C430014 */   lw    $v1, 0x14($v0)
/* 095EA8 7F061338 44806000 */  mtc1  $zero, $f12
/* 095EAC 7F06133C 17210021 */  bne   $t9, $at, .Ljp7F0613C4
/* 095EB0 7F061340 8C640004 */   lw    $a0, 4($v1)
/* 095EB4 7F061344 8E0D0024 */  lw    $t5, 0x24($s0)
/* 095EB8 7F061348 24010001 */  li    $at, 1
/* 095EBC 7F06134C 55A10012 */  bnel  $t5, $at, .Ljp7F061398
/* 095EC0 7F061350 8E18002C */   lw    $t8, 0x2c($s0)
/* 095EC4 7F061354 8E18002C */  lw    $t8, 0x2c($s0)
/* 095EC8 7F061358 8E0E0020 */  lw    $t6, 0x20($s0)
/* 095ECC 7F06135C 3C018005 */  lui   $at, %hi(D_80053E08) # $at, 0x8005
/* 095ED0 7F061360 00187880 */  sll   $t7, $t8, 2
/* 095ED4 7F061364 01F87823 */  subu  $t7, $t7, $t8
/* 095ED8 7F061368 000F7840 */  sll   $t7, $t7, 1
/* 095EDC 7F06136C 01CFC823 */  subu  $t9, $t6, $t7
/* 095EE0 7F061370 272D001E */  addiu $t5, $t9, 0x1e
/* 095EE4 7F061374 448D8000 */  mtc1  $t5, $f16
/* 095EE8 7F061378 C42A3E38 */  lwc1  $f10, %lo(D_80053E08)($at)
/* 095EEC 7F06137C 3C014210 */  li    $at, 0x42100000 # 36.000000
/* 095EF0 7F061380 468081A0 */  cvt.s.w $f6, $f16
/* 095EF4 7F061384 44814000 */  mtc1  $at, $f8
/* 095EF8 7F061388 460A3482 */  mul.s $f18, $f6, $f10
/* 095EFC 7F06138C 1000001D */  b     .Ljp7F061404
/* 095F00 7F061390 46089303 */   div.s $f12, $f18, $f8
/* 095F04 7F061394 8E18002C */  lw    $t8, 0x2c($s0)
.Ljp7F061398:
/* 095F08 7F061398 240E0006 */  li    $t6, 6
/* 095F0C 7F06139C 3C018005 */  lui   $at, %hi(D_80053E0C) # $at, 0x8005
/* 095F10 7F0613A0 01D87823 */  subu  $t7, $t6, $t8
/* 095F14 7F0613A4 448F2000 */  mtc1  $t7, $f4
/* 095F18 7F0613A8 C4263E3C */  lwc1  $f6, %lo(D_80053E0C)($at)
/* 095F1C 7F0613AC 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 095F20 7F0613B0 46802420 */  cvt.s.w $f16, $f4
/* 095F24 7F0613B4 44819000 */  mtc1  $at, $f18
/* 095F28 7F0613B8 46068282 */  mul.s $f10, $f16, $f6
/* 095F2C 7F0613BC 10000011 */  b     .Ljp7F061404
/* 095F30 7F0613C0 46125303 */   div.s $f12, $f10, $f18
.Ljp7F0613C4:
/* 095F34 7F0613C4 8E190024 */  lw    $t9, 0x24($s0)
/* 095F38 7F0613C8 24010001 */  li    $at, 1
/* 095F3C 7F0613CC 1721000D */  bne   $t9, $at, .Ljp7F061404
/* 095F40 7F0613D0 00000000 */   nop
/* 095F44 7F0613D4 8E020020 */  lw    $v0, 0x20($s0)
/* 095F48 7F0613D8 28410006 */  slti  $at, $v0, 6
/* 095F4C 7F0613DC 10200009 */  beqz  $at, .Ljp7F061404
/* 095F50 7F0613E0 00000000 */   nop
/* 095F54 7F0613E4 44824000 */  mtc1  $v0, $f8
/* 095F58 7F0613E8 3C018005 */  lui   $at, %hi(D_80053E10) # $at, 0x8005
/* 095F5C 7F0613EC C4303E40 */  lwc1  $f16, %lo(D_80053E10)($at)
/* 095F60 7F0613F0 46804120 */  cvt.s.w $f4, $f8
/* 095F64 7F0613F4 3C014210 */  li    $at, 0x42100000 # 36.000000
/* 095F68 7F0613F8 44815000 */  mtc1  $at, $f10
/* 095F6C 7F0613FC 46102182 */  mul.s $f6, $f4, $f16
/* 095F70 7F061400 460A3303 */  div.s $f12, $f6, $f10
.Ljp7F061404:
/* 095F74 7F061404 0FC162EA */  jal   matrix_4x4_set_rotation_around_z
/* 095F78 7F061408 AFA4009C */   sw    $a0, 0x9c($sp)
/* 095F7C 7F06140C 8FA4009C */  lw    $a0, 0x9c($sp)
/* 095F80 7F061410 0FC163AE */  jal   matrix_4x4_set_position
/* 095F84 7F061414 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 095F88 7F061418 8FA602A4 */  lw    $a2, 0x2a4($sp)
/* 095F8C 7F06141C 27A40264 */  addiu $a0, $sp, 0x264
/* 095F90 7F061420 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 095F94 7F061424 0FC1617A */  jal   matrix_4x4_multiply
/* 095F98 7F061428 24C600C0 */   addiu $a2, $a2, 0xc0
/* 095F9C 7F06142C 8FAD01A0 */  lw    $t5, 0x1a0($sp)
/* 095FA0 7F061430 8DA20008 */  lw    $v0, 8($t5)
/* 095FA4 7F061434 8C430014 */  lw    $v1, 0x14($v0)
.Ljp7F061438:
/* 095FA8 7F061438 5060002F */  beql  $v1, $zero, .Ljp7F0614F8
/* 095FAC 7F06143C 8FA2010C */   lw    $v0, 0x10c($sp)
/* 095FB0 7F061440 8E0E0024 */  lw    $t6, 0x24($s0)
/* 095FB4 7F061444 24010001 */  li    $at, 1
/* 095FB8 7F061448 8C640004 */  lw    $a0, 4($v1)
/* 095FBC 7F06144C 15C10022 */  bne   $t6, $at, .Ljp7F0614D8
/* 095FC0 7F061450 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 095FC4 7F061454 8E020020 */  lw    $v0, 0x20($s0)
/* 095FC8 7F061458 24180006 */  li    $t8, 6
/* 095FCC 7F06145C 28410003 */  slti  $at, $v0, 3
/* 095FD0 7F061460 1020000C */  beqz  $at, .Ljp7F061494
/* 095FD4 7F061464 03027823 */   subu  $t7, $t8, $v0
/* 095FD8 7F061468 44829000 */  mtc1  $v0, $f18
/* 095FDC 7F06146C 3C018005 */  lui   $at, %hi(D_80053E14) # $at, 0x8005
/* 095FE0 7F061470 C4303E44 */  lwc1  $f16, %lo(D_80053E14)($at)
/* 095FE4 7F061474 46809220 */  cvt.s.w $f8, $f18
/* 095FE8 7F061478 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 095FEC 7F06147C 44815000 */  mtc1  $at, $f10
/* 095FF0 7F061480 46004107 */  neg.s $f4, $f8
/* 095FF4 7F061484 46102002 */  mul.s $f0, $f4, $f16
/* 095FF8 7F061488 46000180 */  add.s $f6, $f0, $f0
/* 095FFC 7F06148C 1000000B */  b     .Ljp7F0614BC
/* 096000 7F061490 460A3303 */   div.s $f12, $f6, $f10
.Ljp7F061494:
/* 096004 7F061494 448F9000 */  mtc1  $t7, $f18
/* 096008 7F061498 3C018005 */  lui   $at, %hi(D_80053E18) # $at, 0x8005
/* 09600C 7F06149C C4303E48 */  lwc1  $f16, %lo(D_80053E18)($at)
/* 096010 7F0614A0 46809220 */  cvt.s.w $f8, $f18
/* 096014 7F0614A4 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 096018 7F0614A8 44815000 */  mtc1  $at, $f10
/* 09601C 7F0614AC 46004107 */  neg.s $f4, $f8
/* 096020 7F0614B0 46102002 */  mul.s $f0, $f4, $f16
/* 096024 7F0614B4 46000180 */  add.s $f6, $f0, $f0
/* 096028 7F0614B8 460A3303 */  div.s $f12, $f6, $f10
.Ljp7F0614BC:
/* 09602C 7F0614BC 0FC162A4 */  jal   matrix_4x4_set_rotation_around_x
/* 096030 7F0614C0 AFA40094 */   sw    $a0, 0x94($sp)
/* 096034 7F0614C4 8FA40094 */  lw    $a0, 0x94($sp)
/* 096038 7F0614C8 0FC163AE */  jal   matrix_4x4_set_position
/* 09603C 7F0614CC 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 096040 7F0614D0 10000004 */  b     .Ljp7F0614E4
/* 096044 7F0614D4 8FA602A4 */   lw    $a2, 0x2a4($sp)
.Ljp7F0614D8:
/* 096048 7F0614D8 0FC163A1 */  jal   matrix_4x4_set_identity_and_position
/* 09604C 7F0614DC 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 096050 7F0614E0 8FA602A4 */  lw    $a2, 0x2a4($sp)
.Ljp7F0614E4:
/* 096054 7F0614E4 27A40264 */  addiu $a0, $sp, 0x264
/* 096058 7F0614E8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 09605C 7F0614EC 0FC1617A */  jal   matrix_4x4_multiply
/* 096060 7F0614F0 24C60100 */   addiu $a2, $a2, 0x100
/* 096064 7F0614F4 8FA2010C */  lw    $v0, 0x10c($sp)
.Ljp7F0614F8:
/* 096068 7F0614F8 50400003 */  beql  $v0, $zero, .Ljp7F061508
/* 09606C 7F0614FC 8FB90108 */   lw    $t9, 0x108($sp)
/* 096070 7F061500 AC400000 */  sw    $zero, ($v0)
/* 096074 7F061504 8FB90108 */  lw    $t9, 0x108($sp)
.Ljp7F061508:
/* 096078 7F061508 53200142 */  beql  $t9, $zero, .Ljp7F061A14
/* 09607C 7F06150C C6100260 */   lwc1  $f16, 0x260($s0)
/* 096080 7F061510 0C002918 */  jal   randomGetNext
/* 096084 7F061514 00000000 */   nop
/* 096088 7F061518 44829000 */  mtc1  $v0, $f18
/* 09608C 7F06151C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 096090 7F061520 04410004 */  bgez  $v0, .Ljp7F061534
/* 096094 7F061524 46809220 */   cvt.s.w $f8, $f18
/* 096098 7F061528 44812000 */  mtc1  $at, $f4
/* 09609C 7F06152C 00000000 */  nop
/* 0960A0 7F061530 46044200 */  add.s $f8, $f8, $f4
.Ljp7F061534:
/* 0960A4 7F061534 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0960A8 7F061538 44818000 */  mtc1  $at, $f16
/* 0960AC 7F06153C 3C013E80 */  li    $at, 0x3E800000 # 0.250000
/* 0960B0 7F061540 44815000 */  mtc1  $at, $f10
/* 0960B4 7F061544 46104182 */  mul.s $f6, $f8, $f16
/* 0960B8 7F061548 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 0960BC 7F06154C 44812000 */  mtc1  $at, $f4
/* 0960C0 7F061550 8FAD00F8 */  lw    $t5, 0xf8($sp)
/* 0960C4 7F061554 8FA400FC */  lw    $a0, 0xfc($sp)
/* 0960C8 7F061558 24050001 */  li    $a1, 1
/* 0960CC 7F06155C 460A3482 */  mul.s $f18, $f6, $f10
/* 0960D0 7F061560 46049200 */  add.s $f8, $f18, $f4
/* 0960D4 7F061564 E7A80080 */  swc1  $f8, 0x80($sp)
/* 0960D8 7F061568 C5B00000 */  lwc1  $f16, ($t5)
/* 0960DC 7F06156C 0FC17975 */  jal   bondwalkItemCheckBitflags
/* 0960E0 7F061570 E7B0007C */   swc1  $f16, 0x7c($sp)
/* 0960E4 7F061574 10400018 */  beqz  $v0, .Ljp7F0615D8
/* 0960E8 7F061578 8FA40108 */   lw    $a0, 0x108($sp)
/* 0960EC 7F06157C 0C002918 */  jal   randomGetNext
/* 0960F0 7F061580 00000000 */   nop
/* 0960F4 7F061584 44823000 */  mtc1  $v0, $f6
/* 0960F8 7F061588 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0960FC 7F06158C 04410004 */  bgez  $v0, .Ljp7F0615A0
/* 096100 7F061590 468032A0 */   cvt.s.w $f10, $f6
/* 096104 7F061594 44819000 */  mtc1  $at, $f18
/* 096108 7F061598 00000000 */  nop
/* 09610C 7F06159C 46125280 */  add.s $f10, $f10, $f18
.Ljp7F0615A0:
/* 096110 7F0615A0 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 096114 7F0615A4 44812000 */  mtc1  $at, $f4
/* 096118 7F0615A8 3C018005 */  lui   $at, %hi(D_80053E1C) # $at, 0x8005
/* 09611C 7F0615AC C4303E4C */  lwc1  $f16, %lo(D_80053E1C)($at)
/* 096120 7F0615B0 46045202 */  mul.s $f8, $f10, $f4
/* 096124 7F0615B4 27A50224 */  addiu $a1, $sp, 0x224
/* 096128 7F0615B8 46104302 */  mul.s $f12, $f8, $f16
/* 09612C 7F0615BC 0FC162EA */  jal   matrix_4x4_set_rotation_around_z
/* 096130 7F0615C0 00000000 */   nop
/* 096134 7F0615C4 8FA40108 */  lw    $a0, 0x108($sp)
/* 096138 7F0615C8 0FC163AE */  jal   matrix_4x4_set_position
/* 09613C 7F0615CC 27A50224 */   addiu $a1, $sp, 0x224
/* 096140 7F0615D0 10000004 */  b     .Ljp7F0615E4
/* 096144 7F0615D4 C7AC0080 */   lwc1  $f12, 0x80($sp)
.Ljp7F0615D8:
/* 096148 7F0615D8 0FC163A1 */  jal   matrix_4x4_set_identity_and_position
/* 09614C 7F0615DC 27A50224 */   addiu $a1, $sp, 0x224
/* 096150 7F0615E0 C7AC0080 */  lwc1  $f12, 0x80($sp)
.Ljp7F0615E4:
/* 096154 7F0615E4 0FC163E7 */  jal   matrix_scalar_multiply
/* 096158 7F0615E8 27A50224 */   addiu $a1, $sp, 0x224
/* 09615C 7F0615EC C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 096160 7F0615F0 0FC163CD */  jal   matrix_column_3_scalar_multiply
/* 096164 7F0615F4 27A50224 */   addiu $a1, $sp, 0x224
/* 096168 7F0615F8 27A40264 */  addiu $a0, $sp, 0x264
/* 09616C 7F0615FC 0FC16162 */  jal   matrix_4x4_multiply_in_place
/* 096170 7F061600 27A50224 */   addiu $a1, $sp, 0x224
/* 096174 7F061604 8FA502A4 */  lw    $a1, 0x2a4($sp)
/* 096178 7F061608 27A40224 */  addiu $a0, $sp, 0x224
/* 09617C 7F06160C 0FC16150 */  jal   matrix_4x4_copy
/* 096180 7F061610 24A50040 */   addiu $a1, $a1, 0x40
/* 096184 7F061614 C7A60254 */  lwc1  $f6, 0x254($sp)
/* 096188 7F061618 E60602E8 */  swc1  $f6, 0x2e8($s0)
/* 09618C 7F06161C C7B20258 */  lwc1  $f18, 0x258($sp)
/* 096190 7F061620 E61202EC */  swc1  $f18, 0x2ec($s0)
/* 096194 7F061624 C7AA025C */  lwc1  $f10, 0x25c($sp)
/* 096198 7F061628 0FC1E28D */  jal   currentPlayerGetMatrix10D4
/* 09619C 7F06162C E60A02F0 */   swc1  $f10, 0x2f0($s0)
/* 0961A0 7F061630 00402025 */  move  $a0, $v0
/* 0961A4 7F061634 0FC16265 */  jal   mtx4TransformVecInPlace
/* 0961A8 7F061638 260502E8 */   addiu $a1, $s0, 0x2e8
/* 0961AC 7F06163C C7A4025C */  lwc1  $f4, 0x25c($sp)
/* 0961B0 7F061640 820E000D */  lb    $t6, 0xd($s0)
/* 0961B4 7F061644 46002207 */  neg.s $f8, $f4
/* 0961B8 7F061648 11C000EE */  beqz  $t6, .Ljp7F061A04
/* 0961BC 7F06164C E60802F4 */   swc1  $f8, 0x2f4($s0)
/* 0961C0 7F061650 8FB8010C */  lw    $t8, 0x10c($sp)
/* 0961C4 7F061654 240F0001 */  li    $t7, 1
/* 0961C8 7F061658 53000003 */  beql  $t8, $zero, .Ljp7F061668
/* 0961CC 7F06165C 8FB901A0 */   lw    $t9, 0x1a0($sp)
/* 0961D0 7F061660 AF0F0000 */  sw    $t7, ($t8)
/* 0961D4 7F061664 8FB901A0 */  lw    $t9, 0x1a0($sp)
.Ljp7F061668:
/* 0961D8 7F061668 8F2D0008 */  lw    $t5, 8($t9)
/* 0961DC 7F06166C 8DA30008 */  lw    $v1, 8($t5)
/* 0961E0 7F061670 5060006D */  beql  $v1, $zero, .Ljp7F061828
/* 0961E4 7F061674 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 0961E8 7F061678 8C620004 */  lw    $v0, 4($v1)
/* 0961EC 7F06167C C7A60224 */  lwc1  $f6, 0x224($sp)
/* 0961F0 7F061680 C7A40234 */  lwc1  $f4, 0x234($sp)
/* 0961F4 7F061684 C4500000 */  lwc1  $f16, ($v0)
/* 0961F8 7F061688 C44A0004 */  lwc1  $f10, 4($v0)
/* 0961FC 7F06168C 46068482 */  mul.s $f18, $f16, $f6
/* 096200 7F061690 C4460008 */  lwc1  $f6, 8($v0)
/* 096204 7F061694 46045202 */  mul.s $f8, $f10, $f4
/* 096208 7F061698 C7AA0244 */  lwc1  $f10, 0x244($sp)
/* 09620C 7F06169C 460A3102 */  mul.s $f4, $f6, $f10
/* 096210 7F0616A0 46089400 */  add.s $f16, $f18, $f8
/* 096214 7F0616A4 C7A80254 */  lwc1  $f8, 0x254($sp)
/* 096218 7F0616A8 46048480 */  add.s $f18, $f16, $f4
/* 09621C 7F0616AC C7B00228 */  lwc1  $f16, 0x228($sp)
/* 096220 7F0616B0 46124180 */  add.s $f6, $f8, $f18
/* 096224 7F0616B4 C7B20238 */  lwc1  $f18, 0x238($sp)
/* 096228 7F0616B8 E7A60084 */  swc1  $f6, 0x84($sp)
/* 09622C 7F0616BC C44A0000 */  lwc1  $f10, ($v0)
/* 096230 7F0616C0 C4480004 */  lwc1  $f8, 4($v0)
/* 096234 7F0616C4 46105102 */  mul.s $f4, $f10, $f16
/* 096238 7F0616C8 C4500008 */  lwc1  $f16, 8($v0)
/* 09623C 7F0616CC 46124182 */  mul.s $f6, $f8, $f18
/* 096240 7F0616D0 C7A80248 */  lwc1  $f8, 0x248($sp)
/* 096244 7F0616D4 46088482 */  mul.s $f18, $f16, $f8
/* 096248 7F0616D8 46062280 */  add.s $f10, $f4, $f6
/* 09624C 7F0616DC C7A60258 */  lwc1  $f6, 0x258($sp)
/* 096250 7F0616E0 46125100 */  add.s $f4, $f10, $f18
/* 096254 7F0616E4 C7AA022C */  lwc1  $f10, 0x22c($sp)
/* 096258 7F0616E8 46043400 */  add.s $f16, $f6, $f4
/* 09625C 7F0616EC C7A4023C */  lwc1  $f4, 0x23c($sp)
/* 096260 7F0616F0 E7B00088 */  swc1  $f16, 0x88($sp)
/* 096264 7F0616F4 C4480000 */  lwc1  $f8, ($v0)
/* 096268 7F0616F8 C4460004 */  lwc1  $f6, 4($v0)
/* 09626C 7F0616FC 460A4482 */  mul.s $f18, $f8, $f10
/* 096270 7F061700 C44A0008 */  lwc1  $f10, 8($v0)
/* 096274 7F061704 46043402 */  mul.s $f16, $f6, $f4
/* 096278 7F061708 C7A6024C */  lwc1  $f6, 0x24c($sp)
/* 09627C 7F06170C 46065102 */  mul.s $f4, $f10, $f6
/* 096280 7F061710 46109200 */  add.s $f8, $f18, $f16
/* 096284 7F061714 C7B0025C */  lwc1  $f16, 0x25c($sp)
/* 096288 7F061718 46044480 */  add.s $f18, $f8, $f4
/* 09628C 7F06171C 46128280 */  add.s $f10, $f16, $f18
/* 096290 7F061720 0C002918 */  jal   randomGetNext
/* 096294 7F061724 E7AA008C */   swc1  $f10, 0x8c($sp)
/* 096298 7F061728 44823000 */  mtc1  $v0, $f6
/* 09629C 7F06172C 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 0962A0 7F061730 04410005 */  bgez  $v0, .Ljp7F061748
/* 0962A4 7F061734 46803220 */   cvt.s.w $f8, $f6
/* 0962A8 7F061738 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0962AC 7F06173C 44812000 */  mtc1  $at, $f4
/* 0962B0 7F061740 00000000 */  nop
/* 0962B4 7F061744 46044200 */  add.s $f8, $f8, $f4
.Ljp7F061748:
/* 0962B8 7F061748 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0962BC 7F06174C 44818000 */  mtc1  $at, $f16
/* 0962C0 7F061750 3C018005 */  lui   $at, %hi(D_80053E20) # $at, 0x8005
/* 0962C4 7F061754 C42A3E50 */  lwc1  $f10, %lo(D_80053E20)($at)
/* 0962C8 7F061758 46104482 */  mul.s $f18, $f8, $f16
/* 0962CC 7F06175C C7B00088 */  lwc1  $f16, 0x88($sp)
/* 0962D0 7F061760 C7A40084 */  lwc1  $f4, 0x84($sp)
/* 0962D4 7F061764 46002207 */  neg.s $f8, $f4
/* 0962D8 7F061768 460A9182 */  mul.s $f6, $f18, $f10
/* 0962DC 7F06176C C7AA008C */  lwc1  $f10, 0x8c($sp)
/* 0962E0 7F061770 46008487 */  neg.s $f18, $f16
/* 0962E4 7F061774 44064000 */  mfc1  $a2, $f8
/* 0962E8 7F061778 44079000 */  mfc1  $a3, $f18
/* 0962EC 7F06177C 44053000 */  mfc1  $a1, $f6
/* 0962F0 7F061780 46005187 */  neg.s $f6, $f10
/* 0962F4 7F061784 0FC16882 */  jal   matrix_4x4_align
/* 0962F8 7F061788 E7A60010 */   swc1  $f6, 0x10($sp)
/* 0962FC 7F06178C 3C018005 */  lui   $at, %hi(D_80053E24) # $at, 0x8005
/* 096300 7F061790 C4243E54 */  lwc1  $f4, %lo(D_80053E24)($at)
/* 096304 7F061794 C7A80080 */  lwc1  $f8, 0x80($sp)
/* 096308 7F061798 27A501E4 */  addiu $a1, $sp, 0x1e4
/* 09630C 7F06179C 46082302 */  mul.s $f12, $f4, $f8
/* 096310 7F0617A0 0FC163E7 */  jal   matrix_scalar_multiply
/* 096314 7F0617A4 00000000 */   nop
/* 096318 7F0617A8 C7B00194 */  lwc1  $f16, 0x194($sp)
/* 09631C 7F0617AC C61201C8 */  lwc1  $f18, 0x1c8($s0)
/* 096320 7F0617B0 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 096324 7F0617B4 C60401CC */  lwc1  $f4, 0x1cc($s0)
/* 096328 7F0617B8 46128281 */  sub.s $f10, $f16, $f18
/* 09632C 7F0617BC C61201D0 */  lwc1  $f18, 0x1d0($s0)
/* 096330 7F0617C0 C7B0019C */  lwc1  $f16, 0x19c($sp)
/* 096334 7F0617C4 46043201 */  sub.s $f8, $f6, $f4
/* 096338 7F0617C8 44065000 */  mfc1  $a2, $f10
/* 09633C 7F0617CC 27A40114 */  addiu $a0, $sp, 0x114
/* 096340 7F0617D0 46128281 */  sub.s $f10, $f16, $f18
/* 096344 7F0617D4 44074000 */  mfc1  $a3, $f8
/* 096348 7F0617D8 24050000 */  li    $a1, 0
/* 09634C 7F0617DC 0FC1681E */  jal   matrix_4x4_set_rotation_axis_angle
/* 096350 7F0617E0 E7AA0010 */   swc1  $f10, 0x10($sp)
/* 096354 7F0617E4 27A40114 */  addiu $a0, $sp, 0x114
/* 096358 7F0617E8 0FC16162 */  jal   matrix_4x4_multiply_in_place
/* 09635C 7F0617EC 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096360 7F0617F0 C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 096364 7F0617F4 0FC16428 */  jal   matrix_row_3_scalar_multiply
/* 096368 7F0617F8 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 09636C 7F0617FC 27A40154 */  addiu $a0, $sp, 0x154
/* 096370 7F061800 0FC16162 */  jal   matrix_4x4_multiply_in_place
/* 096374 7F061804 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096378 7F061808 27A40084 */  addiu $a0, $sp, 0x84
/* 09637C 7F06180C 0FC163AE */  jal   matrix_4x4_set_position
/* 096380 7F061810 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096384 7F061814 8FA502A4 */  lw    $a1, 0x2a4($sp)
/* 096388 7F061818 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 09638C 7F06181C 0FC16150 */  jal   matrix_4x4_copy
/* 096390 7F061820 24A50080 */   addiu $a1, $a1, 0x80
/* 096394 7F061824 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.Ljp7F061828:
/* 096398 7F061828 3C0E8004 */  lui   $t6, %hi(skeleton_gun_kf7) # $t6, 0x8004
/* 09639C 7F06182C 25CEC7DC */  addiu $t6, %lo(skeleton_gun_kf7) # addiu $t6, $t6, -0x3824
/* 0963A0 7F061830 8DF80004 */  lw    $t8, 4($t7)
/* 0963A4 7F061834 55D80074 */  bnel  $t6, $t8, .Ljp7F061A08
/* 0963A8 7F061838 8FB801A0 */   lw    $t8, 0x1a0($sp)
/* 0963AC 7F06183C 8DF90008 */  lw    $t9, 8($t7)
/* 0963B0 7F061840 8F230010 */  lw    $v1, 0x10($t9)
/* 0963B4 7F061844 50600070 */  beql  $v1, $zero, .Ljp7F061A08
/* 0963B8 7F061848 8FB801A0 */   lw    $t8, 0x1a0($sp)
/* 0963BC 7F06184C 8C620004 */  lw    $v0, 4($v1)
/* 0963C0 7F061850 C7A40224 */  lwc1  $f4, 0x224($sp)
/* 0963C4 7F061854 C7B20234 */  lwc1  $f18, 0x234($sp)
/* 0963C8 7F061858 C4460000 */  lwc1  $f6, ($v0)
/* 0963CC 7F06185C C4500004 */  lwc1  $f16, 4($v0)
/* 0963D0 7F061860 3C018005 */  lui   $at, %hi(D_80053E28) # $at, 0x8005
/* 0963D4 7F061864 46043202 */  mul.s $f8, $f6, $f4
/* 0963D8 7F061868 C4440008 */  lwc1  $f4, 8($v0)
/* 0963DC 7F06186C 8FAD02A4 */  lw    $t5, 0x2a4($sp)
/* 0963E0 7F061870 46128282 */  mul.s $f10, $f16, $f18
/* 0963E4 7F061874 C7B00244 */  lwc1  $f16, 0x244($sp)
/* 0963E8 7F061878 25AE00C0 */  addiu $t6, $t5, 0xc0
/* 0963EC 7F06187C 46102482 */  mul.s $f18, $f4, $f16
/* 0963F0 7F061880 460A4180 */  add.s $f6, $f8, $f10
/* 0963F4 7F061884 C7AA0254 */  lwc1  $f10, 0x254($sp)
/* 0963F8 7F061888 46123200 */  add.s $f8, $f6, $f18
/* 0963FC 7F06188C C7A60228 */  lwc1  $f6, 0x228($sp)
/* 096400 7F061890 46085100 */  add.s $f4, $f10, $f8
/* 096404 7F061894 C7A80238 */  lwc1  $f8, 0x238($sp)
/* 096408 7F061898 E7A40084 */  swc1  $f4, 0x84($sp)
/* 09640C 7F06189C C4500000 */  lwc1  $f16, ($v0)
/* 096410 7F0618A0 C44A0004 */  lwc1  $f10, 4($v0)
/* 096414 7F0618A4 46068482 */  mul.s $f18, $f16, $f6
/* 096418 7F0618A8 C4460008 */  lwc1  $f6, 8($v0)
/* 09641C 7F0618AC 46085102 */  mul.s $f4, $f10, $f8
/* 096420 7F0618B0 C7AA0248 */  lwc1  $f10, 0x248($sp)
/* 096424 7F0618B4 460A3202 */  mul.s $f8, $f6, $f10
/* 096428 7F0618B8 46049400 */  add.s $f16, $f18, $f4
/* 09642C 7F0618BC C7A40258 */  lwc1  $f4, 0x258($sp)
/* 096430 7F0618C0 46088480 */  add.s $f18, $f16, $f8
/* 096434 7F0618C4 C7B0022C */  lwc1  $f16, 0x22c($sp)
/* 096438 7F0618C8 46122180 */  add.s $f6, $f4, $f18
/* 09643C 7F0618CC C7B2023C */  lwc1  $f18, 0x23c($sp)
/* 096440 7F0618D0 E7A60088 */  swc1  $f6, 0x88($sp)
/* 096444 7F0618D4 C44A0000 */  lwc1  $f10, ($v0)
/* 096448 7F0618D8 C4440004 */  lwc1  $f4, 4($v0)
/* 09644C 7F0618DC 46105202 */  mul.s $f8, $f10, $f16
/* 096450 7F0618E0 C4500008 */  lwc1  $f16, 8($v0)
/* 096454 7F0618E4 AFAE0040 */  sw    $t6, 0x40($sp)
/* 096458 7F0618E8 46122182 */  mul.s $f6, $f4, $f18
/* 09645C 7F0618EC C7A4024C */  lwc1  $f4, 0x24c($sp)
/* 096460 7F0618F0 46048482 */  mul.s $f18, $f16, $f4
/* 096464 7F0618F4 C4243E58 */  lwc1  $f4, %lo(D_80053E28)($at)
/* 096468 7F0618F8 46064280 */  add.s $f10, $f8, $f6
/* 09646C 7F0618FC C7A6025C */  lwc1  $f6, 0x25c($sp)
/* 096470 7F061900 46125200 */  add.s $f8, $f10, $f18
/* 096474 7F061904 C7AA0080 */  lwc1  $f10, 0x80($sp)
/* 096478 7F061908 460A2482 */  mul.s $f18, $f4, $f10
/* 09647C 7F06190C 46083400 */  add.s $f16, $f6, $f8
/* 096480 7F061910 E7B0008C */  swc1  $f16, 0x8c($sp)
/* 096484 7F061914 0C002918 */  jal   randomGetNext
/* 096488 7F061918 E7B20038 */   swc1  $f18, 0x38($sp)
/* 09648C 7F06191C 44823000 */  mtc1  $v0, $f6
/* 096490 7F061920 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 096494 7F061924 04410005 */  bgez  $v0, .Ljp7F06193C
/* 096498 7F061928 46803220 */   cvt.s.w $f8, $f6
/* 09649C 7F06192C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0964A0 7F061930 44818000 */  mtc1  $at, $f16
/* 0964A4 7F061934 00000000 */  nop
/* 0964A8 7F061938 46104200 */  add.s $f8, $f8, $f16
.Ljp7F06193C:
/* 0964AC 7F06193C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0964B0 7F061940 44812000 */  mtc1  $at, $f4
/* 0964B4 7F061944 3C018005 */  lui   $at, %hi(D_80053E2C) # $at, 0x8005
/* 0964B8 7F061948 C4323E5C */  lwc1  $f18, %lo(D_80053E2C)($at)
/* 0964BC 7F06194C 46044282 */  mul.s $f10, $f8, $f4
/* 0964C0 7F061950 C7A40088 */  lwc1  $f4, 0x88($sp)
/* 0964C4 7F061954 C7B00084 */  lwc1  $f16, 0x84($sp)
/* 0964C8 7F061958 46008207 */  neg.s $f8, $f16
/* 0964CC 7F06195C 46125182 */  mul.s $f6, $f10, $f18
/* 0964D0 7F061960 C7B2008C */  lwc1  $f18, 0x8c($sp)
/* 0964D4 7F061964 46002287 */  neg.s $f10, $f4
/* 0964D8 7F061968 44064000 */  mfc1  $a2, $f8
/* 0964DC 7F06196C 44075000 */  mfc1  $a3, $f10
/* 0964E0 7F061970 44053000 */  mfc1  $a1, $f6
/* 0964E4 7F061974 46009187 */  neg.s $f6, $f18
/* 0964E8 7F061978 0FC16882 */  jal   matrix_4x4_align
/* 0964EC 7F06197C E7A60010 */   swc1  $f6, 0x10($sp)
/* 0964F0 7F061980 C7AC0038 */  lwc1  $f12, 0x38($sp)
/* 0964F4 7F061984 0FC163E7 */  jal   matrix_scalar_multiply
/* 0964F8 7F061988 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 0964FC 7F06198C C7B00194 */  lwc1  $f16, 0x194($sp)
/* 096500 7F061990 C60801C8 */  lwc1  $f8, 0x1c8($s0)
/* 096504 7F061994 C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 096508 7F061998 C61201CC */  lwc1  $f18, 0x1cc($s0)
/* 09650C 7F06199C 46088101 */  sub.s $f4, $f16, $f8
/* 096510 7F0619A0 C60801D0 */  lwc1  $f8, 0x1d0($s0)
/* 096514 7F0619A4 C7B0019C */  lwc1  $f16, 0x19c($sp)
/* 096518 7F0619A8 46125181 */  sub.s $f6, $f10, $f18
/* 09651C 7F0619AC 44062000 */  mfc1  $a2, $f4
/* 096520 7F0619B0 27A40114 */  addiu $a0, $sp, 0x114
/* 096524 7F0619B4 46088101 */  sub.s $f4, $f16, $f8
/* 096528 7F0619B8 44073000 */  mfc1  $a3, $f6
/* 09652C 7F0619BC 24050000 */  li    $a1, 0
/* 096530 7F0619C0 0FC1681E */  jal   matrix_4x4_set_rotation_axis_angle
/* 096534 7F0619C4 E7A40010 */   swc1  $f4, 0x10($sp)
/* 096538 7F0619C8 27A40114 */  addiu $a0, $sp, 0x114
/* 09653C 7F0619CC 0FC16162 */  jal   matrix_4x4_multiply_in_place
/* 096540 7F0619D0 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096544 7F0619D4 C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 096548 7F0619D8 0FC16428 */  jal   matrix_row_3_scalar_multiply
/* 09654C 7F0619DC 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096550 7F0619E0 27A40154 */  addiu $a0, $sp, 0x154
/* 096554 7F0619E4 0FC16162 */  jal   matrix_4x4_multiply_in_place
/* 096558 7F0619E8 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 09655C 7F0619EC 27A40084 */  addiu $a0, $sp, 0x84
/* 096560 7F0619F0 0FC163AE */  jal   matrix_4x4_set_position
/* 096564 7F0619F4 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 096568 7F0619F8 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 09656C 7F0619FC 0FC16150 */  jal   matrix_4x4_copy
/* 096570 7F061A00 8FA50040 */   lw    $a1, 0x40($sp)
.Ljp7F061A04:
/* 096574 7F061A04 8FB801A0 */  lw    $t8, 0x1a0($sp)
.Ljp7F061A08:
/* 096578 7F061A08 1000000C */  b     .Ljp7F061A3C
/* 09657C 7F061A0C 8F020008 */   lw    $v0, 8($t8)
/* 096580 7F061A10 C6100260 */  lwc1  $f16, 0x260($s0)
.Ljp7F061A14:
/* 096584 7F061A14 C60A0298 */  lwc1  $f10, 0x298($s0)
/* 096588 7F061A18 C612029C */  lwc1  $f18, 0x29c($s0)
/* 09658C 7F061A1C C60602A0 */  lwc1  $f6, 0x2a0($s0)
/* 096590 7F061A20 46008207 */  neg.s $f8, $f16
/* 096594 7F061A24 E60A02E8 */  swc1  $f10, 0x2e8($s0)
/* 096598 7F061A28 E60802F4 */  swc1  $f8, 0x2f4($s0)
/* 09659C 7F061A2C E61202EC */  swc1  $f18, 0x2ec($s0)
/* 0965A0 7F061A30 E60602F0 */  swc1  $f6, 0x2f0($s0)
/* 0965A4 7F061A34 8FAF01A0 */  lw    $t7, 0x1a0($sp)
/* 0965A8 7F061A38 8DE20008 */  lw    $v0, 8($t7)
.Ljp7F061A3C:
/* 0965AC 7F061A3C 8C440018 */  lw    $a0, 0x18($v0)
/* 0965B0 7F061A40 50800043 */  beql  $a0, $zero, .Ljp7F061B50
/* 0965B4 7F061A44 8FAD01A0 */   lw    $t5, 0x1a0($sp)
/* 0965B8 7F061A48 8C990004 */  lw    $t9, 4($a0)
/* 0965BC 7F061A4C 00002825 */  move  $a1, $zero
/* 0965C0 7F061A50 0FC1B2D8 */  jal   modelFindNodeMtxIndex
/* 0965C4 7F061A54 AFB90070 */   sw    $t9, 0x70($sp)
/* 0965C8 7F061A58 AFA2006C */  sw    $v0, 0x6c($sp)
/* 0965CC 7F061A5C 8E050010 */  lw    $a1, 0x10($s0)
/* 0965D0 7F061A60 0FC17AF5 */  jal   sub_GAME_7F05E6B4
/* 0965D4 7F061A64 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0965D8 7F061A68 8FAD01A0 */  lw    $t5, 0x1a0($sp)
/* 0965DC 7F061A6C 8FA40070 */  lw    $a0, 0x70($sp)
/* 0965E0 7F061A70 27A601A4 */  addiu $a2, $sp, 0x1a4
/* 0965E4 7F061A74 85AE000C */  lh    $t6, 0xc($t5)
/* 0965E8 7F061A78 29C1001D */  slti  $at, $t6, 0x1d
/* 0965EC 7F061A7C 1420002A */  bnez  $at, .Ljp7F061B28
/* 0965F0 7F061A80 00000000 */   nop
/* 0965F4 7F061A84 8DB80008 */  lw    $t8, 8($t5)
/* 0965F8 7F061A88 8F030070 */  lw    $v1, 0x70($t8)
/* 0965FC 7F061A8C 10600026 */  beqz  $v1, .Ljp7F061B28
/* 096600 7F061A90 00000000 */   nop
/* 096604 7F061A94 8C620004 */  lw    $v0, 4($v1)
/* 096608 7F061A98 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 09660C 7F061A9C 0FC17AE1 */  jal   get_value_if_watch_is_on_hand_or_not
/* 096610 7F061AA0 AFA20068 */   sw    $v0, 0x68($sp)
/* 096614 7F061AA4 3C018005 */  lui   $at, %hi(D_80053E30) # $at, 0x8005
/* 096618 7F061AA8 C42A3E60 */  lwc1  $f10, %lo(D_80053E30)($at)
/* 09661C 7F061AAC C6040214 */  lwc1  $f4, 0x214($s0)
/* 096620 7F061AB0 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 096624 7F061AB4 44818000 */  mtc1  $at, $f16
/* 096628 7F061AB8 460A2480 */  add.s $f18, $f4, $f10
/* 09662C 7F061ABC 3C018005 */  lui   $at, %hi(D_80053E34) # $at, 0x8005
/* 096630 7F061AC0 C4243E64 */  lwc1  $f4, %lo(D_80053E34)($at)
/* 096634 7F061AC4 8FA20068 */  lw    $v0, 0x68($sp)
/* 096638 7F061AC8 46009181 */  sub.s $f6, $f18, $f0
/* 09663C 7F061ACC 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 096640 7F061AD0 C4520000 */  lwc1  $f18, ($v0)
/* 096644 7F061AD4 46103202 */  mul.s $f8, $f6, $f16
/* 096648 7F061AD8 C446000C */  lwc1  $f6, 0xc($v0)
/* 09664C 7F061ADC 46069401 */  sub.s $f16, $f18, $f6
/* 096650 7F061AE0 C4460014 */  lwc1  $f6, 0x14($v0)
/* 096654 7F061AE4 C4520008 */  lwc1  $f18, 8($v0)
/* 096658 7F061AE8 46044283 */  div.s $f10, $f8, $f4
/* 09665C 7F061AEC C4440010 */  lwc1  $f4, 0x10($v0)
/* 096660 7F061AF0 C4480004 */  lwc1  $f8, 4($v0)
/* 096664 7F061AF4 44068000 */  mfc1  $a2, $f16
/* 096668 7F061AF8 46069401 */  sub.s $f16, $f18, $f6
/* 09666C 7F061AFC E7B00010 */  swc1  $f16, 0x10($sp)
/* 096670 7F061B00 44055000 */  mfc1  $a1, $f10
/* 096674 7F061B04 46044281 */  sub.s $f10, $f8, $f4
/* 096678 7F061B08 44075000 */  mfc1  $a3, $f10
/* 09667C 7F061B0C 0C005DD8 */  jal   guRotateF
/* 096680 7F061B10 00000000 */   nop
/* 096684 7F061B14 8FA40070 */  lw    $a0, 0x70($sp)
/* 096688 7F061B18 0FC163AE */  jal   matrix_4x4_set_position
/* 09668C 7F061B1C 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 096690 7F061B20 10000004 */  b     .Ljp7F061B34
/* 096694 7F061B24 8FAF006C */   lw    $t7, 0x6c($sp)
.Ljp7F061B28:
/* 096698 7F061B28 0FC1627C */  jal   matrix_4x4_set_position_and_rotation_around_y
/* 09669C 7F061B2C 8E050214 */   lw    $a1, 0x214($s0)
/* 0966A0 7F061B30 8FAF006C */  lw    $t7, 0x6c($sp)
.Ljp7F061B34:
/* 0966A4 7F061B34 8FAE02A4 */  lw    $t6, 0x2a4($sp)
/* 0966A8 7F061B38 27A40264 */  addiu $a0, $sp, 0x264
/* 0966AC 7F061B3C 000FC980 */  sll   $t9, $t7, 6
/* 0966B0 7F061B40 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 0966B4 7F061B44 0FC161AB */  jal   matrix_4x4_multiply_homogeneous
/* 0966B8 7F061B48 032E3021 */   addu  $a2, $t9, $t6
/* 0966BC 7F061B4C 8FAD01A0 */  lw    $t5, 0x1a0($sp)
.Ljp7F061B50:
/* 0966C0 7F061B50 8FA40044 */  lw    $a0, 0x44($sp)
/* 0966C4 7F061B54 85B8000C */  lh    $t8, 0xc($t5)
/* 0966C8 7F061B58 01A02825 */  move  $a1, $t5
/* 0966CC 7F061B5C 2B01001E */  slti  $at, $t8, 0x1e
/* 0966D0 7F061B60 54200004 */  bnezl $at, .Ljp7F061B74
/* 0966D4 7F061B64 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 0966D8 7F061B68 0FC220B8 */  jal   seems_to_load_cuff_microcode
/* 0966DC 7F061B6C 2406001D */   li    $a2, 29
/* 0966E0 7F061B70 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.Ljp7F061B74:
/* 0966E4 7F061B74 8DF90008 */  lw    $t9, 8($t7)
/* 0966E8 7F061B78 8F24001C */  lw    $a0, 0x1c($t9)
/* 0966EC 7F061B7C 50800017 */  beql  $a0, $zero, .Ljp7F061BDC
/* 0966F0 7F061B80 8FB901A0 */   lw    $t9, 0x1a0($sp)
/* 0966F4 7F061B84 8C8E0004 */  lw    $t6, 4($a0)
/* 0966F8 7F061B88 00002825 */  move  $a1, $zero
/* 0966FC 7F061B8C 0FC1B2D8 */  jal   modelFindNodeMtxIndex
/* 096700 7F061B90 AFAE0064 */   sw    $t6, 0x64($sp)
/* 096704 7F061B94 AFA20060 */  sw    $v0, 0x60($sp)
/* 096708 7F061B98 0FC17B57 */  jal   sub_GAME_7F05E83C
/* 09670C 7F061B9C 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096710 7F061BA0 8FA40064 */  lw    $a0, 0x64($sp)
/* 096714 7F061BA4 0FC163A1 */  jal   matrix_4x4_set_identity_and_position
/* 096718 7F061BA8 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 09671C 7F061BAC C7A801DC */  lwc1  $f8, 0x1dc($sp)
/* 096720 7F061BB0 C6040218 */  lwc1  $f4, 0x218($s0)
/* 096724 7F061BB4 8FB80060 */  lw    $t8, 0x60($sp)
/* 096728 7F061BB8 8FAF02A4 */  lw    $t7, 0x2a4($sp)
/* 09672C 7F061BBC 46044281 */  sub.s $f10, $f8, $f4
/* 096730 7F061BC0 00186980 */  sll   $t5, $t8, 6
/* 096734 7F061BC4 27A40264 */  addiu $a0, $sp, 0x264
/* 096738 7F061BC8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 09673C 7F061BCC E7AA01DC */  swc1  $f10, 0x1dc($sp)
/* 096740 7F061BD0 0FC1617A */  jal   matrix_4x4_multiply
/* 096744 7F061BD4 01AF3021 */   addu  $a2, $t5, $t7
/* 096748 7F061BD8 8FB901A0 */  lw    $t9, 0x1a0($sp)
.Ljp7F061BDC:
/* 09674C 7F061BDC 00001825 */  move  $v1, $zero
/* 096750 7F061BE0 00003025 */  move  $a2, $zero
/* 096754 7F061BE4 872E000C */  lh    $t6, 0xc($t9)
/* 096758 7F061BE8 24070005 */  li    $a3, 5
/* 09675C 7F061BEC 29C10013 */  slti  $at, $t6, 0x13
/* 096760 7F061BF0 1420002A */  bnez  $at, .Ljp7F061C9C
/* 096764 7F061BF4 00000000 */   nop
.Ljp7F061BF8:
/* 096768 7F061BF8 8FB801A0 */  lw    $t8, 0x1a0($sp)
/* 09676C 7F061BFC 8FA40044 */  lw    $a0, 0x44($sp)
/* 096770 7F061C00 8F0D0008 */  lw    $t5, 8($t8)
/* 096774 7F061C04 01A67821 */  addu  $t7, $t5, $a2
/* 096778 7F061C08 8DE50048 */  lw    $a1, 0x48($t7)
/* 09677C 7F061C0C 50A0000E */  beql  $a1, $zero, .Ljp7F061C48
/* 096780 7F061C10 8FAD01A0 */   lw    $t5, 0x1a0($sp)
/* 096784 7F061C14 AFA3005C */  sw    $v1, 0x5c($sp)
/* 096788 7F061C18 0FC1B363 */  jal   modelGetNodeRwData
/* 09678C 7F061C1C AFA60040 */   sw    $a2, 0x40($sp)
/* 096790 7F061C20 8FA3005C */  lw    $v1, 0x5c($sp)
/* 096794 7F061C24 8FA60040 */  lw    $a2, 0x40($sp)
/* 096798 7F061C28 10400006 */  beqz  $v0, .Ljp7F061C44
/* 09679C 7F061C2C 24070005 */   li    $a3, 5
/* 0967A0 7F061C30 8E190034 */  lw    $t9, 0x34($s0)
/* 0967A4 7F061C34 00E37023 */  subu  $t6, $a3, $v1
/* 0967A8 7F061C38 032EC02A */  slt   $t8, $t9, $t6
/* 0967AC 7F061C3C 3B180001 */  xori  $t8, $t8, 1
/* 0967B0 7F061C40 AC580000 */  sw    $t8, ($v0)
.Ljp7F061C44:
/* 0967B4 7F061C44 8FAD01A0 */  lw    $t5, 0x1a0($sp)
.Ljp7F061C48:
/* 0967B8 7F061C48 8FA40044 */  lw    $a0, 0x44($sp)
/* 0967BC 7F061C4C 8DAF0008 */  lw    $t7, 8($t5)
/* 0967C0 7F061C50 01E6C821 */  addu  $t9, $t7, $a2
/* 0967C4 7F061C54 8F25005C */  lw    $a1, 0x5c($t9)
/* 0967C8 7F061C58 50A0000E */  beql  $a1, $zero, .Ljp7F061C94
/* 0967CC 7F061C5C 24630001 */   addiu $v1, $v1, 1
/* 0967D0 7F061C60 AFA3005C */  sw    $v1, 0x5c($sp)
/* 0967D4 7F061C64 0FC1B363 */  jal   modelGetNodeRwData
/* 0967D8 7F061C68 AFA60040 */   sw    $a2, 0x40($sp)
/* 0967DC 7F061C6C 8FA3005C */  lw    $v1, 0x5c($sp)
/* 0967E0 7F061C70 8FA60040 */  lw    $a2, 0x40($sp)
/* 0967E4 7F061C74 10400006 */  beqz  $v0, .Ljp7F061C90
/* 0967E8 7F061C78 24070005 */   li    $a3, 5
/* 0967EC 7F061C7C 8E0E0034 */  lw    $t6, 0x34($s0)
/* 0967F0 7F061C80 00E3C023 */  subu  $t8, $a3, $v1
/* 0967F4 7F061C84 01D8682A */  slt   $t5, $t6, $t8
/* 0967F8 7F061C88 39AD0001 */  xori  $t5, $t5, 1
/* 0967FC 7F061C8C AC4D0000 */  sw    $t5, ($v0)
.Ljp7F061C90:
/* 096800 7F061C90 24630001 */  addiu $v1, $v1, 1
.Ljp7F061C94:
/* 096804 7F061C94 1467FFD8 */  bne   $v1, $a3, .Ljp7F061BF8
/* 096808 7F061C98 24C60004 */   addiu $a2, $a2, 4
.Ljp7F061C9C:
/* 09680C 7F061C9C 0FC1BD6D */  jal   modelUpdateNodeRelations
/* 096810 7F061CA0 8FA40044 */   lw    $a0, 0x44($sp)
/* 096814 7F061CA4 820F000C */  lb    $t7, 0xc($s0)
/* 096818 7F061CA8 8FB900FC */  lw    $t9, 0xfc($sp)
/* 09681C 7F061CAC 11E00014 */  beqz  $t7, weapon_bullet_type_shotgun_mine
/* 096820 7F061CB0 272EFFFC */   addiu $t6, $t9, -4
/* 096824 7F061CB4 2DC10014 */  sltiu $at, $t6, 0x14
/* 096828 7F061CB8 10200011 */  beqz  $at, weapon_bullet_type_shotgun_mine
/* 09682C 7F061CBC 000E7080 */   sll   $t6, $t6, 2
/* 096830 7F061CC0 3C018005 */  lui   $at, %hi(jpt_weapon_bullet_type)
/* 096834 7F061CC4 002E0821 */  addu  $at, $at, $t6
/* 096838 7F061CC8 8C2E3E68 */  lw    $t6, %lo(jpt_weapon_bullet_type)($at)
/* 09683C 7F061CCC 01C00008 */  jr    $t6
/* 096840 7F061CD0 00000000 */   nop
weapon_bullet_type_pistol:
/* 096844 7F061CD4 0FC18848 */  jal   sub_GAME_7F061BF4
/* 096848 7F061CD8 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 09684C 7F061CDC 8E180030 */  lw    $t8, 0x30($s0)
/* 096850 7F061CE0 270D0001 */  addiu $t5, $t8, 1
/* 096854 7F061CE4 10000006 */  b     weapon_bullet_type_shotgun_mine
/* 096858 7F061CE8 AE0D0030 */   sw    $t5, 0x30($s0)
weapon_bullet_type_none:
/* 09685C 7F061CEC 8E0F0030 */  lw    $t7, 0x30($s0)
/* 096860 7F061CF0 25F90001 */  addiu $t9, $t7, 1
/* 096864 7F061CF4 AE190030 */  sw    $t9, 0x30($s0)
/* 096868 7F061CF8 0FC18848 */  jal   sub_GAME_7F061BF4
/* 09686C 7F061CFC 8FA402A8 */   lw    $a0, 0x2a8($sp)
weapon_bullet_type_shotgun_mine:
/* 096870 7F061D00 8FAE00FC */  lw    $t6, 0xfc($sp)
.Ljp7F061D04:
/* 096874 7F061D04 24010019 */  li    $at, 25
/* 096878 7F061D08 55C10004 */  bnel  $t6, $at, .Ljp7F061D1C
/* 09687C 7F061D0C 8218000C */   lb    $t8, 0xc($s0)
/* 096880 7F061D10 0FC17F92 */  jal   sub_GAME_7F05F928
/* 096884 7F061D14 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096888 7F061D18 8218000C */  lb    $t8, 0xc($s0)
.Ljp7F061D1C:
/* 09688C 7F061D1C 3C048008 */  lui   $a0, %hi(g_CurrentPlayer) # $a0, 0x8008
/* 096890 7F061D20 53000046 */  beql  $t8, $zero, .Ljp7F061E3C
/* 096894 7F061D24 8FBF0034 */   lw    $ra, 0x34($sp)
/* 096898 7F061D28 0FC22793 */  jal   bondviewGetPlayerStanHeight
/* 09689C 7F061D2C 8C84A120 */   lw    $a0, %lo(g_CurrentPlayer)($a0)
/* 0968A0 7F061D30 44050000 */  mfc1  $a1, $f0
/* 0968A4 7F061D34 0FC1A2B8 */  jal   sub_GAME_7F068508
/* 0968A8 7F061D38 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0968AC 7F061D3C 8FAD00FC */  lw    $t5, 0xfc($sp)
/* 0968B0 7F061D40 24010018 */  li    $at, 24
/* 0968B4 7F061D44 8FAF00FC */  lw    $t7, 0xfc($sp)
/* 0968B8 7F061D48 55A10006 */  bnel  $t5, $at, .Ljp7F061D64
/* 0968BC 7F061D4C 2401001A */   li    $at, 26
/* 0968C0 7F061D50 0FC17F17 */  jal   sub_GAME_7F05F73C
/* 0968C4 7F061D54 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0968C8 7F061D58 10000038 */  b     .Ljp7F061E3C
/* 0968CC 7F061D5C 8FBF0034 */   lw    $ra, 0x34($sp)
/* 0968D0 7F061D60 2401001A */  li    $at, 26
.Ljp7F061D64:
/* 0968D4 7F061D64 15E10005 */  bne   $t7, $at, .Ljp7F061D7C
/* 0968D8 7F061D68 8FB900FC */   lw    $t9, 0xfc($sp)
/* 0968DC 7F061D6C 0FC17CD1 */  jal   generate_player_thrown_grenade
/* 0968E0 7F061D70 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0968E4 7F061D74 10000031 */  b     .Ljp7F061E3C
/* 0968E8 7F061D78 8FBF0034 */   lw    $ra, 0x34($sp)
.Ljp7F061D7C:
/* 0968EC 7F061D7C 24010019 */  li    $at, 25
/* 0968F0 7F061D80 17210005 */  bne   $t9, $at, .Ljp7F061D98
/* 0968F4 7F061D84 8FAE00FC */   lw    $t6, 0xfc($sp)
/* 0968F8 7F061D88 0FC18021 */  jal   gunFireTankShell
/* 0968FC 7F061D8C 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096900 7F061D90 1000002A */  b     .Ljp7F061E3C
/* 096904 7F061D94 8FBF0034 */   lw    $ra, 0x34($sp)
.Ljp7F061D98:
/* 096908 7F061D98 24010003 */  li    $at, 3
/* 09690C 7F061D9C 15C10005 */  bne   $t6, $at, .Ljp7F061DB4
/* 096910 7F061DA0 8FB800FC */   lw    $t8, 0xfc($sp)
/* 096914 7F061DA4 0FC17D6F */  jal   generate_player_thrown_knife
/* 096918 7F061DA8 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 09691C 7F061DAC 10000023 */  b     .Ljp7F061E3C
/* 096920 7F061DB0 8FBF0034 */   lw    $ra, 0x34($sp)
.Ljp7F061DB4:
/* 096924 7F061DB4 2401001D */  li    $at, 29
/* 096928 7F061DB8 1301000F */  beq   $t8, $at, .Ljp7F061DF8
/* 09692C 7F061DBC 2401001C */   li    $at, 28
/* 096930 7F061DC0 1301000D */  beq   $t8, $at, .Ljp7F061DF8
/* 096934 7F061DC4 2401001B */   li    $at, 27
/* 096938 7F061DC8 1301000B */  beq   $t8, $at, .Ljp7F061DF8
/* 09693C 7F061DCC 24010021 */   li    $at, 33
/* 096940 7F061DD0 13010009 */  beq   $t8, $at, .Ljp7F061DF8
/* 096944 7F061DD4 2401002F */   li    $at, 47
/* 096948 7F061DD8 13010007 */  beq   $t8, $at, .Ljp7F061DF8
/* 09694C 7F061DDC 24010030 */   li    $at, 48
/* 096950 7F061DE0 13010005 */  beq   $t8, $at, .Ljp7F061DF8
/* 096954 7F061DE4 2401003D */   li    $at, 61
/* 096958 7F061DE8 13010003 */  beq   $t8, $at, .Ljp7F061DF8
/* 09695C 7F061DEC 24010022 */   li    $at, 34
/* 096960 7F061DF0 17010005 */  bne   $t8, $at, .Ljp7F061E08
/* 096964 7F061DF4 8FAD00FC */   lw    $t5, 0xfc($sp)
.Ljp7F061DF8:
/* 096968 7F061DF8 0FC17E1E */  jal   generate_player_thrown_object
/* 09696C 7F061DFC 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 096970 7F061E00 1000000E */  b     .Ljp7F061E3C
/* 096974 7F061E04 8FBF0034 */   lw    $ra, 0x34($sp)
.Ljp7F061E08:
/* 096978 7F061E08 24010023 */  li    $at, 35
/* 09697C 7F061E0C 15A10005 */  bne   $t5, $at, .Ljp7F061E24
/* 096980 7F061E10 8FAF00FC */   lw    $t7, 0xfc($sp)
/* 096984 7F061E14 0FC17F17 */  jal   sub_GAME_7F05F73C
/* 096988 7F061E18 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 09698C 7F061E1C 10000007 */  b     .Ljp7F061E3C
/* 096990 7F061E20 8FBF0034 */   lw    $ra, 0x34($sp)
.Ljp7F061E24:
/* 096994 7F061E24 24010024 */  li    $at, 36
/* 096998 7F061E28 55E10004 */  bnel  $t7, $at, .Ljp7F061E3C
/* 09699C 7F061E2C 8FBF0034 */   lw    $ra, 0x34($sp)
/* 0969A0 7F061E30 0FC17F17 */  jal   sub_GAME_7F05F73C
/* 0969A4 7F061E34 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0969A8 7F061E38 8FBF0034 */  lw    $ra, 0x34($sp)
.Ljp7F061E3C:
/* 0969AC 7F061E3C 8FB00030 */  lw    $s0, 0x30($sp)
/* 0969B0 7F061E40 27BD02A8 */  addiu $sp, $sp, 0x2a8
/* 0969B4 7F061E44 03E00008 */  jr    $ra
/* 0969B8 7F061E48 00000000 */   nop
)
#endif

#ifdef VERSION_EU
GLOBAL_ASM(
.late_rodata
glabel D_80053DE0
.word 0x3F70B780  /* 0.940299987793 */
glabel D_80053DE4
.word 0x3D748800 /* 0.059700012207 */
glabel D_80053DE8
.word 0x3f19999a /*0.60000002*/
glabel D_80053DEC
.word 0x3e99999a /*0.30000001*/
glabel D_80053DF0
.word 0x3f19999a /*0.60000002*/
glabel D_80053DF4
.word 0x3e99999a /*0.30000001*/
glabel D_80053DF8
.word 0x3f19999a /*0.60000002*/
glabel D_80053DFC
.word 0x3e99999a /*0.30000001*/
glabel D_80053E00
.word 0x41de6666 /*27.799999*/
glabel D_80053E04
.word 0x3dccccce /*0.10000001*/
glabel D_80053E08
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E0C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E10
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E14
.word 0x3f060a92 /*0.52359879*/
glabel D_80053E18
.word 0x3f060a92 /*0.52359879*/
glabel D_80053E1C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E20
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E24
.word 0x3dccccce /*0.10000001*/
glabel D_80053E28
.word 0x3dccccce /*0.10000001*/
glabel D_80053E2C
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E30
.word 0x40c90fdb /*6.2831855*/
glabel D_80053E34
.word 0x40c90fdb /*6.2831855*/

/*D:80053E38*/
glabel jpt_weapon_bullet_type
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_shotgun_mine
.word weapon_bullet_type_shotgun_mine
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_pistol
.word weapon_bullet_type_none
.word weapon_bullet_type_none
.size jpt_weapon_bullet_type, . - jpt_weapon_bullet_type

.text
glabel handles_firing_or_throwing_weapon_in_hand
/* 092E70 7F060480 27BDFD58 */  addiu $sp, $sp, -0x2a8
/* 092E74 7F060484 3C0F8003 */  lui   $t7, %hi(D_80035C40) # $t7, 0x8003
/* 092E78 7F060488 AFBF0034 */  sw    $ra, 0x34($sp)
/* 092E7C 7F06048C AFB00030 */  sw    $s0, 0x30($sp)
/* 092E80 7F060490 25EF1190 */  addiu $t7, %lo(D_80035C40) # addiu $t7, $t7, 0x1190
/* 092E84 7F060494 8DE10000 */  lw    $at, ($t7)
/* 092E88 7F060498 27AE0194 */  addiu $t6, $sp, 0x194
/* 092E8C 7F06049C 8DED0004 */  lw    $t5, 4($t7)
/* 092E90 7F0604A0 ADC10000 */  sw    $at, ($t6)
/* 092E94 7F0604A4 8DE10008 */  lw    $at, 8($t7)
/* 092E98 7F0604A8 0004C0C0 */  sll   $t8, $a0, 3
/* 092E9C 7F0604AC 0304C023 */  subu  $t8, $t8, $a0
/* 092EA0 7F0604B0 0018C080 */  sll   $t8, $t8, 2
/* 092EA4 7F0604B4 0304C021 */  addu  $t8, $t8, $a0
/* 092EA8 7F0604B8 3C198007 */  lui   $t9, %hi(g_CurrentPlayer) # $t9, 0x8007
/* 092EAC 7F0604BC ADCD0004 */  sw    $t5, 4($t6)
/* 092EB0 7F0604C0 ADC10008 */  sw    $at, 8($t6)
/* 092EB4 7F0604C4 8F398BC0 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 092EB8 7F0604C8 0018C080 */  sll   $t8, $t8, 2
/* 092EBC 7F0604CC 0304C021 */  addu  $t8, $t8, $a0
/* 092EC0 7F0604D0 0018C0C0 */  sll   $t8, $t8, 3
/* 092EC4 7F0604D4 03388021 */  addu  $s0, $t9, $t8
/* 092EC8 7F0604D8 AFA0010C */  sw    $zero, 0x10c($sp)
/* 092ECC 7F0604DC AFA00108 */  sw    $zero, 0x108($sp)
/* 092ED0 7F0604E0 26100868 */  addiu $s0, $s0, 0x868
/* 092ED4 7F0604E4 0FC177BF */  jal   get_item_in_hand_or_watch_menu
/* 092ED8 7F0604E8 AFA402A8 */   sw    $a0, 0x2a8($sp)
/* 092EDC 7F0604EC AFA200FC */  sw    $v0, 0xfc($sp)
/* 092EE0 7F0604F0 0FC17359 */  jal   get_ptr_item_statistics
/* 092EE4 7F0604F4 00402025 */   move  $a0, $v0
/* 092EE8 7F0604F8 8FAE02A8 */  lw    $t6, 0x2a8($sp)
/* 092EEC 7F0604FC AFA200F8 */  sw    $v0, 0xf8($sp)
/* 092EF0 7F060500 15C0002D */  bnez  $t6, .L7F0605B8
/* 092EF4 7F060504 00000000 */   nop
/* 092EF8 7F060508 0FC177BF */  jal   get_item_in_hand_or_watch_menu
/* 092EFC 7F06050C 24040001 */   li    $a0, 1
/* 092F00 7F060510 00402025 */  move  $a0, $v0
/* 092F04 7F060514 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 092F08 7F060518 24050800 */   li    $a1, 2048
/* 092F0C 7F06051C 10400015 */  beqz  $v0, .L7F060574
/* 092F10 7F060520 3C018004 */   lui   $at, %hi(g_GlobalTimerDelta)
/* 092F14 7F060524 3C018004 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8004
/* 092F18 7F060528 C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 092F1C 7F06052C 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 092F20 7F060530 44813000 */  mtc1  $at, $f6
/* 092F24 7F060534 46000100 */  add.s $f4, $f0, $f0
/* 092F28 7F060538 C60A01C4 */  lwc1  $f10, 0x1c4($s0)
/* 092F2C 7F06053C 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 092F30 7F060540 44819000 */  mtc1  $at, $f18
/* 092F34 7F060544 46062203 */  div.s $f8, $f4, $f6
/* 092F38 7F060548 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 092F3C 7F06054C 46085400 */  add.s $f16, $f10, $f8
/* 092F40 7F060550 E61001C4 */  swc1  $f16, 0x1c4($s0)
/* 092F44 7F060554 C60401C4 */  lwc1  $f4, 0x1c4($s0)
/* 092F48 7F060558 4604903C */  c.lt.s $f18, $f4
/* 092F4C 7F06055C 00000000 */  nop
/* 092F50 7F060560 4500003F */  bc1f  .L7F060660
/* 092F54 7F060564 00000000 */   nop
/* 092F58 7F060568 44813000 */  mtc1  $at, $f6
/* 092F5C 7F06056C 1000003C */  b     .L7F060660
/* 092F60 7F060570 E60601C4 */   swc1  $f6, 0x1c4($s0)
.L7F060574:
/* 092F64 7F060574 C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 092F68 7F060578 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 092F6C 7F06057C 44814000 */  mtc1  $at, $f8
/* 092F70 7F060580 46000280 */  add.s $f10, $f0, $f0
/* 092F74 7F060584 C61201C4 */  lwc1  $f18, 0x1c4($s0)
/* 092F78 7F060588 46085403 */  div.s $f16, $f10, $f8
/* 092F7C 7F06058C 44805000 */  mtc1  $zero, $f10
/* 092F80 7F060590 46109101 */  sub.s $f4, $f18, $f16
/* 092F84 7F060594 E60401C4 */  swc1  $f4, 0x1c4($s0)
/* 092F88 7F060598 C60601C4 */  lwc1  $f6, 0x1c4($s0)
/* 092F8C 7F06059C 460A303C */  c.lt.s $f6, $f10
/* 092F90 7F0605A0 00000000 */  nop
/* 092F94 7F0605A4 4500002E */  bc1f  .L7F060660
/* 092F98 7F0605A8 00000000 */   nop
/* 092F9C 7F0605AC 44804000 */  mtc1  $zero, $f8
/* 092FA0 7F0605B0 1000002B */  b     .L7F060660
/* 092FA4 7F0605B4 E60801C4 */   swc1  $f8, 0x1c4($s0)
.L7F0605B8:
/* 092FA8 7F0605B8 0FC177BF */  jal   get_item_in_hand_or_watch_menu
/* 092FAC 7F0605BC 00002025 */   move  $a0, $zero
/* 092FB0 7F0605C0 00402025 */  move  $a0, $v0
/* 092FB4 7F0605C4 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 092FB8 7F0605C8 24050800 */   li    $a1, 2048
/* 092FBC 7F0605CC 10400013 */  beqz  $v0, .L7F06061C
/* 092FC0 7F0605D0 3C018004 */   lui   $at, 0x8004
/* 092FC4 7F0605D4 3C01C000 */  li    $at, 0xC0000000 # -2.000000
/* 092FC8 7F0605D8 44811000 */  mtc1  $at, $f2
/* 092FCC 7F0605DC 3C018004 */  lui   $at, %hi(g_GlobalTimerDelta) # $at, 0x8004
/* 092FD0 7F0605E0 C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 092FD4 7F0605E4 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 092FD8 7F0605E8 44818000 */  mtc1  $at, $f16
/* 092FDC 7F0605EC 46000480 */  add.s $f18, $f0, $f0
/* 092FE0 7F0605F0 C60601C4 */  lwc1  $f6, 0x1c4($s0)
/* 092FE4 7F0605F4 46109103 */  div.s $f4, $f18, $f16
/* 092FE8 7F0605F8 46043281 */  sub.s $f10, $f6, $f4
/* 092FEC 7F0605FC E60A01C4 */  swc1  $f10, 0x1c4($s0)
/* 092FF0 7F060600 C60801C4 */  lwc1  $f8, 0x1c4($s0)
/* 092FF4 7F060604 4602403C */  c.lt.s $f8, $f2
/* 092FF8 7F060608 00000000 */  nop
/* 092FFC 7F06060C 45000014 */  bc1f  .L7F060660
/* 093000 7F060610 00000000 */   nop
/* 093004 7F060614 10000012 */  b     .L7F060660
/* 093008 7F060618 E60201C4 */   swc1  $f2, 0x1c4($s0)
.L7F06061C:
/* 09300C 7F06061C C4201004 */  lwc1  $f0, %lo(g_GlobalTimerDelta)($at)
/* 093010 7F060620 3C014370 */  li    $at, 0x43700000 # 240.000000
/* 093014 7F060624 44818000 */  mtc1  $at, $f16
/* 093018 7F060628 46000480 */  add.s $f18, $f0, $f0
/* 09301C 7F06062C C60401C4 */  lwc1  $f4, 0x1c4($s0)
/* 093020 7F060630 44804000 */  mtc1  $zero, $f8
/* 093024 7F060634 46109183 */  div.s $f6, $f18, $f16
/* 093028 7F060638 46062280 */  add.s $f10, $f4, $f6
/* 09302C 7F06063C E60A01C4 */  swc1  $f10, 0x1c4($s0)
/* 093030 7F060640 C61201C4 */  lwc1  $f18, 0x1c4($s0)
/* 093034 7F060644 4612403C */  c.lt.s $f8, $f18
/* 093038 7F060648 00000000 */  nop
/* 09303C 7F06064C 45000004 */  bc1f  .L7F060660
/* 093040 7F060650 00000000 */   nop
/* 093044 7F060654 44808000 */  mtc1  $zero, $f16
/* 093048 7F060658 00000000 */  nop
/* 09304C 7F06065C E61001C4 */  swc1  $f16, 0x1c4($s0)
.L7F060660:
/* 093050 7F060660 3C0F8003 */  lui   $t7, %hi(D_80035C4C) # $t7, 0x8003
/* 093054 7F060664 25EF119C */  addiu $t7, %lo(D_80035C4C) # addiu $t7, $t7, 0x119c
/* 093058 7F060668 8DE10000 */  lw    $at, ($t7)
/* 09305C 7F06066C 27AC00E0 */  addiu $t4, $sp, 0xe0
/* 093060 7F060670 3C0E8003 */  lui   $t6, %hi(D_80035C58) # $t6, 0x8003
/* 093064 7F060674 AD810000 */  sw    $at, ($t4)
/* 093068 7F060678 8DF90004 */  lw    $t9, 4($t7)
/* 09306C 7F06067C 25CE11A8 */  addiu $t6, %lo(D_80035C58) # addiu $t6, $t6, 0x11a8
/* 093070 7F060680 27B800D4 */  addiu $t8, $sp, 0xd4
/* 093074 7F060684 AD990004 */  sw    $t9, 4($t4)
/* 093078 7F060688 8DE10008 */  lw    $at, 8($t7)
/* 09307C 7F06068C 3C0D8003 */  lui   $t5, %hi(D_80035C64) # $t5, 0x8003
/* 093080 7F060690 25AD11B4 */  addiu $t5, %lo(D_80035C64) # addiu $t5, $t5, 0x11b4
/* 093084 7F060694 AD810008 */  sw    $at, 8($t4)
/* 093088 7F060698 8DC10000 */  lw    $at, ($t6)
/* 09308C 7F06069C 8DCF0004 */  lw    $t7, 4($t6)
/* 093090 7F0606A0 27B900C8 */  addiu $t9, $sp, 0xc8
/* 093094 7F0606A4 AF010000 */  sw    $at, ($t8)
/* 093098 7F0606A8 8DC10008 */  lw    $at, 8($t6)
/* 09309C 7F0606AC AF0F0004 */  sw    $t7, 4($t8)
/* 0930A0 7F0606B0 2403000C */  li    $v1, 12
/* 0930A4 7F0606B4 AF010008 */  sw    $at, 8($t8)
/* 0930A8 7F0606B8 8DA10000 */  lw    $at, ($t5)
/* 0930AC 7F0606BC 8DAE0004 */  lw    $t6, 4($t5)
/* 0930B0 7F0606C0 AF210000 */  sw    $at, ($t9)
/* 0930B4 7F0606C4 8DA10008 */  lw    $at, 8($t5)
/* 0930B8 7F0606C8 AF2E0004 */  sw    $t6, 4($t9)
/* 0930BC 7F0606CC AF210008 */  sw    $at, 8($t9)
/* 0930C0 7F0606D0 8E020198 */  lw    $v0, 0x198($s0)
/* 0930C4 7F0606D4 C604019C */  lwc1  $f4, 0x19c($s0)
/* 0930C8 7F0606D8 AFAC0014 */  sw    $t4, 0x14($sp)
/* 0930CC 7F0606DC 244F0003 */  addiu $t7, $v0, 3
/* 0930D0 7F0606E0 05E10004 */  bgez  $t7, .L7F0606F4
/* 0930D4 7F0606E4 31F80003 */   andi  $t8, $t7, 3
/* 0930D8 7F0606E8 13000002 */  beqz  $t8, .L7F0606F4
/* 0930DC 7F0606EC 00000000 */   nop
/* 0930E0 7F0606F0 2718FFFC */  addiu $t8, $t8, -4
.L7F0606F4:
/* 0930E4 7F0606F4 03030019 */  multu $t8, $v1
/* 0930E8 7F0606F8 244E0001 */  addiu $t6, $v0, 1
/* 0930EC 7F0606FC E7A40010 */  swc1  $f4, 0x10($sp)
/* 0930F0 7F060700 0000C812 */  mflo  $t9
/* 0930F4 7F060704 02194021 */  addu  $t0, $s0, $t9
/* 0930F8 7F060708 24590002 */  addiu $t9, $v0, 2
/* 0930FC 7F06070C 00430019 */  multu $v0, $v1
/* 093100 7F060710 25040108 */  addiu $a0, $t0, 0x108
/* 093104 7F060714 AFA80044 */  sw    $t0, 0x44($sp)
/* 093108 7F060718 00006812 */  mflo  $t5
/* 09310C 7F06071C 020D4821 */  addu  $t1, $s0, $t5
/* 093110 7F060720 25250108 */  addiu $a1, $t1, 0x108
/* 093114 7F060724 05C10004 */  bgez  $t6, .L7F060738
/* 093118 7F060728 31CF0003 */   andi  $t7, $t6, 3
/* 09311C 7F06072C 11E00002 */  beqz  $t7, .L7F060738
/* 093120 7F060730 00000000 */   nop
/* 093124 7F060734 25EFFFFC */  addiu $t7, $t7, -4
.L7F060738:
/* 093128 7F060738 01E30019 */  multu $t7, $v1
/* 09312C 7F06073C AFA90040 */  sw    $t1, 0x40($sp)
/* 093130 7F060740 0000C012 */  mflo  $t8
/* 093134 7F060744 02185021 */  addu  $t2, $s0, $t8
/* 093138 7F060748 25460108 */  addiu $a2, $t2, 0x108
/* 09313C 7F06074C 07210004 */  bgez  $t9, .L7F060760
/* 093140 7F060750 332D0003 */   andi  $t5, $t9, 3
/* 093144 7F060754 11A00002 */  beqz  $t5, .L7F060760
/* 093148 7F060758 00000000 */   nop
/* 09314C 7F06075C 25ADFFFC */  addiu $t5, $t5, -4
.L7F060760:
/* 093150 7F060760 01A30019 */  multu $t5, $v1
/* 093154 7F060764 AFAA003C */  sw    $t2, 0x3c($sp)
/* 093158 7F060768 00007012 */  mflo  $t6
/* 09315C 7F06076C 020E5821 */  addu  $t3, $s0, $t6
/* 093160 7F060770 25670108 */  addiu $a3, $t3, 0x108
/* 093164 7F060774 0FC16CEB */  jal   sub_GAME_7F05AEFC
/* 093168 7F060778 AFAB0038 */   sw    $t3, 0x38($sp)
/* 09316C 7F06077C 8FA40044 */  lw    $a0, 0x44($sp)
/* 093170 7F060780 8FA50040 */  lw    $a1, 0x40($sp)
/* 093174 7F060784 8FA6003C */  lw    $a2, 0x3c($sp)
/* 093178 7F060788 8FA70038 */  lw    $a3, 0x38($sp)
/* 09317C 7F06078C C606019C */  lwc1  $f6, 0x19c($s0)
/* 093180 7F060790 27AF00D4 */  addiu $t7, $sp, 0xd4
/* 093184 7F060794 AFAF0014 */  sw    $t7, 0x14($sp)
/* 093188 7F060798 24840138 */  addiu $a0, $a0, 0x138
/* 09318C 7F06079C 24A50138 */  addiu $a1, $a1, 0x138
/* 093190 7F0607A0 24C60138 */  addiu $a2, $a2, 0x138
/* 093194 7F0607A4 24E70138 */  addiu $a3, $a3, 0x138
/* 093198 7F0607A8 0FC16CEB */  jal   sub_GAME_7F05AEFC
/* 09319C 7F0607AC E7A60010 */   swc1  $f6, 0x10($sp)
/* 0931A0 7F0607B0 8FA40044 */  lw    $a0, 0x44($sp)
/* 0931A4 7F0607B4 8FA50040 */  lw    $a1, 0x40($sp)
/* 0931A8 7F0607B8 8FA6003C */  lw    $a2, 0x3c($sp)
/* 0931AC 7F0607BC 8FA70038 */  lw    $a3, 0x38($sp)
/* 0931B0 7F0607C0 C60A019C */  lwc1  $f10, 0x19c($s0)
/* 0931B4 7F0607C4 27B800C8 */  addiu $t8, $sp, 0xc8
/* 0931B8 7F0607C8 AFB80014 */  sw    $t8, 0x14($sp)
/* 0931BC 7F0607CC 24840168 */  addiu $a0, $a0, 0x168
/* 0931C0 7F0607D0 24A50168 */  addiu $a1, $a1, 0x168
/* 0931C4 7F0607D4 24C60168 */  addiu $a2, $a2, 0x168
/* 0931C8 7F0607D8 24E70168 */  addiu $a3, $a3, 0x168
/* 0931CC 7F0607DC 0FC16CEB */  jal   sub_GAME_7F05AEFC
/* 0931D0 7F0607E0 E7AA0010 */   swc1  $f10, 0x10($sp)
/* 0931D4 7F0607E4 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 0931D8 7F0607E8 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 0931DC 7F0607EC C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 0931E0 7F0607F0 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 0931E4 7F0607F4 C4520FB8 */  lwc1  $f18, 0xfb8($v0)
/* 0931E8 7F0607F8 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 0931EC 7F0607FC 46124402 */  mul.s $f16, $f8, $f18
/* 0931F0 7F060800 C7A800E8 */  lwc1  $f8, 0xe8($sp)
/* 0931F4 7F060804 E7B000E0 */  swc1  $f16, 0xe0($sp)
/* 0931F8 7F060808 C4460FB8 */  lwc1  $f6, 0xfb8($v0)
/* 0931FC 7F06080C 46062282 */  mul.s $f10, $f4, $f6
/* 093200 7F060810 E7AA00E4 */  swc1  $f10, 0xe4($sp)
/* 093204 7F060814 C4520FB8 */  lwc1  $f18, 0xfb8($v0)
/* 093208 7F060818 46124102 */  mul.s $f4, $f8, $f18
/* 09320C 7F06081C E7A400E8 */  swc1  $f4, 0xe8($sp)
/* 093210 7F060820 C60601AC */  lwc1  $f6, 0x1ac($s0)
/* 093214 7F060824 46068200 */  add.s $f8, $f16, $f6
/* 093218 7F060828 E7A800E0 */  swc1  $f8, 0xe0($sp)
/* 09321C 7F06082C C61201B0 */  lwc1  $f18, 0x1b0($s0)
/* 093220 7F060830 46125100 */  add.s $f4, $f10, $f18
/* 093224 7F060834 0FC1785C */  jal   sub_GAME_7F05DCB8
/* 093228 7F060838 E7A400E4 */   swc1  $f4, 0xe4($sp)
/* 09322C 7F06083C C7B000E0 */  lwc1  $f16, 0xe0($sp)
/* 093230 7F060840 3C028004 */  lui   $v0, %hi(g_ClockTimer) # $v0, 0x8004
/* 093234 7F060844 24420FF4 */  addiu $v0, %lo(g_ClockTimer) # addiu $v0, $v0, 0xff4
/* 093238 7F060848 46008180 */  add.s $f6, $f16, $f0
/* 09323C 7F06084C 8C590000 */  lw    $t9, ($v0)
/* 093240 7F060850 00001825 */  move  $v1, $zero
/* 093244 7F060854 1B200035 */  blez  $t9, .L7F06092C
/* 093248 7F060858 E7A600E0 */   swc1  $f6, 0xe0($sp)
/* 09324C 7F06085C 3C018005 */  lui   $at, %hi(D_80053DE0) # $at, 0x8005
/* 093250 7F060860 C4209F20 */  lwc1  $f0, %lo(D_80053DE0)($at)
/* 093254 7F060864 C60A00E4 */  lwc1  $f10, 0xe4($s0)
.L7F060868:
/* 093258 7F060868 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 09325C 7F06086C C60600E8 */  lwc1  $f6, 0xe8($s0)
/* 093260 7F060870 460A0482 */  mul.s $f18, $f0, $f10
/* 093264 7F060874 24630001 */  addiu $v1, $v1, 1
/* 093268 7F060878 46060282 */  mul.s $f10, $f0, $f6
/* 09326C 7F06087C 46124100 */  add.s $f4, $f8, $f18
/* 093270 7F060880 E60400E4 */  swc1  $f4, 0xe4($s0)
/* 093274 7F060884 C7B000E4 */  lwc1  $f16, 0xe4($sp)
/* 093278 7F060888 C60400EC */  lwc1  $f4, 0xec($s0)
/* 09327C 7F06088C 460A8200 */  add.s $f8, $f16, $f10
/* 093280 7F060890 46040182 */  mul.s $f6, $f0, $f4
/* 093284 7F060894 E60800E8 */  swc1  $f8, 0xe8($s0)
/* 093288 7F060898 C7B200E8 */  lwc1  $f18, 0xe8($sp)
/* 09328C 7F06089C C60800F0 */  lwc1  $f8, 0xf0($s0)
/* 093290 7F0608A0 46069400 */  add.s $f16, $f18, $f6
/* 093294 7F0608A4 46080102 */  mul.s $f4, $f0, $f8
/* 093298 7F0608A8 E61000EC */  swc1  $f16, 0xec($s0)
/* 09329C 7F0608AC C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 0932A0 7F0608B0 C61000F4 */  lwc1  $f16, 0xf4($s0)
/* 0932A4 7F0608B4 46045480 */  add.s $f18, $f10, $f4
/* 0932A8 7F0608B8 46100202 */  mul.s $f8, $f0, $f16
/* 0932AC 7F0608BC E61200F0 */  swc1  $f18, 0xf0($s0)
/* 0932B0 7F0608C0 C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 0932B4 7F0608C4 C61200F8 */  lwc1  $f18, 0xf8($s0)
/* 0932B8 7F0608C8 46083280 */  add.s $f10, $f6, $f8
/* 0932BC 7F0608CC 46120402 */  mul.s $f16, $f0, $f18
/* 0932C0 7F0608D0 E60A00F4 */  swc1  $f10, 0xf4($s0)
/* 0932C4 7F0608D4 C7A400DC */  lwc1  $f4, 0xdc($sp)
/* 0932C8 7F0608D8 C60A00FC */  lwc1  $f10, 0xfc($s0)
/* 0932CC 7F0608DC 46102180 */  add.s $f6, $f4, $f16
/* 0932D0 7F0608E0 460A0482 */  mul.s $f18, $f0, $f10
/* 0932D4 7F0608E4 E60600F8 */  swc1  $f6, 0xf8($s0)
/* 0932D8 7F0608E8 C7A800C8 */  lwc1  $f8, 0xc8($sp)
/* 0932DC 7F0608EC C6060100 */  lwc1  $f6, 0x100($s0)
/* 0932E0 7F0608F0 46124100 */  add.s $f4, $f8, $f18
/* 0932E4 7F0608F4 46060282 */  mul.s $f10, $f0, $f6
/* 0932E8 7F0608F8 E60400FC */  swc1  $f4, 0xfc($s0)
/* 0932EC 7F0608FC C7B000CC */  lwc1  $f16, 0xcc($sp)
/* 0932F0 7F060900 C6040104 */  lwc1  $f4, 0x104($s0)
/* 0932F4 7F060904 460A8200 */  add.s $f8, $f16, $f10
/* 0932F8 7F060908 46040182 */  mul.s $f6, $f0, $f4
/* 0932FC 7F06090C E6080100 */  swc1  $f8, 0x100($s0)
/* 093300 7F060910 C7B200D0 */  lwc1  $f18, 0xd0($sp)
/* 093304 7F060914 46069400 */  add.s $f16, $f18, $f6
/* 093308 7F060918 E6100104 */  swc1  $f16, 0x104($s0)
/* 09330C 7F06091C 8C4D0000 */  lw    $t5, ($v0)
/* 093310 7F060920 006D082A */  slt   $at, $v1, $t5
/* 093314 7F060924 5420FFD0 */  bnezl $at, .L7F060868
/* 093318 7F060928 C60A00E4 */   lwc1  $f10, 0xe4($s0)
.L7F06092C:
/* 09331C 7F06092C 3C018005 */  lui   $at, %hi(D_80053DE4) # $at, 0x8005
/* 093320 7F060930 C4209F24 */  lwc1  $f0, %lo(D_80053DE4)($at)
/* 093324 7F060934 C60A00E4 */  lwc1  $f10, 0xe4($s0)
/* 093328 7F060938 C60400E8 */  lwc1  $f4, 0xe8($s0)
/* 09332C 7F06093C C60600EC */  lwc1  $f6, 0xec($s0)
/* 093330 7F060940 46005202 */  mul.s $f8, $f10, $f0
/* 093334 7F060944 C60A00F0 */  lwc1  $f10, 0xf0($s0)
/* 093338 7F060948 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 09333C 7F06094C 46002482 */  mul.s $f18, $f4, $f0
/* 093340 7F060950 C60400F4 */  lwc1  $f4, 0xf4($s0)
/* 093344 7F060954 46003402 */  mul.s $f16, $f6, $f0
/* 093348 7F060958 E60800C0 */  swc1  $f8, 0xc0($s0)
/* 09334C 7F06095C C60600F8 */  lwc1  $f6, 0xf8($s0)
/* 093350 7F060960 46005202 */  mul.s $f8, $f10, $f0
/* 093354 7F060964 E61200C4 */  swc1  $f18, 0xc4($s0)
/* 093358 7F060968 C60A00FC */  lwc1  $f10, 0xfc($s0)
/* 09335C 7F06096C 46002482 */  mul.s $f18, $f4, $f0
/* 093360 7F060970 E61000C8 */  swc1  $f16, 0xc8($s0)
/* 093364 7F060974 C6040100 */  lwc1  $f4, 0x100($s0)
/* 093368 7F060978 46003402 */  mul.s $f16, $f6, $f0
/* 09336C 7F06097C E60800CC */  swc1  $f8, 0xcc($s0)
/* 093370 7F060980 C6060104 */  lwc1  $f6, 0x104($s0)
/* 093374 7F060984 46005202 */  mul.s $f8, $f10, $f0
/* 093378 7F060988 E61200D0 */  swc1  $f18, 0xd0($s0)
/* 09337C 7F06098C 46002482 */  mul.s $f18, $f4, $f0
/* 093380 7F060990 E61000D4 */  swc1  $f16, 0xd4($s0)
/* 093384 7F060994 46003402 */  mul.s $f16, $f6, $f0
/* 093388 7F060998 E60800D8 */  swc1  $f8, 0xd8($s0)
/* 09338C 7F06099C E61200DC */  swc1  $f18, 0xdc($s0)
/* 093390 7F0609A0 14800009 */  bnez  $a0, .L7F0609C8
/* 093394 7F0609A4 E61000E0 */   swc1  $f16, 0xe0($s0)
/* 093398 7F0609A8 0FC17868 */  jal   gunSetHorizontalOffset
/* 09339C 7F0609AC 00000000 */   nop
/* 0933A0 7F0609B0 C60800C0 */  lwc1  $f8, 0xc0($s0)
/* 0933A4 7F0609B4 C60A01B8 */  lwc1  $f10, 0x1b8($s0)
/* 0933A8 7F0609B8 46080100 */  add.s $f4, $f0, $f8
/* 0933AC 7F0609BC 46045480 */  add.s $f18, $f10, $f4
/* 0933B0 7F0609C0 10000008 */  b     .L7F0609E4
/* 0933B4 7F0609C4 E7B20194 */   swc1  $f18, 0x194($sp)
.L7F0609C8:
/* 0933B8 7F0609C8 0FC17868 */  jal   gunSetHorizontalOffset
/* 0933BC 7F0609CC 00000000 */   nop
/* 0933C0 7F0609D0 C60600C0 */  lwc1  $f6, 0xc0($s0)
/* 0933C4 7F0609D4 C60801B8 */  lwc1  $f8, 0x1b8($s0)
/* 0933C8 7F0609D8 46060400 */  add.s $f16, $f0, $f6
/* 0933CC 7F0609DC 46088281 */  sub.s $f10, $f16, $f8
/* 0933D0 7F0609E0 E7AA0194 */  swc1  $f10, 0x194($sp)
.L7F0609E4:
/* 0933D4 7F0609E4 8FAE00F8 */  lw    $t6, 0xf8($sp)
/* 0933D8 7F0609E8 C61200C4 */  lwc1  $f18, 0xc4($s0)
/* 0933DC 7F0609EC C61001BC */  lwc1  $f16, 0x1bc($s0)
/* 0933E0 7F0609F0 C5C40008 */  lwc1  $f4, 8($t6)
/* 0933E4 7F0609F4 8FA400FC */  lw    $a0, 0xfc($sp)
/* 0933E8 7F0609F8 24010019 */  li    $at, 25
/* 0933EC 7F0609FC 46122180 */  add.s $f6, $f4, $f18
/* 0933F0 7F060A00 46068200 */  add.s $f8, $f16, $f6
/* 0933F4 7F060A04 E7A80198 */  swc1  $f8, 0x198($sp)
/* 0933F8 7F060A08 C60400C8 */  lwc1  $f4, 0xc8($s0)
/* 0933FC 7F060A0C C5CA000C */  lwc1  $f10, 0xc($t6)
/* 093400 7F060A10 C61001C0 */  lwc1  $f16, 0x1c0($s0)
/* 093404 7F060A14 46045480 */  add.s $f18, $f10, $f4
/* 093408 7F060A18 46128180 */  add.s $f6, $f16, $f18
/* 09340C 7F060A1C 10810005 */  beq   $a0, $at, .L7F060A34
/* 093410 7F060A20 E7A6019C */   swc1  $f6, 0x19c($sp)
/* 093414 7F060A24 2401001E */  li    $at, 30
/* 093418 7F060A28 10810002 */  beq   $a0, $at, .L7F060A34
/* 09341C 7F060A2C 24010017 */   li    $at, 23
/* 093420 7F060A30 14810028 */  bne   $a0, $at, .L7F060AD4
.L7F060A34:
/* 093424 7F060A34 3C028007 */   lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 093428 7F060A38 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09342C 7F060A3C 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 093430 7F060A40 44810000 */  mtc1  $at, $f0
/* 093434 7F060A44 C44A00A0 */  lwc1  $f10, 0xa0($v0)
/* 093438 7F060A48 C7A80198 */  lwc1  $f8, 0x198($sp)
/* 09343C 7F060A4C 3C014040 */  li    $at, 0x40400000 # 3.000000
/* 093440 7F060A50 46005103 */  div.s $f4, $f10, $f0
/* 093444 7F060A54 44819000 */  mtc1  $at, $f18
/* 093448 7F060A58 24010019 */  li    $at, 25
/* 09344C 7F060A5C 46044400 */  add.s $f16, $f8, $f4
/* 093450 7F060A60 C7A4019C */  lwc1  $f4, 0x19c($sp)
/* 093454 7F060A64 E7B00198 */  swc1  $f16, 0x198($sp)
/* 093458 7F060A68 C44600A0 */  lwc1  $f6, 0xa0($v0)
/* 09345C 7F060A6C 46069282 */  mul.s $f10, $f18, $f6
/* 093460 7F060A70 46005203 */  div.s $f8, $f10, $f0
/* 093464 7F060A74 46082400 */  add.s $f16, $f4, $f8
/* 093468 7F060A78 14810014 */  bne   $a0, $at, .L7F060ACC
/* 09346C 7F060A7C E7B0019C */   swc1  $f16, 0x19c($sp)
/* 093470 7F060A80 0FC2907A */  jal   cur_player_get_screen_setting
/* 093474 7F060A84 00000000 */   nop
/* 093478 7F060A88 24010001 */  li    $at, 1
/* 09347C 7F060A8C 5041000B */  beql  $v0, $at, .L7F060ABC
/* 093480 7F060A90 3C014040 */   lui   $at, 0x4040
/* 093484 7F060A94 0FC2907A */  jal   cur_player_get_screen_setting
/* 093488 7F060A98 00000000 */   nop
/* 09348C 7F060A9C 24010002 */  li    $at, 2
/* 093490 7F060AA0 50410006 */  beql  $v0, $at, .L7F060ABC
/* 093494 7F060AA4 3C014040 */   lui   $at, 0x4040
/* 093498 7F060AA8 0FC29080 */  jal   get_screen_ratio
/* 09349C 7F060AAC 00000000 */   nop
/* 0934A0 7F060AB0 24010001 */  li    $at, 1
/* 0934A4 7F060AB4 14410005 */  bne   $v0, $at, .L7F060ACC
/* 0934A8 7F060AB8 3C014040 */   li    $at, 0x40400000 # 3.000000
.L7F060ABC:
/* 0934AC 7F060ABC 44813000 */  mtc1  $at, $f6
/* 0934B0 7F060AC0 C7B20198 */  lwc1  $f18, 0x198($sp)
/* 0934B4 7F060AC4 46069281 */  sub.s $f10, $f18, $f6
/* 0934B8 7F060AC8 E7AA0198 */  swc1  $f10, 0x198($sp)
.L7F060ACC:
/* 0934BC 7F060ACC 1000002C */  b     .L7F060B80
/* 0934C0 7F060AD0 8FA400FC */   lw    $a0, 0xfc($sp)
.L7F060AD4:
/* 0934C4 7F060AD4 2401001F */  li    $at, 31
/* 0934C8 7F060AD8 14810016 */  bne   $a0, $at, .L7F060B34
/* 0934CC 7F060ADC 3C028007 */   lui   $v0, 0x8007
/* 0934D0 7F060AE0 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 0934D4 7F060AE4 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 0934D8 7F060AE8 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 0934DC 7F060AEC 44810000 */  mtc1  $at, $f0
/* 0934E0 7F060AF0 3C014020 */  li    $at, 0x40200000 # 2.500000
/* 0934E4 7F060AF4 44812000 */  mtc1  $at, $f4
/* 0934E8 7F060AF8 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 0934EC 7F060AFC C7A60198 */  lwc1  $f6, 0x198($sp)
/* 0934F0 7F060B00 3C0140F0 */  li    $at, 0x40F00000 # 7.500000
/* 0934F4 7F060B04 46082402 */  mul.s $f16, $f4, $f8
/* 0934F8 7F060B08 44812000 */  mtc1  $at, $f4
/* 0934FC 7F060B0C 46008483 */  div.s $f18, $f16, $f0
/* 093500 7F060B10 46123280 */  add.s $f10, $f6, $f18
/* 093504 7F060B14 C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 093508 7F060B18 E7AA0198 */  swc1  $f10, 0x198($sp)
/* 09350C 7F060B1C C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 093510 7F060B20 46082402 */  mul.s $f16, $f4, $f8
/* 093514 7F060B24 46008183 */  div.s $f6, $f16, $f0
/* 093518 7F060B28 46069280 */  add.s $f10, $f18, $f6
/* 09351C 7F060B2C 10000014 */  b     .L7F060B80
/* 093520 7F060B30 E7AA019C */   swc1  $f10, 0x19c($sp)
.L7F060B34:
/* 093524 7F060B34 8C428BC0 */  lw    $v0, -0x7440($v0)
/* 093528 7F060B38 3C01C2C8 */  li    $at, 0xC2C80000 # -100.000000
/* 09352C 7F060B3C 44810000 */  mtc1  $at, $f0
/* 093530 7F060B40 3C0140A0 */  li    $at, 0x40A00000 # 5.000000
/* 093534 7F060B44 44812000 */  mtc1  $at, $f4
/* 093538 7F060B48 C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 09353C 7F060B4C C7A60198 */  lwc1  $f6, 0x198($sp)
/* 093540 7F060B50 3C014170 */  li    $at, 0x41700000 # 15.000000
/* 093544 7F060B54 46082402 */  mul.s $f16, $f4, $f8
/* 093548 7F060B58 44812000 */  mtc1  $at, $f4
/* 09354C 7F060B5C 46008483 */  div.s $f18, $f16, $f0
/* 093550 7F060B60 46123280 */  add.s $f10, $f6, $f18
/* 093554 7F060B64 C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 093558 7F060B68 E7AA0198 */  swc1  $f10, 0x198($sp)
/* 09355C 7F060B6C C44800A0 */  lwc1  $f8, 0xa0($v0)
/* 093560 7F060B70 46082402 */  mul.s $f16, $f4, $f8
/* 093564 7F060B74 46008183 */  div.s $f6, $f16, $f0
/* 093568 7F060B78 46069280 */  add.s $f10, $f18, $f6
/* 09356C 7F060B7C E7AA019C */  swc1  $f10, 0x19c($sp)
.L7F060B80:
/* 093570 7F060B80 820F000C */  lb    $t7, 0xc($s0)
/* 093574 7F060B84 11E00047 */  beqz  $t7, .L7F060CA4
/* 093578 7F060B88 00000000 */   nop
/* 09357C 7F060B8C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093580 7F060B90 24050020 */   li    $a1, 32
/* 093584 7F060B94 10400043 */  beqz  $v0, .L7F060CA4
/* 093588 7F060B98 8FA400FC */   lw    $a0, 0xfc($sp)
/* 09358C 7F060B9C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093590 7F060BA0 24050040 */   li    $a1, 64
/* 093594 7F060BA4 10400016 */  beqz  $v0, .L7F060C00
/* 093598 7F060BA8 00000000 */   nop
/* 09359C 7F060BAC 0C00262C */  jal   randomGetNext
/* 0935A0 7F060BB0 00000000 */   nop
/* 0935A4 7F060BB4 44822000 */  mtc1  $v0, $f4
/* 0935A8 7F060BB8 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0935AC 7F060BBC 04410004 */  bgez  $v0, .L7F060BD0
/* 0935B0 7F060BC0 46802220 */   cvt.s.w $f8, $f4
/* 0935B4 7F060BC4 44818000 */  mtc1  $at, $f16
/* 0935B8 7F060BC8 00000000 */  nop
/* 0935BC 7F060BCC 46104200 */  add.s $f8, $f8, $f16
.L7F060BD0:
/* 0935C0 7F060BD0 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0935C4 7F060BD4 44819000 */  mtc1  $at, $f18
/* 0935C8 7F060BD8 3C018005 */  lui   $at, %hi(D_80053DE8) # $at, 0x8005
/* 0935CC 7F060BDC C42A9F28 */  lwc1  $f10, %lo(D_80053DE8)($at)
/* 0935D0 7F060BE0 46124182 */  mul.s $f6, $f8, $f18
/* 0935D4 7F060BE4 3C018005 */  lui   $at, %hi(D_80053DEC) # $at, 0x8005
/* 0935D8 7F060BE8 C4309F2C */  lwc1  $f16, %lo(D_80053DEC)($at)
/* 0935DC 7F060BEC C7B20194 */  lwc1  $f18, 0x194($sp)
/* 0935E0 7F060BF0 460A3102 */  mul.s $f4, $f6, $f10
/* 0935E4 7F060BF4 46048201 */  sub.s $f8, $f16, $f4
/* 0935E8 7F060BF8 46089180 */  add.s $f6, $f18, $f8
/* 0935EC 7F060BFC E7A60194 */  swc1  $f6, 0x194($sp)
.L7F060C00:
/* 0935F0 7F060C00 0C00262C */  jal   randomGetNext
/* 0935F4 7F060C04 00000000 */   nop
/* 0935F8 7F060C08 44825000 */  mtc1  $v0, $f10
/* 0935FC 7F060C0C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 093600 7F060C10 04410004 */  bgez  $v0, .L7F060C24
/* 093604 7F060C14 46805420 */   cvt.s.w $f16, $f10
/* 093608 7F060C18 44812000 */  mtc1  $at, $f4
/* 09360C 7F060C1C 00000000 */  nop
/* 093610 7F060C20 46048400 */  add.s $f16, $f16, $f4
.L7F060C24:
/* 093614 7F060C24 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 093618 7F060C28 44819000 */  mtc1  $at, $f18
/* 09361C 7F060C2C 3C018005 */  lui   $at, %hi(D_80053DF0) # $at, 0x8005
/* 093620 7F060C30 C4269F30 */  lwc1  $f6, %lo(D_80053DF0)($at)
/* 093624 7F060C34 46128202 */  mul.s $f8, $f16, $f18
/* 093628 7F060C38 3C018005 */  lui   $at, %hi(D_80053DF4) # $at, 0x8005
/* 09362C 7F060C3C C4249F34 */  lwc1  $f4, %lo(D_80053DF4)($at)
/* 093630 7F060C40 C7B20198 */  lwc1  $f18, 0x198($sp)
/* 093634 7F060C44 46064282 */  mul.s $f10, $f8, $f6
/* 093638 7F060C48 460A2401 */  sub.s $f16, $f4, $f10
/* 09363C 7F060C4C 46109200 */  add.s $f8, $f18, $f16
/* 093640 7F060C50 0C00262C */  jal   randomGetNext
/* 093644 7F060C54 E7A80198 */   swc1  $f8, 0x198($sp)
/* 093648 7F060C58 44823000 */  mtc1  $v0, $f6
/* 09364C 7F060C5C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 093650 7F060C60 04410004 */  bgez  $v0, .L7F060C74
/* 093654 7F060C64 46803120 */   cvt.s.w $f4, $f6
/* 093658 7F060C68 44815000 */  mtc1  $at, $f10
/* 09365C 7F060C6C 00000000 */  nop
/* 093660 7F060C70 460A2100 */  add.s $f4, $f4, $f10
.L7F060C74:
/* 093664 7F060C74 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 093668 7F060C78 44819000 */  mtc1  $at, $f18
/* 09366C 7F060C7C 3C018005 */  lui   $at, %hi(D_80053DF8) # $at, 0x8005
/* 093670 7F060C80 C4289F38 */  lwc1  $f8, %lo(D_80053DF8)($at)
/* 093674 7F060C84 46122402 */  mul.s $f16, $f4, $f18
/* 093678 7F060C88 3C018005 */  lui    $at, %hi(D_80053DFC)
/* 09367C 7F060C8C C42A9F3C */  lwc1  $f10, %lo(D_80053DFC)($at)
/* 093680 7F060C90 C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 093684 7F060C94 46088182 */  mul.s $f6, $f16, $f8
/* 093688 7F060C98 46065101 */  sub.s $f4, $f10, $f6
/* 09368C 7F060C9C 46049400 */  add.s $f16, $f18, $f4
/* 093690 7F060CA0 E7B0019C */  swc1  $f16, 0x19c($sp)
.L7F060CA4:
/* 093694 7F060CA4 0FC1E149 */  jal   getPlayer_c_screenwidth
/* 093698 7F060CA8 00000000 */   nop
/* 09369C 7F060CAC 0FC1E149 */  jal   getPlayer_c_screenwidth
/* 0936A0 7F060CB0 E7A00048 */   swc1  $f0, 0x48($sp)
/* 0936A4 7F060CB4 0FC1E151 */  jal   getPlayer_c_screenleft
/* 0936A8 7F060CB8 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 0936AC 7F060CBC 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 0936B0 7F060CC0 3C188007 */  lui   $t8, %hi(g_CurrentPlayer) # $t8, 0x8007
/* 0936B4 7F060CC4 8F188BC0 */  lw    $t8, %lo(g_CurrentPlayer)($t8)
/* 0936B8 7F060CC8 44811000 */  mtc1  $at, $f2
/* 0936BC 7F060CCC C7A6004C */  lwc1  $f6, 0x4c($sp)
/* 0936C0 7F060CD0 C7080FF4 */  lwc1  $f8, 0xff4($t8)
/* 0936C4 7F060CD4 8FB900F8 */  lw    $t9, 0xf8($sp)
/* 0936C8 7F060CD8 46023482 */  mul.s $f18, $f6, $f2
/* 0936CC 7F060CDC 46004281 */  sub.s $f10, $f8, $f0
/* 0936D0 7F060CE0 C7300018 */  lwc1  $f16, 0x18($t9)
/* 0936D4 7F060CE4 C7A60048 */  lwc1  $f6, 0x48($sp)
/* 0936D8 7F060CE8 46125101 */  sub.s $f4, $f10, $f18
/* 0936DC 7F060CEC 46102202 */  mul.s $f8, $f4, $f16
/* 0936E0 7F060CF0 C7A40194 */  lwc1  $f4, 0x194($sp)
/* 0936E4 7F060CF4 46023282 */  mul.s $f10, $f6, $f2
/* 0936E8 7F060CF8 460A4483 */  div.s $f18, $f8, $f10
/* 0936EC 7F060CFC 46122400 */  add.s $f16, $f4, $f18
/* 0936F0 7F060D00 0FC1E155 */  jal   getPlayer_c_screentop
/* 0936F4 7F060D04 E7B00194 */   swc1  $f16, 0x194($sp)
/* 0936F8 7F060D08 0FC1E14D */  jal   getPlayer_c_screenheight
/* 0936FC 7F060D0C E7A00050 */   swc1  $f0, 0x50($sp)
/* 093700 7F060D10 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 093704 7F060D14 3C0D8007 */  lui   $t5, %hi(g_CurrentPlayer) # $t5, 0x8007
/* 093708 7F060D18 8DAD8BC0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 09370C 7F060D1C 44813000 */  mtc1  $at, $f6
/* 093710 7F060D20 C7A40050 */  lwc1  $f4, 0x50($sp)
/* 093714 7F060D24 C5AA0FF8 */  lwc1  $f10, 0xff8($t5)
/* 093718 7F060D28 46060202 */  mul.s $f8, $f0, $f6
/* 09371C 7F060D2C 46045481 */  sub.s $f18, $f10, $f4
/* 093720 7F060D30 4612403C */  c.lt.s $f8, $f18
/* 093724 7F060D34 00000000 */  nop
/* 093728 7F060D38 4500001A */  bc1f  .L7F060DA4
/* 09372C 7F060D3C 00000000 */   nop
/* 093730 7F060D40 0FC1E14D */  jal   getPlayer_c_screenheight
/* 093734 7F060D44 00000000 */   nop
/* 093738 7F060D48 0FC1E14D */  jal   getPlayer_c_screenheight
/* 09373C 7F060D4C E7A00048 */   swc1  $f0, 0x48($sp)
/* 093740 7F060D50 0FC1E155 */  jal   getPlayer_c_screentop
/* 093744 7F060D54 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 093748 7F060D58 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09374C 7F060D5C 3C0E8007 */  lui   $t6, %hi(g_CurrentPlayer) # $t6, 0x8007
/* 093750 7F060D60 8DCE8BC0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 093754 7F060D64 44811000 */  mtc1  $at, $f2
/* 093758 7F060D68 C7AA004C */  lwc1  $f10, 0x4c($sp)
/* 09375C 7F060D6C C5D00FF8 */  lwc1  $f16, 0xff8($t6)
/* 093760 7F060D70 8FAF00F8 */  lw    $t7, 0xf8($sp)
/* 093764 7F060D74 46025102 */  mul.s $f4, $f10, $f2
/* 093768 7F060D78 46008181 */  sub.s $f6, $f16, $f0
/* 09376C 7F060D7C C5F20014 */  lwc1  $f18, 0x14($t7)
/* 093770 7F060D80 C7AA0048 */  lwc1  $f10, 0x48($sp)
/* 093774 7F060D84 46043201 */  sub.s $f8, $f6, $f4
/* 093778 7F060D88 46124402 */  mul.s $f16, $f8, $f18
/* 09377C 7F060D8C C7A80198 */  lwc1  $f8, 0x198($sp)
/* 093780 7F060D90 46025182 */  mul.s $f6, $f10, $f2
/* 093784 7F060D94 46068103 */  div.s $f4, $f16, $f6
/* 093788 7F060D98 46044481 */  sub.s $f18, $f8, $f4
/* 09378C 7F060D9C 1000001A */  b     .L7F060E08
/* 093790 7F060DA0 E7B20198 */   swc1  $f18, 0x198($sp)
.L7F060DA4:
/* 093794 7F060DA4 0FC1E14D */  jal   getPlayer_c_screenheight
/* 093798 7F060DA8 00000000 */   nop
/* 09379C 7F060DAC 0FC1E14D */  jal   getPlayer_c_screenheight
/* 0937A0 7F060DB0 E7A00048 */   swc1  $f0, 0x48($sp)
/* 0937A4 7F060DB4 0FC1E155 */  jal   getPlayer_c_screentop
/* 0937A8 7F060DB8 E7A0004C */   swc1  $f0, 0x4c($sp)
/* 0937AC 7F060DBC 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 0937B0 7F060DC0 3C188007 */  lui   $t8, %hi(g_CurrentPlayer) # $t8, 0x8007
/* 0937B4 7F060DC4 8F188BC0 */  lw    $t8, %lo(g_CurrentPlayer)($t8)
/* 0937B8 7F060DC8 44818000 */  mtc1  $at, $f16
/* 0937BC 7F060DCC C7AA004C */  lwc1  $f10, 0x4c($sp)
/* 0937C0 7F060DD0 C7080FF8 */  lwc1  $f8, 0xff8($t8)
/* 0937C4 7F060DD4 8FB900F8 */  lw    $t9, 0xf8($sp)
/* 0937C8 7F060DD8 46105182 */  mul.s $f6, $f10, $f16
/* 0937CC 7F060DDC 46004101 */  sub.s $f4, $f8, $f0
/* 0937D0 7F060DE0 C72A0010 */  lwc1  $f10, 0x10($t9)
/* 0937D4 7F060DE4 C7A80048 */  lwc1  $f8, 0x48($sp)
/* 0937D8 7F060DE8 46062481 */  sub.s $f18, $f4, $f6
/* 0937DC 7F060DEC 44812000 */  mtc1  $at, $f4
/* 0937E0 7F060DF0 460A9402 */  mul.s $f16, $f18, $f10
/* 0937E4 7F060DF4 C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 0937E8 7F060DF8 46044182 */  mul.s $f6, $f8, $f4
/* 0937EC 7F060DFC 46068483 */  div.s $f18, $f16, $f6
/* 0937F0 7F060E00 46125201 */  sub.s $f8, $f10, $f18
/* 0937F4 7F060E04 E7A80198 */  swc1  $f8, 0x198($sp)
.L7F060E08:
/* 0937F8 7F060E08 0FC172B1 */  jal   sub_GAME_7F05C614
/* 0937FC 7F060E0C 00000000 */   nop
/* 093800 7F060E10 0FC1611E */  jal   matrix_4x4_set_identity
/* 093804 7F060E14 27A40154 */   addiu $a0, $sp, 0x154
/* 093808 7F060E18 8FA200FC */  lw    $v0, 0xfc($sp)
/* 09380C 7F060E1C 2401001E */  li    $at, 30
/* 093810 7F060E20 10410002 */  beq   $v0, $at, .L7F060E2C
/* 093814 7F060E24 24010017 */   li    $at, 23
/* 093818 7F060E28 14410010 */  bne   $v0, $at, .L7F060E6C
.L7F060E2C:
/* 09381C 7F060E2C 3C0D8003 */   lui   $t5, %hi(D_80035C70) # $t5, 0x8003
/* 093820 7F060E30 25AD11C0 */  addiu $t5, %lo(D_80035C70) # addiu $t5, $t5, 0x11c0
/* 093824 7F060E34 8DA10000 */  lw    $at, ($t5)
/* 093828 7F060E38 27A400B8 */  addiu $a0, $sp, 0xb8
/* 09382C 7F060E3C 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 093830 7F060E40 AC810000 */  sw    $at, ($a0)
/* 093834 7F060E44 8DAF0004 */  lw    $t7, 4($t5)
/* 093838 7F060E48 AC8F0004 */  sw    $t7, 4($a0)
/* 09383C 7F060E4C 8DA10008 */  lw    $at, 8($t5)
/* 093840 7F060E50 0FC162EF */  jal   matrix_4x4_set_rotation_around_xyz
/* 093844 7F060E54 AC810008 */   sw    $at, 8($a0)
/* 093848 7F060E58 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 09384C 7F060E5C 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 093850 7F060E60 27A50154 */   addiu $a1, $sp, 0x154
/* 093854 7F060E64 10000039 */  b     .L7F060F4C
/* 093858 7F060E68 8E0D00BC */   lw    $t5, 0xbc($s0)
.L7F060E6C:
/* 09385C 7F060E6C 2401001F */  li    $at, 31
/* 093860 7F060E70 14410010 */  bne   $v0, $at, .L7F060EB4
/* 093864 7F060E74 3C188003 */   lui   $t8, %hi(D_80035C7C) # $t8, 0x8003
/* 093868 7F060E78 271811CC */  addiu $t8, %lo(D_80035C7C) # addiu $t8, $t8, 0x11cc
/* 09386C 7F060E7C 8F010000 */  lw    $at, ($t8)
/* 093870 7F060E80 27A400AC */  addiu $a0, $sp, 0xac
/* 093874 7F060E84 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 093878 7F060E88 AC810000 */  sw    $at, ($a0)
/* 09387C 7F060E8C 8F0E0004 */  lw    $t6, 4($t8)
/* 093880 7F060E90 AC8E0004 */  sw    $t6, 4($a0)
/* 093884 7F060E94 8F010008 */  lw    $at, 8($t8)
/* 093888 7F060E98 0FC162EF */  jal   matrix_4x4_set_rotation_around_xyz
/* 09388C 7F060E9C AC810008 */   sw    $at, 8($a0)
/* 093890 7F060EA0 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 093894 7F060EA4 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 093898 7F060EA8 27A50154 */   addiu $a1, $sp, 0x154
/* 09389C 7F060EAC 10000027 */  b     .L7F060F4C
/* 0938A0 7F060EB0 8E0D00BC */   lw    $t5, 0xbc($s0)
.L7F060EB4:
/* 0938A4 7F060EB4 24010001 */  li    $at, 1
/* 0938A8 7F060EB8 14410023 */  bne   $v0, $at, .L7F060F48
/* 0938AC 7F060EBC 3C0D8007 */   lui   $t5, %hi(g_CurrentPlayer) # $t5, 0x8007
/* 0938B0 7F060EC0 8DAD8BC0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 0938B4 7F060EC4 24010011 */  li    $at, 17
/* 0938B8 7F060EC8 3C198003 */  lui   $t9, %hi(D_80035C88) # $t9, 0x8003
/* 0938BC 7F060ECC 8DAF2A30 */  lw    $t7, 0x2a30($t5)
/* 0938C0 7F060ED0 273911D8 */  addiu $t9, %lo(D_80035C88) # addiu $t9, $t9, 0x11d8
/* 0938C4 7F060ED4 55E1001D */  bnel  $t7, $at, .L7F060F4C
/* 0938C8 7F060ED8 8E0D00BC */   lw    $t5, 0xbc($s0)
/* 0938CC 7F060EDC 8F210000 */  lw    $at, ($t9)
/* 0938D0 7F060EE0 27A400A0 */  addiu $a0, $sp, 0xa0
/* 0938D4 7F060EE4 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 0938D8 7F060EE8 AC810000 */  sw    $at, ($a0)
/* 0938DC 7F060EEC 8F2E0004 */  lw    $t6, 4($t9)
/* 0938E0 7F060EF0 AC8E0004 */  sw    $t6, 4($a0)
/* 0938E4 7F060EF4 8F210008 */  lw    $at, 8($t9)
/* 0938E8 7F060EF8 0FC162EF */  jal   matrix_4x4_set_rotation_around_xyz
/* 0938EC 7F060EFC AC810008 */   sw    $at, 8($a0)
/* 0938F0 7F060F00 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 0938F4 7F060F04 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 0938F8 7F060F08 27A50154 */   addiu $a1, $sp, 0x154
/* 0938FC 7F060F0C 3C01C020 */  li    $at, 0xC0200000 # -2.500000
/* 093900 7F060F10 44818000 */  mtc1  $at, $f16
/* 093904 7F060F14 C7A40194 */  lwc1  $f4, 0x194($sp)
/* 093908 7F060F18 3C018005 */  lui   $at, %hi(D_80053E00) # $at, 0x8005
/* 09390C 7F060F1C C4329F40 */  lwc1  $f18, %lo(D_80053E00)($at)
/* 093910 7F060F20 46102180 */  add.s $f6, $f4, $f16
/* 093914 7F060F24 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 093918 7F060F28 C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 09391C 7F060F2C 44818000 */  mtc1  $at, $f16
/* 093920 7F060F30 C7A4019C */  lwc1  $f4, 0x19c($sp)
/* 093924 7F060F34 E7A60194 */  swc1  $f6, 0x194($sp)
/* 093928 7F060F38 46125200 */  add.s $f8, $f10, $f18
/* 09392C 7F060F3C 46102180 */  add.s $f6, $f4, $f16
/* 093930 7F060F40 E7A80198 */  swc1  $f8, 0x198($sp)
/* 093934 7F060F44 E7A6019C */  swc1  $f6, 0x19c($sp)
.L7F060F48:
/* 093938 7F060F48 8E0D00BC */  lw    $t5, 0xbc($s0)
.L7F060F4C:
/* 09393C 7F060F4C 51A00017 */  beql  $t5, $zero, .L7F060FAC
/* 093940 7F060F50 44802000 */   mtc1  $zero, $f4
/* 093944 7F060F54 C7AA0194 */  lwc1  $f10, 0x194($sp)
/* 093948 7F060F58 C61200AC */  lwc1  $f18, 0xac($s0)
/* 09394C 7F060F5C C7A40198 */  lwc1  $f4, 0x198($sp)
/* 093950 7F060F60 2604007C */  addiu $a0, $s0, 0x7c
/* 093954 7F060F64 46125200 */  add.s $f8, $f10, $f18
/* 093958 7F060F68 C7AA019C */  lwc1  $f10, 0x19c($sp)
/* 09395C 7F060F6C 27A50154 */  addiu $a1, $sp, 0x154
/* 093960 7F060F70 E7A80194 */  swc1  $f8, 0x194($sp)
/* 093964 7F060F74 C61000B0 */  lwc1  $f16, 0xb0($s0)
/* 093968 7F060F78 46102180 */  add.s $f6, $f4, $f16
/* 09396C 7F060F7C E7A60198 */  swc1  $f6, 0x198($sp)
/* 093970 7F060F80 C61200B4 */  lwc1  $f18, 0xb4($s0)
/* 093974 7F060F84 46125200 */  add.s $f8, $f10, $f18
/* 093978 7F060F88 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 09397C 7F060F8C E7A8019C */   swc1  $f8, 0x19c($sp)
/* 093980 7F060F90 44800000 */  mtc1  $zero, $f0
/* 093984 7F060F94 00000000 */  nop
/* 093988 7F060F98 E7A00184 */  swc1  $f0, 0x184($sp)
/* 09398C 7F060F9C E7A00188 */  swc1  $f0, 0x188($sp)
/* 093990 7F060FA0 1000000A */  b     .L7F060FCC
/* 093994 7F060FA4 E7A0018C */   swc1  $f0, 0x18c($sp)
/* 093998 7F060FA8 44802000 */  mtc1  $zero, $f4
.L7F060FAC:
/* 09399C 7F060FAC 44808000 */  mtc1  $zero, $f16
/* 0939A0 7F060FB0 44803000 */  mtc1  $zero, $f6
/* 0939A4 7F060FB4 44805000 */  mtc1  $zero, $f10
/* 0939A8 7F060FB8 44800000 */  mtc1  $zero, $f0
/* 0939AC 7F060FBC E6040078 */  swc1  $f4, 0x78($s0)
/* 0939B0 7F060FC0 E610006C */  swc1  $f16, 0x6c($s0)
/* 0939B4 7F060FC4 E6060070 */  swc1  $f6, 0x70($s0)
/* 0939B8 7F060FC8 E60A0074 */  swc1  $f10, 0x74($s0)
.L7F060FCC:
/* 0939BC 7F060FCC C61200CC */  lwc1  $f18, 0xcc($s0)
/* 0939C0 7F060FD0 44050000 */  mfc1  $a1, $f0
/* 0939C4 7F060FD4 44060000 */  mfc1  $a2, $f0
/* 0939C8 7F060FD8 E7B20010 */  swc1  $f18, 0x10($sp)
/* 0939CC 7F060FDC C60800D0 */  lwc1  $f8, 0xd0($s0)
/* 0939D0 7F060FE0 44070000 */  mfc1  $a3, $f0
/* 0939D4 7F060FE4 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 0939D8 7F060FE8 E7A80014 */  swc1  $f8, 0x14($sp)
/* 0939DC 7F060FEC C60400D4 */  lwc1  $f4, 0xd4($s0)
/* 0939E0 7F060FF0 E7A40018 */  swc1  $f4, 0x18($sp)
/* 0939E4 7F060FF4 C61000D8 */  lwc1  $f16, 0xd8($s0)
/* 0939E8 7F060FF8 E7B0001C */  swc1  $f16, 0x1c($sp)
/* 0939EC 7F060FFC C60600DC */  lwc1  $f6, 0xdc($s0)
/* 0939F0 7F061000 E7A60020 */  swc1  $f6, 0x20($sp)
/* 0939F4 7F061004 C60A00E0 */  lwc1  $f10, 0xe0($s0)
/* 0939F8 7F061008 0FC1676C */  jal   matrix_4x4_set_basis_and_position_target
/* 0939FC 7F06100C E7AA0024 */   swc1  $f10, 0x24($sp)
/* 093A00 7F061010 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 093A04 7F061014 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 093A08 7F061018 27A50154 */   addiu $a1, $sp, 0x154
/* 093A0C 7F06101C C7B20194 */  lwc1  $f18, 0x194($sp)
/* 093A10 7F061020 C60801C8 */  lwc1  $f8, 0x1c8($s0)
/* 093A14 7F061024 C7B00198 */  lwc1  $f16, 0x198($sp)
/* 093A18 7F061028 C60601CC */  lwc1  $f6, 0x1cc($s0)
/* 093A1C 7F06102C 46089101 */  sub.s $f4, $f18, $f8
/* 093A20 7F061030 C60801D0 */  lwc1  $f8, 0x1d0($s0)
/* 093A24 7F061034 C7B2019C */  lwc1  $f18, 0x19c($sp)
/* 093A28 7F061038 46068281 */  sub.s $f10, $f16, $f6
/* 093A2C 7F06103C 44062000 */  mfc1  $a2, $f4
/* 093A30 7F061040 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 093A34 7F061044 46089101 */  sub.s $f4, $f18, $f8
/* 093A38 7F061048 44075000 */  mfc1  $a3, $f10
/* 093A3C 7F06104C 24050000 */  li    $a1, 0
/* 093A40 7F061050 0FC16864 */  jal   matrix_4x4_align
/* 093A44 7F061054 E7A40010 */   swc1  $f4, 0x10($sp)
/* 093A48 7F061058 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 093A4C 7F06105C 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 093A50 7F061060 27A50154 */   addiu $a1, $sp, 0x154
/* 093A54 7F061064 27A40154 */  addiu $a0, $sp, 0x154
/* 093A58 7F061068 0FC16132 */  jal   matrix_4x4_copy
/* 093A5C 7F06106C 27A50264 */   addiu $a1, $sp, 0x264
/* 093A60 7F061070 27A40194 */  addiu $a0, $sp, 0x194
/* 093A64 7F061074 0FC16390 */  jal   matrix_4x4_set_position
/* 093A68 7F061078 27A50264 */   addiu $a1, $sp, 0x264
/* 093A6C 7F06107C 26050228 */  addiu $a1, $s0, 0x228
/* 093A70 7F061080 AFA50044 */  sw    $a1, 0x44($sp)
/* 093A74 7F061084 0FC16132 */  jal   matrix_4x4_copy
/* 093A78 7F061088 27A40264 */   addiu $a0, $sp, 0x264
/* 093A7C 7F06108C 26040268 */  addiu $a0, $s0, 0x268
/* 093A80 7F061090 AFA40040 */  sw    $a0, 0x40($sp)
/* 093A84 7F061094 0FC16132 */  jal   matrix_4x4_copy
/* 093A88 7F061098 260502A8 */   addiu $a1, $s0, 0x2a8
/* 093A8C 7F06109C 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 093A90 7F0610A0 00000000 */   nop
/* 093A94 7F0610A4 00402025 */  move  $a0, $v0
/* 093A98 7F0610A8 8FA50044 */  lw    $a1, 0x44($sp)
/* 093A9C 7F0610AC 0FC1618D */  jal   matrix_4x4_multiply_homogeneous
/* 093AA0 7F0610B0 8FA60040 */   lw    $a2, 0x40($sp)
/* 093AA4 7F0610B4 240F0001 */  li    $t7, 1
/* 093AA8 7F0610B8 A20F000F */  sb    $t7, 0xf($s0)
/* 093AAC 7F0610BC 0FC17540 */  jal   get_ptr_weapon_model_header_line
/* 093AB0 7F0610C0 8FA400FC */   lw    $a0, 0xfc($sp)
/* 093AB4 7F0610C4 10400017 */  beqz  $v0, .L7F061124
/* 093AB8 7F0610C8 8FA400FC */   lw    $a0, 0xfc($sp)
/* 093ABC 7F0610CC 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093AC0 7F0610D0 24050800 */   li    $a1, 2048
/* 093AC4 7F0610D4 10400013 */  beqz  $v0, .L7F061124
/* 093AC8 7F0610D8 8FA400FC */   lw    $a0, 0xfc($sp)
/* 093ACC 7F0610DC 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093AD0 7F0610E0 24052000 */   li    $a1, 8192
/* 093AD4 7F0610E4 54400010 */  bnezl $v0, .L7F061128
/* 093AD8 7F0610E8 A200000F */   sb    $zero, 0xf($s0)
/* 093ADC 7F0610EC 8E020024 */  lw    $v0, 0x24($s0)
/* 093AE0 7F0610F0 24010006 */  li    $at, 6
/* 093AE4 7F0610F4 1041000B */  beq   $v0, $at, .L7F061124
/* 093AE8 7F0610F8 24010007 */   li    $at, 7
/* 093AEC 7F0610FC 5041000A */  beql  $v0, $at, .L7F061128
/* 093AF0 7F061100 A200000F */   sb    $zero, 0xf($s0)
/* 093AF4 7F061104 0FC174DB */  jal   Gun_hand_without_item
/* 093AF8 7F061108 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 093AFC 7F06110C 50400006 */  beql  $v0, $zero, .L7F061128
/* 093B00 7F061110 A200000F */   sb    $zero, 0xf($s0)
/* 093B04 7F061114 0FC174EC */  jal   get_itemtype_in_hand
/* 093B08 7F061118 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 093B0C 7F06111C 54400003 */  bnezl $v0, .L7F06112C
/* 093B10 7F061120 8E18002C */   lw    $t8, 0x2c($s0)
.L7F061124:
/* 093B14 7F061124 A200000F */  sb    $zero, 0xf($s0)
.L7F061128:
/* 093B18 7F061128 8E18002C */  lw    $t8, 0x2c($s0)
.L7F06112C:
/* 093B1C 7F06112C 8FA400FC */  lw    $a0, 0xfc($sp)
/* 093B20 7F061130 5F000007 */  bgtzl $t8, .L7F061150
/* 093B24 7F061134 8219000F */   lb    $t9, 0xf($s0)
/* 093B28 7F061138 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093B2C 7F06113C 24050002 */   li    $a1, 2
/* 093B30 7F061140 50400003 */  beql  $v0, $zero, .L7F061150
/* 093B34 7F061144 8219000F */   lb    $t9, 0xf($s0)
/* 093B38 7F061148 A200000F */  sb    $zero, 0xf($s0)
/* 093B3C 7F06114C 8219000F */  lb    $t9, 0xf($s0)
.L7F061150:
/* 093B40 7F061150 8FAD02A8 */  lw    $t5, 0x2a8($sp)
/* 093B44 7F061154 3C0E8007 */  lui   $t6, %hi(g_CurrentPlayer) # $t6, 0x8007
/* 093B48 7F061158 132002CF */  beqz  $t9, .L7F061C98
/* 093B4C 7F06115C 000D78C0 */   sll   $t7, $t5, 3
/* 093B50 7F061160 8DCE8BC0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 093B54 7F061164 01ED7823 */  subu  $t7, $t7, $t5
/* 093B58 7F061168 000F7880 */  sll   $t7, $t7, 2
/* 093B5C 7F06116C 01CF1021 */  addu  $v0, $t6, $t7
/* 093B60 7F061170 8444081E */  lh    $a0, 0x81e($v0)
/* 093B64 7F061174 24420810 */  addiu $v0, $v0, 0x810
/* 093B68 7F061178 AFA201A0 */  sw    $v0, 0x1a0($sp)
/* 093B6C 7F06117C 0004C180 */  sll   $t8, $a0, 6
/* 093B70 7F061180 03002025 */  move  $a0, $t8
/* 093B74 7F061184 AFA00100 */  sw    $zero, 0x100($sp)
/* 093B78 7F061188 0FC2F2B1 */  jal   dynAllocate
/* 093B7C 7F06118C 00001825 */   move  $v1, $zero
/* 093B80 7F061190 8FB901A0 */  lw    $t9, 0x1a0($sp)
/* 093B84 7F061194 AFA202A4 */  sw    $v0, 0x2a4($sp)
/* 093B88 7F061198 8FA30100 */  lw    $v1, 0x100($sp)
/* 093B8C 7F06119C 872D000E */  lh    $t5, 0xe($t9)
/* 093B90 7F0611A0 19A0000D */  blez  $t5, .L7F0611D8
/* 093B94 7F0611A4 00402025 */   move  $a0, $v0
/* 093B98 7F0611A8 AFA30100 */  sw    $v1, 0x100($sp)
.L7F0611AC:
/* 093B9C 7F0611AC 0FC1611E */  jal   matrix_4x4_set_identity
/* 093BA0 7F0611B0 AFA40044 */   sw    $a0, 0x44($sp)
/* 093BA4 7F0611B4 8FAE01A0 */  lw    $t6, 0x1a0($sp)
/* 093BA8 7F0611B8 8FA30100 */  lw    $v1, 0x100($sp)
/* 093BAC 7F0611BC 8FA40044 */  lw    $a0, 0x44($sp)
/* 093BB0 7F0611C0 85CF000E */  lh    $t7, 0xe($t6)
/* 093BB4 7F0611C4 24630001 */  addiu $v1, $v1, 1
/* 093BB8 7F0611C8 24840040 */  addiu $a0, $a0, 0x40
/* 093BBC 7F0611CC 006F082A */  slt   $at, $v1, $t7
/* 093BC0 7F0611D0 5420FFF6 */  bnezl $at, .L7F0611AC
/* 093BC4 7F0611D4 AFA30100 */   sw    $v1, 0x100($sp)
.L7F0611D8:
/* 093BC8 7F0611D8 0FC1D75F */  jal   modelCalculateRwDataLen
/* 093BCC 7F0611DC 8FA401A0 */   lw    $a0, 0x1a0($sp)
/* 093BD0 7F0611E0 260402F8 */  addiu $a0, $s0, 0x2f8
/* 093BD4 7F0611E4 8FA501A0 */  lw    $a1, 0x1a0($sp)
/* 093BD8 7F0611E8 AFA40044 */  sw    $a0, 0x44($sp)
/* 093BDC 7F0611EC 0FC1D7F9 */  jal   modelInit
/* 093BE0 7F0611F0 26060318 */   addiu $a2, $s0, 0x318
/* 093BE4 7F0611F4 8FA40044 */  lw    $a0, 0x44($sp)
/* 093BE8 7F0611F8 0FC17B8C */  jal   sub_GAME_7F05E978
/* 093BEC 7F0611FC 24050001 */   li    $a1, 1
/* 093BF0 7F061200 8FA40044 */  lw    $a0, 0x44($sp)
/* 093BF4 7F061204 0FC17BD3 */  jal   sub_GAME_7F05EA94
/* 093BF8 7F061208 8205000E */   lb    $a1, 0xe($s0)
/* 093BFC 7F06120C 8FB801A0 */  lw    $t8, 0x1a0($sp)
/* 093C00 7F061210 8F020008 */  lw    $v0, 8($t8)
/* 093C04 7F061214 8C440004 */  lw    $a0, 4($v0)
/* 093C08 7F061218 50800008 */  beql  $a0, $zero, .L7F06123C
/* 093C0C 7F06121C 8C43000C */   lw    $v1, 0xc($v0)
/* 093C10 7F061220 8C830004 */  lw    $v1, 4($a0)
/* 093C14 7F061224 94790004 */  lhu   $t9, 4($v1)
/* 093C18 7F061228 00196880 */  sll   $t5, $t9, 2
/* 093C1C 7F06122C 020D7021 */  addu  $t6, $s0, $t5
/* 093C20 7F061230 25CF0318 */  addiu $t7, $t6, 0x318
/* 093C24 7F061234 AFAF010C */  sw    $t7, 0x10c($sp)
/* 093C28 7F061238 8C43000C */  lw    $v1, 0xc($v0)
.L7F06123C:
/* 093C2C 7F06123C 50600004 */  beql  $v1, $zero, .L7F061250
/* 093C30 7F061240 8FB902A4 */   lw    $t9, 0x2a4($sp)
/* 093C34 7F061244 8C780004 */  lw    $t8, 4($v1)
/* 093C38 7F061248 AFB80108 */  sw    $t8, 0x108($sp)
/* 093C3C 7F06124C 8FB902A4 */  lw    $t9, 0x2a4($sp)
.L7F061250:
/* 093C40 7F061250 24050400 */  li    $a1, 1024
/* 093C44 7F061254 AE190304 */  sw    $t9, 0x304($s0)
/* 093C48 7F061258 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093C4C 7F06125C 8FA400FC */   lw    $a0, 0xfc($sp)
/* 093C50 7F061260 10400008 */  beqz  $v0, .L7F061284
/* 093C54 7F061264 00000000 */   nop
/* 093C58 7F061268 8FAD02A8 */  lw    $t5, 0x2a8($sp)
/* 093C5C 7F06126C 24010001 */  li    $at, 1
/* 093C60 7F061270 15A10004 */  bne   $t5, $at, .L7F061284
/* 093C64 7F061274 3C01BF80 */   li    $at, 0xBF800000 # -1.000000
/* 093C68 7F061278 44816000 */  mtc1  $at, $f12
/* 093C6C 7F06127C 0FC16397 */  jal   matrix_column_1_scalar_multiply
/* 093C70 7F061280 27A50264 */   addiu $a1, $sp, 0x264
.L7F061284:
/* 093C74 7F061284 3C018005 */  lui   $at, %hi(D_80053E04) # $at, 0x8005
/* 093C78 7F061288 C42C9F44 */  lwc1  $f12, %lo(D_80053E04)($at)
/* 093C7C 7F06128C 0FC163C9 */  jal   matrix_scalar_multiply
/* 093C80 7F061290 27A50264 */   addiu $a1, $sp, 0x264
/* 093C84 7F061294 27A40264 */  addiu $a0, $sp, 0x264
/* 093C88 7F061298 0FC16132 */  jal   matrix_4x4_copy
/* 093C8C 7F06129C 8FA502A4 */   lw    $a1, 0x2a4($sp)
/* 093C90 7F0612A0 8FAF01A0 */  lw    $t7, 0x1a0($sp)
/* 093C94 7F0612A4 3C0E8003 */  lui   $t6, %hi(skeleton_gun_revolver) # $t6, 0x8003
/* 093C98 7F0612A8 25CE666C */  addiu $t6, %lo(skeleton_gun_revolver) # addiu $t6, $t6, 0x666c
/* 093C9C 7F0612AC 8DF80004 */  lw    $t8, 4($t7)
/* 093CA0 7F0612B0 55D80077 */  bnel  $t6, $t8, .L7F061490
/* 093CA4 7F0612B4 8FA2010C */   lw    $v0, 0x10c($sp)
/* 093CA8 7F0612B8 8DE20008 */  lw    $v0, 8($t7)
/* 093CAC 7F0612BC 8FB900FC */  lw    $t9, 0xfc($sp)
/* 093CB0 7F0612C0 24010012 */  li    $at, 18
/* 093CB4 7F0612C4 8C430010 */  lw    $v1, 0x10($v0)
/* 093CB8 7F0612C8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 093CBC 7F0612CC 50600040 */  beql  $v1, $zero, .L7F0613D0
/* 093CC0 7F0612D0 8C430014 */   lw    $v1, 0x14($v0)
/* 093CC4 7F0612D4 44806000 */  mtc1  $zero, $f12
/* 093CC8 7F0612D8 17210020 */  bne   $t9, $at, .L7F06135C
/* 093CCC 7F0612DC 8C640004 */   lw    $a0, 4($v1)
/* 093CD0 7F0612E0 8E0D0024 */  lw    $t5, 0x24($s0)
/* 093CD4 7F0612E4 24010001 */  li    $at, 1
/* 093CD8 7F0612E8 55A10011 */  bnel  $t5, $at, .L7F061330
/* 093CDC 7F0612EC 8E18002C */   lw    $t8, 0x2c($s0)
/* 093CE0 7F0612F0 8E18002C */  lw    $t8, 0x2c($s0)
/* 093CE4 7F0612F4 8E0E0020 */  lw    $t6, 0x20($s0)
/* 093CE8 7F0612F8 3C018005 */  lui   $at, %hi(D_80053E08) # $at, 0x8005
/* 093CEC 7F0612FC 00187880 */  sll   $t7, $t8, 2
/* 093CF0 7F061300 01F87821 */  addu  $t7, $t7, $t8
/* 093CF4 7F061304 01CFC823 */  subu  $t9, $t6, $t7
/* 093CF8 7F061308 272D0019 */  addiu $t5, $t9, 0x19
/* 093CFC 7F06130C 448D8000 */  mtc1  $t5, $f16
/* 093D00 7F061310 C42A9F48 */  lwc1  $f10, %lo(D_80053E08)($at)
/* 093D04 7F061314 3C0141F0 */  li    $at, 0x41F00000 # 30.000000
/* 093D08 7F061318 468081A0 */  cvt.s.w $f6, $f16
/* 093D0C 7F06131C 44814000 */  mtc1  $at, $f8
/* 093D10 7F061320 460A3482 */  mul.s $f18, $f6, $f10
/* 093D14 7F061324 1000001D */  b     .L7F06139C
/* 093D18 7F061328 46089303 */   div.s $f12, $f18, $f8
/* 093D1C 7F06132C 8E18002C */  lw    $t8, 0x2c($s0)
.L7F061330:
/* 093D20 7F061330 240E0006 */  li    $t6, 6
/* 093D24 7F061334 3C018005 */  lui   $at, %hi(D_80053E0C) # $at, 0x8005
/* 093D28 7F061338 01D87823 */  subu  $t7, $t6, $t8
/* 093D2C 7F06133C 448F2000 */  mtc1  $t7, $f4
/* 093D30 7F061340 C4269F4C */  lwc1  $f6, %lo(D_80053E0C)($at)
/* 093D34 7F061344 3C0140C0 */  li    $at, 0x40C00000 # 6.000000
/* 093D38 7F061348 46802420 */  cvt.s.w $f16, $f4
/* 093D3C 7F06134C 44819000 */  mtc1  $at, $f18
/* 093D40 7F061350 46068282 */  mul.s $f10, $f16, $f6
/* 093D44 7F061354 10000011 */  b     .L7F06139C
/* 093D48 7F061358 46125303 */   div.s $f12, $f10, $f18
.L7F06135C:
/* 093D4C 7F06135C 8E190024 */  lw    $t9, 0x24($s0)
/* 093D50 7F061360 24010001 */  li    $at, 1
/* 093D54 7F061364 1721000D */  bne   $t9, $at, .L7F06139C
/* 093D58 7F061368 00000000 */   nop
/* 093D5C 7F06136C 8E020020 */  lw    $v0, 0x20($s0)
/* 093D60 7F061370 28410005 */  slti  $at, $v0, 5
/* 093D64 7F061374 10200009 */  beqz  $at, .L7F06139C
/* 093D68 7F061378 00000000 */   nop
/* 093D6C 7F06137C 44824000 */  mtc1  $v0, $f8
/* 093D70 7F061380 3C018005 */  lui   $at, %hi(D_80053E10) # $at, 0x8005
/* 093D74 7F061384 C4309F50 */  lwc1  $f16, %lo(D_80053E10)($at)
/* 093D78 7F061388 46804120 */  cvt.s.w $f4, $f8
/* 093D7C 7F06138C 3C0141F0 */  li    $at, 0x41F00000 # 30.000000
/* 093D80 7F061390 44815000 */  mtc1  $at, $f10
/* 093D84 7F061394 46102182 */  mul.s $f6, $f4, $f16
/* 093D88 7F061398 460A3303 */  div.s $f12, $f6, $f10
.L7F06139C:
/* 093D8C 7F06139C 0FC162CC */  jal   matrix_4x4_set_rotation_around_z
/* 093D90 7F0613A0 AFA4009C */   sw    $a0, 0x9c($sp)
/* 093D94 7F0613A4 8FA4009C */  lw    $a0, 0x9c($sp)
/* 093D98 7F0613A8 0FC16390 */  jal   matrix_4x4_set_position
/* 093D9C 7F0613AC 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 093DA0 7F0613B0 8FA602A4 */  lw    $a2, 0x2a4($sp)
/* 093DA4 7F0613B4 27A40264 */  addiu $a0, $sp, 0x264
/* 093DA8 7F0613B8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 093DAC 7F0613BC 0FC1615C */  jal   matrix_4x4_multiply
/* 093DB0 7F0613C0 24C600C0 */   addiu $a2, $a2, 0xc0
/* 093DB4 7F0613C4 8FAD01A0 */  lw    $t5, 0x1a0($sp)
/* 093DB8 7F0613C8 8DA20008 */  lw    $v0, 8($t5)
/* 093DBC 7F0613CC 8C430014 */  lw    $v1, 0x14($v0)
.L7F0613D0:
/* 093DC0 7F0613D0 5060002F */  beql  $v1, $zero, .L7F061490
/* 093DC4 7F0613D4 8FA2010C */   lw    $v0, 0x10c($sp)
/* 093DC8 7F0613D8 8E0E0024 */  lw    $t6, 0x24($s0)
/* 093DCC 7F0613DC 24010001 */  li    $at, 1
/* 093DD0 7F0613E0 8C640004 */  lw    $a0, 4($v1)
/* 093DD4 7F0613E4 15C10022 */  bne   $t6, $at, .L7F061470
/* 093DD8 7F0613E8 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 093DDC 7F0613EC 8E020020 */  lw    $v0, 0x20($s0)
/* 093DE0 7F0613F0 24180005 */  li    $t8, 5
/* 093DE4 7F0613F4 28410002 */  slti  $at, $v0, 2
/* 093DE8 7F0613F8 1020000C */  beqz  $at, .L7F06142C
/* 093DEC 7F0613FC 03027823 */   subu  $t7, $t8, $v0
/* 093DF0 7F061400 44829000 */  mtc1  $v0, $f18
/* 093DF4 7F061404 3C018005 */  lui   $at, %hi(D_80053E14) # $at, 0x8005
/* 093DF8 7F061408 C4309F54 */  lwc1  $f16, %lo(D_80053E14)($at)
/* 093DFC 7F06140C 46809220 */  cvt.s.w $f8, $f18
/* 093E00 7F061410 3C0140A0 */  li    $at, 0x40A00000 # 5.000000
/* 093E04 7F061414 44815000 */  mtc1  $at, $f10
/* 093E08 7F061418 46004107 */  neg.s $f4, $f8
/* 093E0C 7F06141C 46102002 */  mul.s $f0, $f4, $f16
/* 093E10 7F061420 46000180 */  add.s $f6, $f0, $f0
/* 093E14 7F061424 1000000B */  b     .L7F061454
/* 093E18 7F061428 460A3303 */   div.s $f12, $f6, $f10
.L7F06142C:
/* 093E1C 7F06142C 448F9000 */  mtc1  $t7, $f18
/* 093E20 7F061430 3C018005 */  lui   $at, %hi(D_80053E18) # $at, 0x8005
/* 093E24 7F061434 C4309F58 */  lwc1  $f16, %lo(D_80053E18)($at)
/* 093E28 7F061438 46809220 */  cvt.s.w $f8, $f18
/* 093E2C 7F06143C 3C0140A0 */  li    $at, 0x40A00000 # 5.000000
/* 093E30 7F061440 44815000 */  mtc1  $at, $f10
/* 093E34 7F061444 46004107 */  neg.s $f4, $f8
/* 093E38 7F061448 46102002 */  mul.s $f0, $f4, $f16
/* 093E3C 7F06144C 46000180 */  add.s $f6, $f0, $f0
/* 093E40 7F061450 460A3303 */  div.s $f12, $f6, $f10
.L7F061454:
/* 093E44 7F061454 0FC16286 */  jal   matrix_4x4_set_rotation_around_x
/* 093E48 7F061458 AFA40094 */   sw    $a0, 0x94($sp)
/* 093E4C 7F06145C 8FA40094 */  lw    $a0, 0x94($sp)
/* 093E50 7F061460 0FC16390 */  jal   matrix_4x4_set_position
/* 093E54 7F061464 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 093E58 7F061468 10000004 */  b     .L7F06147C
/* 093E5C 7F06146C 8FA602A4 */   lw    $a2, 0x2a4($sp)
.L7F061470:
/* 093E60 7F061470 0FC16383 */  jal   matrix_4x4_set_identity_and_position
/* 093E64 7F061474 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 093E68 7F061478 8FA602A4 */  lw    $a2, 0x2a4($sp)
.L7F06147C:
/* 093E6C 7F06147C 27A40264 */  addiu $a0, $sp, 0x264
/* 093E70 7F061480 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 093E74 7F061484 0FC1615C */  jal   matrix_4x4_multiply
/* 093E78 7F061488 24C60100 */   addiu $a2, $a2, 0x100
/* 093E7C 7F06148C 8FA2010C */  lw    $v0, 0x10c($sp)
.L7F061490:
/* 093E80 7F061490 50400003 */  beql  $v0, $zero, .L7F0614A0
/* 093E84 7F061494 8FB90108 */   lw    $t9, 0x108($sp)
/* 093E88 7F061498 AC400000 */  sw    $zero, ($v0)
/* 093E8C 7F06149C 8FB90108 */  lw    $t9, 0x108($sp)
.L7F0614A0:
/* 093E90 7F0614A0 53200142 */  beql  $t9, $zero, .L7F0619AC
/* 093E94 7F0614A4 C6100260 */   lwc1  $f16, 0x260($s0)
/* 093E98 7F0614A8 0C00262C */  jal   randomGetNext
/* 093E9C 7F0614AC 00000000 */   nop
/* 093EA0 7F0614B0 44829000 */  mtc1  $v0, $f18
/* 093EA4 7F0614B4 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 093EA8 7F0614B8 04410004 */  bgez  $v0, .L7F0614CC
/* 093EAC 7F0614BC 46809220 */   cvt.s.w $f8, $f18
/* 093EB0 7F0614C0 44812000 */  mtc1  $at, $f4
/* 093EB4 7F0614C4 00000000 */  nop
/* 093EB8 7F0614C8 46044200 */  add.s $f8, $f8, $f4
.L7F0614CC:
/* 093EBC 7F0614CC 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 093EC0 7F0614D0 44818000 */  mtc1  $at, $f16
/* 093EC4 7F0614D4 3C013E80 */  li    $at, 0x3E800000 # 0.250000
/* 093EC8 7F0614D8 44815000 */  mtc1  $at, $f10
/* 093ECC 7F0614DC 46104182 */  mul.s $f6, $f8, $f16
/* 093ED0 7F0614E0 3C013F80 */  li    $at, 0x3F800000 # 1.000000
/* 093ED4 7F0614E4 44812000 */  mtc1  $at, $f4
/* 093ED8 7F0614E8 8FAD00F8 */  lw    $t5, 0xf8($sp)
/* 093EDC 7F0614EC 8FA400FC */  lw    $a0, 0xfc($sp)
/* 093EE0 7F0614F0 24050001 */  li    $a1, 1
/* 093EE4 7F0614F4 460A3482 */  mul.s $f18, $f6, $f10
/* 093EE8 7F0614F8 46049200 */  add.s $f8, $f18, $f4
/* 093EEC 7F0614FC E7A80080 */  swc1  $f8, 0x80($sp)
/* 093EF0 7F061500 C5B00000 */  lwc1  $f16, ($t5)
/* 093EF4 7F061504 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 093EF8 7F061508 E7B0007C */   swc1  $f16, 0x7c($sp)
/* 093EFC 7F06150C 10400018 */  beqz  $v0, .L7F061570
/* 093F00 7F061510 8FA40108 */   lw    $a0, 0x108($sp)
/* 093F04 7F061514 0C00262C */  jal   randomGetNext
/* 093F08 7F061518 00000000 */   nop
/* 093F0C 7F06151C 44823000 */  mtc1  $v0, $f6
/* 093F10 7F061520 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 093F14 7F061524 04410004 */  bgez  $v0, .L7F061538
/* 093F18 7F061528 468032A0 */   cvt.s.w $f10, $f6
/* 093F1C 7F06152C 44819000 */  mtc1  $at, $f18
/* 093F20 7F061530 00000000 */  nop
/* 093F24 7F061534 46125280 */  add.s $f10, $f10, $f18
.L7F061538:
/* 093F28 7F061538 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 093F2C 7F06153C 44812000 */  mtc1  $at, $f4
/* 093F30 7F061540 3C018005 */  lui   $at, %hi(D_80053E1C) # $at, 0x8005
/* 093F34 7F061544 C4309F5C */  lwc1  $f16, %lo(D_80053E1C)($at)
/* 093F38 7F061548 46045202 */  mul.s $f8, $f10, $f4
/* 093F3C 7F06154C 27A50224 */  addiu $a1, $sp, 0x224
/* 093F40 7F061550 46104302 */  mul.s $f12, $f8, $f16
/* 093F44 7F061554 0FC162CC */  jal   matrix_4x4_set_rotation_around_z
/* 093F48 7F061558 00000000 */   nop
/* 093F4C 7F06155C 8FA40108 */  lw    $a0, 0x108($sp)
/* 093F50 7F061560 0FC16390 */  jal   matrix_4x4_set_position
/* 093F54 7F061564 27A50224 */   addiu $a1, $sp, 0x224
/* 093F58 7F061568 10000004 */  b     .L7F06157C
/* 093F5C 7F06156C C7AC0080 */   lwc1  $f12, 0x80($sp)
.L7F061570:
/* 093F60 7F061570 0FC16383 */  jal   matrix_4x4_set_identity_and_position
/* 093F64 7F061574 27A50224 */   addiu $a1, $sp, 0x224
/* 093F68 7F061578 C7AC0080 */  lwc1  $f12, 0x80($sp)
.L7F06157C:
/* 093F6C 7F06157C 0FC163C9 */  jal   matrix_scalar_multiply
/* 093F70 7F061580 27A50224 */   addiu $a1, $sp, 0x224
/* 093F74 7F061584 C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 093F78 7F061588 0FC163AF */  jal   matrix_column_3_scalar_multiply
/* 093F7C 7F06158C 27A50224 */   addiu $a1, $sp, 0x224
/* 093F80 7F061590 27A40264 */  addiu $a0, $sp, 0x264
/* 093F84 7F061594 0FC16144 */  jal   matrix_4x4_multiply_in_place
/* 093F88 7F061598 27A50224 */   addiu $a1, $sp, 0x224
/* 093F8C 7F06159C 8FA502A4 */  lw    $a1, 0x2a4($sp)
/* 093F90 7F0615A0 27A40224 */  addiu $a0, $sp, 0x224
/* 093F94 7F0615A4 0FC16132 */  jal   matrix_4x4_copy
/* 093F98 7F0615A8 24A50040 */   addiu $a1, $a1, 0x40
/* 093F9C 7F0615AC C7A60254 */  lwc1  $f6, 0x254($sp)
/* 093FA0 7F0615B0 E60602E8 */  swc1  $f6, 0x2e8($s0)
/* 093FA4 7F0615B4 C7B20258 */  lwc1  $f18, 0x258($sp)
/* 093FA8 7F0615B8 E61202EC */  swc1  $f18, 0x2ec($s0)
/* 093FAC 7F0615BC C7AA025C */  lwc1  $f10, 0x25c($sp)
/* 093FB0 7F0615C0 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 093FB4 7F0615C4 E60A02F0 */   swc1  $f10, 0x2f0($s0)
/* 093FB8 7F0615C8 00402025 */  move  $a0, $v0
/* 093FBC 7F0615CC 0FC16247 */  jal   mtx4TransformVecInPlace
/* 093FC0 7F0615D0 260502E8 */   addiu $a1, $s0, 0x2e8
/* 093FC4 7F0615D4 C7A4025C */  lwc1  $f4, 0x25c($sp)
/* 093FC8 7F0615D8 820E000D */  lb    $t6, 0xd($s0)
/* 093FCC 7F0615DC 46002207 */  neg.s $f8, $f4
/* 093FD0 7F0615E0 11C000EE */  beqz  $t6, .L7F06199C
/* 093FD4 7F0615E4 E60802F4 */   swc1  $f8, 0x2f4($s0)
/* 093FD8 7F0615E8 8FB8010C */  lw    $t8, 0x10c($sp)
/* 093FDC 7F0615EC 240F0001 */  li    $t7, 1
/* 093FE0 7F0615F0 53000003 */  beql  $t8, $zero, .L7F061600
/* 093FE4 7F0615F4 8FB901A0 */   lw    $t9, 0x1a0($sp)
/* 093FE8 7F0615F8 AF0F0000 */  sw    $t7, ($t8)
/* 093FEC 7F0615FC 8FB901A0 */  lw    $t9, 0x1a0($sp)
.L7F061600:
/* 093FF0 7F061600 8F2D0008 */  lw    $t5, 8($t9)
/* 093FF4 7F061604 8DA30008 */  lw    $v1, 8($t5)
/* 093FF8 7F061608 5060006D */  beql  $v1, $zero, .L7F0617C0
/* 093FFC 7F06160C 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 094000 7F061610 8C620004 */  lw    $v0, 4($v1)
/* 094004 7F061614 C7A60224 */  lwc1  $f6, 0x224($sp)
/* 094008 7F061618 C7A40234 */  lwc1  $f4, 0x234($sp)
/* 09400C 7F06161C C4500000 */  lwc1  $f16, ($v0)
/* 094010 7F061620 C44A0004 */  lwc1  $f10, 4($v0)
/* 094014 7F061624 46068482 */  mul.s $f18, $f16, $f6
/* 094018 7F061628 C4460008 */  lwc1  $f6, 8($v0)
/* 09401C 7F06162C 46045202 */  mul.s $f8, $f10, $f4
/* 094020 7F061630 C7AA0244 */  lwc1  $f10, 0x244($sp)
/* 094024 7F061634 460A3102 */  mul.s $f4, $f6, $f10
/* 094028 7F061638 46089400 */  add.s $f16, $f18, $f8
/* 09402C 7F06163C C7A80254 */  lwc1  $f8, 0x254($sp)
/* 094030 7F061640 46048480 */  add.s $f18, $f16, $f4
/* 094034 7F061644 C7B00228 */  lwc1  $f16, 0x228($sp)
/* 094038 7F061648 46124180 */  add.s $f6, $f8, $f18
/* 09403C 7F06164C C7B20238 */  lwc1  $f18, 0x238($sp)
/* 094040 7F061650 E7A60084 */  swc1  $f6, 0x84($sp)
/* 094044 7F061654 C44A0000 */  lwc1  $f10, ($v0)
/* 094048 7F061658 C4480004 */  lwc1  $f8, 4($v0)
/* 09404C 7F06165C 46105102 */  mul.s $f4, $f10, $f16
/* 094050 7F061660 C4500008 */  lwc1  $f16, 8($v0)
/* 094054 7F061664 46124182 */  mul.s $f6, $f8, $f18
/* 094058 7F061668 C7A80248 */  lwc1  $f8, 0x248($sp)
/* 09405C 7F06166C 46088482 */  mul.s $f18, $f16, $f8
/* 094060 7F061670 46062280 */  add.s $f10, $f4, $f6
/* 094064 7F061674 C7A60258 */  lwc1  $f6, 0x258($sp)
/* 094068 7F061678 46125100 */  add.s $f4, $f10, $f18
/* 09406C 7F06167C C7AA022C */  lwc1  $f10, 0x22c($sp)
/* 094070 7F061680 46043400 */  add.s $f16, $f6, $f4
/* 094074 7F061684 C7A4023C */  lwc1  $f4, 0x23c($sp)
/* 094078 7F061688 E7B00088 */  swc1  $f16, 0x88($sp)
/* 09407C 7F06168C C4480000 */  lwc1  $f8, ($v0)
/* 094080 7F061690 C4460004 */  lwc1  $f6, 4($v0)
/* 094084 7F061694 460A4482 */  mul.s $f18, $f8, $f10
/* 094088 7F061698 C44A0008 */  lwc1  $f10, 8($v0)
/* 09408C 7F06169C 46043402 */  mul.s $f16, $f6, $f4
/* 094090 7F0616A0 C7A6024C */  lwc1  $f6, 0x24c($sp)
/* 094094 7F0616A4 46065102 */  mul.s $f4, $f10, $f6
/* 094098 7F0616A8 46109200 */  add.s $f8, $f18, $f16
/* 09409C 7F0616AC C7B0025C */  lwc1  $f16, 0x25c($sp)
/* 0940A0 7F0616B0 46044480 */  add.s $f18, $f8, $f4
/* 0940A4 7F0616B4 46128280 */  add.s $f10, $f16, $f18
/* 0940A8 7F0616B8 0C00262C */  jal   randomGetNext
/* 0940AC 7F0616BC E7AA008C */   swc1  $f10, 0x8c($sp)
/* 0940B0 7F0616C0 44823000 */  mtc1  $v0, $f6
/* 0940B4 7F0616C4 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 0940B8 7F0616C8 04410005 */  bgez  $v0, .L7F0616E0
/* 0940BC 7F0616CC 46803220 */   cvt.s.w $f8, $f6
/* 0940C0 7F0616D0 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0940C4 7F0616D4 44812000 */  mtc1  $at, $f4
/* 0940C8 7F0616D8 00000000 */  nop
/* 0940CC 7F0616DC 46044200 */  add.s $f8, $f8, $f4
.L7F0616E0:
/* 0940D0 7F0616E0 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0940D4 7F0616E4 44818000 */  mtc1  $at, $f16
/* 0940D8 7F0616E8 3C018005 */  lui   $at, %hi(D_80053E20) # $at, 0x8005
/* 0940DC 7F0616EC C42A9F60 */  lwc1  $f10, %lo(D_80053E20)($at)
/* 0940E0 7F0616F0 46104482 */  mul.s $f18, $f8, $f16
/* 0940E4 7F0616F4 C7B00088 */  lwc1  $f16, 0x88($sp)
/* 0940E8 7F0616F8 C7A40084 */  lwc1  $f4, 0x84($sp)
/* 0940EC 7F0616FC 46002207 */  neg.s $f8, $f4
/* 0940F0 7F061700 460A9182 */  mul.s $f6, $f18, $f10
/* 0940F4 7F061704 C7AA008C */  lwc1  $f10, 0x8c($sp)
/* 0940F8 7F061708 46008487 */  neg.s $f18, $f16
/* 0940FC 7F06170C 44064000 */  mfc1  $a2, $f8
/* 094100 7F061710 44079000 */  mfc1  $a3, $f18
/* 094104 7F061714 44053000 */  mfc1  $a1, $f6
/* 094108 7F061718 46005187 */  neg.s $f6, $f10
/* 09410C 7F06171C 0FC16864 */  jal   matrix_4x4_align
/* 094110 7F061720 E7A60010 */   swc1  $f6, 0x10($sp)
/* 094114 7F061724 3C018005 */  lui   $at, %hi(D_80053E24) # $at, 0x8005
/* 094118 7F061728 C4249F64 */  lwc1  $f4, %lo(D_80053E24)($at)
/* 09411C 7F06172C C7A80080 */  lwc1  $f8, 0x80($sp)
/* 094120 7F061730 27A501E4 */  addiu $a1, $sp, 0x1e4
/* 094124 7F061734 46082302 */  mul.s $f12, $f4, $f8
/* 094128 7F061738 0FC163C9 */  jal   matrix_scalar_multiply
/* 09412C 7F06173C 00000000 */   nop
/* 094130 7F061740 C7B00194 */  lwc1  $f16, 0x194($sp)
/* 094134 7F061744 C61201C8 */  lwc1  $f18, 0x1c8($s0)
/* 094138 7F061748 C7A60198 */  lwc1  $f6, 0x198($sp)
/* 09413C 7F06174C C60401CC */  lwc1  $f4, 0x1cc($s0)
/* 094140 7F061750 46128281 */  sub.s $f10, $f16, $f18
/* 094144 7F061754 C61201D0 */  lwc1  $f18, 0x1d0($s0)
/* 094148 7F061758 C7B0019C */  lwc1  $f16, 0x19c($sp)
/* 09414C 7F06175C 46043201 */  sub.s $f8, $f6, $f4
/* 094150 7F061760 44065000 */  mfc1  $a2, $f10
/* 094154 7F061764 27A40114 */  addiu $a0, $sp, 0x114
/* 094158 7F061768 46128281 */  sub.s $f10, $f16, $f18
/* 09415C 7F06176C 44074000 */  mfc1  $a3, $f8
/* 094160 7F061770 24050000 */  li    $a1, 0
/* 094164 7F061774 0FC16800 */  jal   matrix_4x4_set_rotation_axis_angle
/* 094168 7F061778 E7AA0010 */   swc1  $f10, 0x10($sp)
/* 09416C 7F06177C 27A40114 */  addiu $a0, $sp, 0x114
/* 094170 7F061780 0FC16144 */  jal   matrix_4x4_multiply_in_place
/* 094174 7F061784 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094178 7F061788 C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 09417C 7F06178C 0FC1640A */  jal   matrix_row_3_scalar_multiply
/* 094180 7F061790 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094184 7F061794 27A40154 */  addiu $a0, $sp, 0x154
/* 094188 7F061798 0FC16144 */  jal   matrix_4x4_multiply_in_place
/* 09418C 7F06179C 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094190 7F0617A0 27A40084 */  addiu $a0, $sp, 0x84
/* 094194 7F0617A4 0FC16390 */  jal   matrix_4x4_set_position
/* 094198 7F0617A8 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 09419C 7F0617AC 8FA502A4 */  lw    $a1, 0x2a4($sp)
/* 0941A0 7F0617B0 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 0941A4 7F0617B4 0FC16132 */  jal   matrix_4x4_copy
/* 0941A8 7F0617B8 24A50080 */   addiu $a1, $a1, 0x80
/* 0941AC 7F0617BC 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.L7F0617C0:
/* 0941B0 7F0617C0 3C0E8003 */  lui   $t6, %hi(skeleton_gun_kf7) # $t6, 0x8003
/* 0941B4 7F0617C4 25CE66AC */  addiu $t6, %lo(skeleton_gun_kf7) # addiu $t6, $t6, 0x66ac
/* 0941B8 7F0617C8 8DF80004 */  lw    $t8, 4($t7)
/* 0941BC 7F0617CC 55D80074 */  bnel  $t6, $t8, .L7F0619A0
/* 0941C0 7F0617D0 8FB801A0 */   lw    $t8, 0x1a0($sp)
/* 0941C4 7F0617D4 8DF90008 */  lw    $t9, 8($t7)
/* 0941C8 7F0617D8 8F230010 */  lw    $v1, 0x10($t9)
/* 0941CC 7F0617DC 50600070 */  beql  $v1, $zero, .L7F0619A0
/* 0941D0 7F0617E0 8FB801A0 */   lw    $t8, 0x1a0($sp)
/* 0941D4 7F0617E4 8C620004 */  lw    $v0, 4($v1)
/* 0941D8 7F0617E8 C7A40224 */  lwc1  $f4, 0x224($sp)
/* 0941DC 7F0617EC C7B20234 */  lwc1  $f18, 0x234($sp)
/* 0941E0 7F0617F0 C4460000 */  lwc1  $f6, ($v0)
/* 0941E4 7F0617F4 C4500004 */  lwc1  $f16, 4($v0)
/* 0941E8 7F0617F8 3C018005 */  lui   $at, %hi(D_80053E28) # $at, 0x8005
/* 0941EC 7F0617FC 46043202 */  mul.s $f8, $f6, $f4
/* 0941F0 7F061800 C4440008 */  lwc1  $f4, 8($v0)
/* 0941F4 7F061804 8FAD02A4 */  lw    $t5, 0x2a4($sp)
/* 0941F8 7F061808 46128282 */  mul.s $f10, $f16, $f18
/* 0941FC 7F06180C C7B00244 */  lwc1  $f16, 0x244($sp)
/* 094200 7F061810 25AE00C0 */  addiu $t6, $t5, 0xc0
/* 094204 7F061814 46102482 */  mul.s $f18, $f4, $f16
/* 094208 7F061818 460A4180 */  add.s $f6, $f8, $f10
/* 09420C 7F06181C C7AA0254 */  lwc1  $f10, 0x254($sp)
/* 094210 7F061820 46123200 */  add.s $f8, $f6, $f18
/* 094214 7F061824 C7A60228 */  lwc1  $f6, 0x228($sp)
/* 094218 7F061828 46085100 */  add.s $f4, $f10, $f8
/* 09421C 7F06182C C7A80238 */  lwc1  $f8, 0x238($sp)
/* 094220 7F061830 E7A40084 */  swc1  $f4, 0x84($sp)
/* 094224 7F061834 C4500000 */  lwc1  $f16, ($v0)
/* 094228 7F061838 C44A0004 */  lwc1  $f10, 4($v0)
/* 09422C 7F06183C 46068482 */  mul.s $f18, $f16, $f6
/* 094230 7F061840 C4460008 */  lwc1  $f6, 8($v0)
/* 094234 7F061844 46085102 */  mul.s $f4, $f10, $f8
/* 094238 7F061848 C7AA0248 */  lwc1  $f10, 0x248($sp)
/* 09423C 7F06184C 460A3202 */  mul.s $f8, $f6, $f10
/* 094240 7F061850 46049400 */  add.s $f16, $f18, $f4
/* 094244 7F061854 C7A40258 */  lwc1  $f4, 0x258($sp)
/* 094248 7F061858 46088480 */  add.s $f18, $f16, $f8
/* 09424C 7F06185C C7B0022C */  lwc1  $f16, 0x22c($sp)
/* 094250 7F061860 46122180 */  add.s $f6, $f4, $f18
/* 094254 7F061864 C7B2023C */  lwc1  $f18, 0x23c($sp)
/* 094258 7F061868 E7A60088 */  swc1  $f6, 0x88($sp)
/* 09425C 7F06186C C44A0000 */  lwc1  $f10, ($v0)
/* 094260 7F061870 C4440004 */  lwc1  $f4, 4($v0)
/* 094264 7F061874 46105202 */  mul.s $f8, $f10, $f16
/* 094268 7F061878 C4500008 */  lwc1  $f16, 8($v0)
/* 09426C 7F06187C AFAE0040 */  sw    $t6, 0x40($sp)
/* 094270 7F061880 46122182 */  mul.s $f6, $f4, $f18
/* 094274 7F061884 C7A4024C */  lwc1  $f4, 0x24c($sp)
/* 094278 7F061888 46048482 */  mul.s $f18, $f16, $f4
/* 09427C 7F06188C C4249F68 */  lwc1  $f4, %lo(D_80053E28)($at)
/* 094280 7F061890 46064280 */  add.s $f10, $f8, $f6
/* 094284 7F061894 C7A6025C */  lwc1  $f6, 0x25c($sp)
/* 094288 7F061898 46125200 */  add.s $f8, $f10, $f18
/* 09428C 7F06189C C7AA0080 */  lwc1  $f10, 0x80($sp)
/* 094290 7F0618A0 460A2482 */  mul.s $f18, $f4, $f10
/* 094294 7F0618A4 46083400 */  add.s $f16, $f6, $f8
/* 094298 7F0618A8 E7B0008C */  swc1  $f16, 0x8c($sp)
/* 09429C 7F0618AC 0C00262C */  jal   randomGetNext
/* 0942A0 7F0618B0 E7B20038 */   swc1  $f18, 0x38($sp)
/* 0942A4 7F0618B4 44823000 */  mtc1  $v0, $f6
/* 0942A8 7F0618B8 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 0942AC 7F0618BC 04410005 */  bgez  $v0, .L7F0618D4
/* 0942B0 7F0618C0 46803220 */   cvt.s.w $f8, $f6
/* 0942B4 7F0618C4 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 0942B8 7F0618C8 44818000 */  mtc1  $at, $f16
/* 0942BC 7F0618CC 00000000 */  nop
/* 0942C0 7F0618D0 46104200 */  add.s $f8, $f8, $f16
.L7F0618D4:
/* 0942C4 7F0618D4 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 0942C8 7F0618D8 44812000 */  mtc1  $at, $f4
/* 0942CC 7F0618DC 3C018005 */  lui   $at, %hi(D_80053E2C) # $at, 0x8005
/* 0942D0 7F0618E0 C4329F6C */  lwc1  $f18, %lo(D_80053E2C)($at)
/* 0942D4 7F0618E4 46044282 */  mul.s $f10, $f8, $f4
/* 0942D8 7F0618E8 C7A40088 */  lwc1  $f4, 0x88($sp)
/* 0942DC 7F0618EC C7B00084 */  lwc1  $f16, 0x84($sp)
/* 0942E0 7F0618F0 46008207 */  neg.s $f8, $f16
/* 0942E4 7F0618F4 46125182 */  mul.s $f6, $f10, $f18
/* 0942E8 7F0618F8 C7B2008C */  lwc1  $f18, 0x8c($sp)
/* 0942EC 7F0618FC 46002287 */  neg.s $f10, $f4
/* 0942F0 7F061900 44064000 */  mfc1  $a2, $f8
/* 0942F4 7F061904 44075000 */  mfc1  $a3, $f10
/* 0942F8 7F061908 44053000 */  mfc1  $a1, $f6
/* 0942FC 7F06190C 46009187 */  neg.s $f6, $f18
/* 094300 7F061910 0FC16864 */  jal   matrix_4x4_align
/* 094304 7F061914 E7A60010 */   swc1  $f6, 0x10($sp)
/* 094308 7F061918 C7AC0038 */  lwc1  $f12, 0x38($sp)
/* 09430C 7F06191C 0FC163C9 */  jal   matrix_scalar_multiply
/* 094310 7F061920 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094314 7F061924 C7B00194 */  lwc1  $f16, 0x194($sp)
/* 094318 7F061928 C60801C8 */  lwc1  $f8, 0x1c8($s0)
/* 09431C 7F06192C C7AA0198 */  lwc1  $f10, 0x198($sp)
/* 094320 7F061930 C61201CC */  lwc1  $f18, 0x1cc($s0)
/* 094324 7F061934 46088101 */  sub.s $f4, $f16, $f8
/* 094328 7F061938 C60801D0 */  lwc1  $f8, 0x1d0($s0)
/* 09432C 7F06193C C7B0019C */  lwc1  $f16, 0x19c($sp)
/* 094330 7F061940 46125181 */  sub.s $f6, $f10, $f18
/* 094334 7F061944 44062000 */  mfc1  $a2, $f4
/* 094338 7F061948 27A40114 */  addiu $a0, $sp, 0x114
/* 09433C 7F06194C 46088101 */  sub.s $f4, $f16, $f8
/* 094340 7F061950 44073000 */  mfc1  $a3, $f6
/* 094344 7F061954 24050000 */  li    $a1, 0
/* 094348 7F061958 0FC16800 */  jal   matrix_4x4_set_rotation_axis_angle
/* 09434C 7F06195C E7A40010 */   swc1  $f4, 0x10($sp)
/* 094350 7F061960 27A40114 */  addiu $a0, $sp, 0x114
/* 094354 7F061964 0FC16144 */  jal   matrix_4x4_multiply_in_place
/* 094358 7F061968 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 09435C 7F06196C C7AC007C */  lwc1  $f12, 0x7c($sp)
/* 094360 7F061970 0FC1640A */  jal   matrix_row_3_scalar_multiply
/* 094364 7F061974 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094368 7F061978 27A40154 */  addiu $a0, $sp, 0x154
/* 09436C 7F06197C 0FC16144 */  jal   matrix_4x4_multiply_in_place
/* 094370 7F061980 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094374 7F061984 27A40084 */  addiu $a0, $sp, 0x84
/* 094378 7F061988 0FC16390 */  jal   matrix_4x4_set_position
/* 09437C 7F06198C 27A501E4 */   addiu $a1, $sp, 0x1e4
/* 094380 7F061990 27A401E4 */  addiu $a0, $sp, 0x1e4
/* 094384 7F061994 0FC16132 */  jal   matrix_4x4_copy
/* 094388 7F061998 8FA50040 */   lw    $a1, 0x40($sp)
.L7F06199C:
/* 09438C 7F06199C 8FB801A0 */  lw    $t8, 0x1a0($sp)
.L7F0619A0:
/* 094390 7F0619A0 1000000C */  b     .L7F0619D4
/* 094394 7F0619A4 8F020008 */   lw    $v0, 8($t8)
/* 094398 7F0619A8 C6100260 */  lwc1  $f16, 0x260($s0)
.L7F0619AC:
/* 09439C 7F0619AC C60A0298 */  lwc1  $f10, 0x298($s0)
/* 0943A0 7F0619B0 C612029C */  lwc1  $f18, 0x29c($s0)
/* 0943A4 7F0619B4 C60602A0 */  lwc1  $f6, 0x2a0($s0)
/* 0943A8 7F0619B8 46008207 */  neg.s $f8, $f16
/* 0943AC 7F0619BC E60A02E8 */  swc1  $f10, 0x2e8($s0)
/* 0943B0 7F0619C0 E60802F4 */  swc1  $f8, 0x2f4($s0)
/* 0943B4 7F0619C4 E61202EC */  swc1  $f18, 0x2ec($s0)
/* 0943B8 7F0619C8 E60602F0 */  swc1  $f6, 0x2f0($s0)
/* 0943BC 7F0619CC 8FAF01A0 */  lw    $t7, 0x1a0($sp)
/* 0943C0 7F0619D0 8DE20008 */  lw    $v0, 8($t7)
.L7F0619D4:
/* 0943C4 7F0619D4 8C440018 */  lw    $a0, 0x18($v0)
/* 0943C8 7F0619D8 50800043 */  beql  $a0, $zero, .L7F061AE8
/* 0943CC 7F0619DC 8FAD01A0 */   lw    $t5, 0x1a0($sp)
/* 0943D0 7F0619E0 8C990004 */  lw    $t9, 4($a0)
/* 0943D4 7F0619E4 00002825 */  move  $a1, $zero
/* 0943D8 7F0619E8 0FC1B32A */  jal   modelFindNodeMtxIndex
/* 0943DC 7F0619EC AFB90070 */   sw    $t9, 0x70($sp)
/* 0943E0 7F0619F0 AFA2006C */  sw    $v0, 0x6c($sp)
/* 0943E4 7F0619F4 8E050010 */  lw    $a1, 0x10($s0)
/* 0943E8 7F0619F8 0FC17ADB */  jal   sub_GAME_7F05E6B4
/* 0943EC 7F0619FC 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0943F0 7F061A00 8FAD01A0 */  lw    $t5, 0x1a0($sp)
/* 0943F4 7F061A04 8FA40070 */  lw    $a0, 0x70($sp)
/* 0943F8 7F061A08 27A601A4 */  addiu $a2, $sp, 0x1a4
/* 0943FC 7F061A0C 85AE000C */  lh    $t6, 0xc($t5)
/* 094400 7F061A10 29C1001D */  slti  $at, $t6, 0x1d
/* 094404 7F061A14 1420002A */  bnez  $at, .L7F061AC0
/* 094408 7F061A18 00000000 */   nop
/* 09440C 7F061A1C 8DB80008 */  lw    $t8, 8($t5)
/* 094410 7F061A20 8F030070 */  lw    $v1, 0x70($t8)
/* 094414 7F061A24 10600026 */  beqz  $v1, .L7F061AC0
/* 094418 7F061A28 00000000 */   nop
/* 09441C 7F061A2C 8C620004 */  lw    $v0, 4($v1)
/* 094420 7F061A30 8FA402A8 */  lw    $a0, 0x2a8($sp)
/* 094424 7F061A34 0FC17AC7 */  jal   get_value_if_watch_is_on_hand_or_not
/* 094428 7F061A38 AFA20068 */   sw    $v0, 0x68($sp)
/* 09442C 7F061A3C 3C018005 */  lui   $at, %hi(D_80053E30) # $at, 0x8005
/* 094430 7F061A40 C42A9F70 */  lwc1  $f10, %lo(D_80053E30)($at)
/* 094434 7F061A44 C6040214 */  lwc1  $f4, 0x214($s0)
/* 094438 7F061A48 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 09443C 7F061A4C 44818000 */  mtc1  $at, $f16
/* 094440 7F061A50 460A2480 */  add.s $f18, $f4, $f10
/* 094444 7F061A54 3C018005 */  lui   $at, %hi(D_80053E34) # $at, 0x8005
/* 094448 7F061A58 C4249F74 */  lwc1  $f4, %lo(D_80053E34)($at)
/* 09444C 7F061A5C 8FA20068 */  lw    $v0, 0x68($sp)
/* 094450 7F061A60 46009181 */  sub.s $f6, $f18, $f0
/* 094454 7F061A64 27A401A4 */  addiu $a0, $sp, 0x1a4
/* 094458 7F061A68 C4520000 */  lwc1  $f18, ($v0)
/* 09445C 7F061A6C 46103202 */  mul.s $f8, $f6, $f16
/* 094460 7F061A70 C446000C */  lwc1  $f6, 0xc($v0)
/* 094464 7F061A74 46069401 */  sub.s $f16, $f18, $f6
/* 094468 7F061A78 C4460014 */  lwc1  $f6, 0x14($v0)
/* 09446C 7F061A7C C4520008 */  lwc1  $f18, 8($v0)
/* 094470 7F061A80 46044283 */  div.s $f10, $f8, $f4
/* 094474 7F061A84 C4440010 */  lwc1  $f4, 0x10($v0)
/* 094478 7F061A88 C4480004 */  lwc1  $f8, 4($v0)
/* 09447C 7F061A8C 44068000 */  mfc1  $a2, $f16
/* 094480 7F061A90 46069401 */  sub.s $f16, $f18, $f6
/* 094484 7F061A94 E7B00010 */  swc1  $f16, 0x10($sp)
/* 094488 7F061A98 44055000 */  mfc1  $a1, $f10
/* 09448C 7F061A9C 46044281 */  sub.s $f10, $f8, $f4
/* 094490 7F061AA0 44075000 */  mfc1  $a3, $f10
/* 094494 7F061AA4 0C005B70 */  jal   guRotateF
/* 094498 7F061AA8 00000000 */   nop
/* 09449C 7F061AAC 8FA40070 */  lw    $a0, 0x70($sp)
/* 0944A0 7F061AB0 0FC16390 */  jal   matrix_4x4_set_position
/* 0944A4 7F061AB4 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 0944A8 7F061AB8 10000004 */  b     .L7F061ACC
/* 0944AC 7F061ABC 8FAF006C */   lw    $t7, 0x6c($sp)
.L7F061AC0:
/* 0944B0 7F061AC0 0FC1625E */  jal   matrix_4x4_set_position_and_rotation_around_y
/* 0944B4 7F061AC4 8E050214 */   lw    $a1, 0x214($s0)
/* 0944B8 7F061AC8 8FAF006C */  lw    $t7, 0x6c($sp)
.L7F061ACC:
/* 0944BC 7F061ACC 8FAE02A4 */  lw    $t6, 0x2a4($sp)
/* 0944C0 7F061AD0 27A40264 */  addiu $a0, $sp, 0x264
/* 0944C4 7F061AD4 000FC980 */  sll   $t9, $t7, 6
/* 0944C8 7F061AD8 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 0944CC 7F061ADC 0FC1618D */  jal   matrix_4x4_multiply_homogeneous
/* 0944D0 7F061AE0 032E3021 */   addu  $a2, $t9, $t6
/* 0944D4 7F061AE4 8FAD01A0 */  lw    $t5, 0x1a0($sp)
.L7F061AE8:
/* 0944D8 7F061AE8 8FA40044 */  lw    $a0, 0x44($sp)
/* 0944DC 7F061AEC 85B8000C */  lh    $t8, 0xc($t5)
/* 0944E0 7F061AF0 01A02825 */  move  $a1, $t5
/* 0944E4 7F061AF4 2B01001E */  slti  $at, $t8, 0x1e
/* 0944E8 7F061AF8 54200004 */  bnezl $at, .L7F061B0C
/* 0944EC 7F061AFC 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 0944F0 7F061B00 0FC21F5D */  jal   seems_to_load_cuff_microcode
/* 0944F4 7F061B04 2406001D */   li    $a2, 29
/* 0944F8 7F061B08 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.L7F061B0C:
/* 0944FC 7F061B0C 8DF90008 */  lw    $t9, 8($t7)
/* 094500 7F061B10 8F24001C */  lw    $a0, 0x1c($t9)
/* 094504 7F061B14 50800017 */  beql  $a0, $zero, .L7F061B74eu
/* 094508 7F061B18 8FB901A0 */   lw    $t9, 0x1a0($sp)
/* 09450C 7F061B1C 8C8E0004 */  lw    $t6, 4($a0)
/* 094510 7F061B20 00002825 */  move  $a1, $zero
/* 094514 7F061B24 0FC1B32A */  jal   modelFindNodeMtxIndex
/* 094518 7F061B28 AFAE0064 */   sw    $t6, 0x64($sp)
/* 09451C 7F061B2C AFA20060 */  sw    $v0, 0x60($sp)
/* 094520 7F061B30 0FC17B3D */  jal   sub_GAME_7F05E83C
/* 094524 7F061B34 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 094528 7F061B38 8FA40064 */  lw    $a0, 0x64($sp)
/* 09452C 7F061B3C 0FC16383 */  jal   matrix_4x4_set_identity_and_position
/* 094530 7F061B40 27A501A4 */   addiu $a1, $sp, 0x1a4
/* 094534 7F061B44 C7A801DC */  lwc1  $f8, 0x1dc($sp)
/* 094538 7F061B48 C6040218 */  lwc1  $f4, 0x218($s0)
/* 09453C 7F061B4C 8FB80060 */  lw    $t8, 0x60($sp)
/* 094540 7F061B50 8FAF02A4 */  lw    $t7, 0x2a4($sp)
/* 094544 7F061B54 46044281 */  sub.s $f10, $f8, $f4
/* 094548 7F061B58 00186980 */  sll   $t5, $t8, 6
/* 09454C 7F061B5C 27A40264 */  addiu $a0, $sp, 0x264
/* 094550 7F061B60 27A501A4 */  addiu $a1, $sp, 0x1a4
/* 094554 7F061B64 E7AA01DC */  swc1  $f10, 0x1dc($sp)
/* 094558 7F061B68 0FC1615C */  jal   matrix_4x4_multiply
/* 09455C 7F061B6C 01AF3021 */   addu  $a2, $t5, $t7
/* 094560 7F061B70 8FB901A0 */  lw    $t9, 0x1a0($sp)
.L7F061B74eu:
/* 094564 7F061B74 00001825 */  move  $v1, $zero
/* 094568 7F061B78 00003025 */  move  $a2, $zero
/* 09456C 7F061B7C 872E000C */  lh    $t6, 0xc($t9)
/* 094570 7F061B80 29C10013 */  slti  $at, $t6, 0x13
/* 094574 7F061B84 1420002B */  bnez  $at, .L7F061C34
/* 094578 7F061B88 00000000 */   nop
.L7F061B8C:
/* 09457C 7F061B8C 8FB801A0 */  lw    $t8, 0x1a0($sp)
/* 094580 7F061B90 8FA40044 */  lw    $a0, 0x44($sp)
/* 094584 7F061B94 8F0D0008 */  lw    $t5, 8($t8)
/* 094588 7F061B98 01A67821 */  addu  $t7, $t5, $a2
/* 09458C 7F061B9C 8DE50048 */  lw    $a1, 0x48($t7)
/* 094590 7F061BA0 50A0000E */  beql  $a1, $zero, .L7F061BDC
/* 094594 7F061BA4 8FAF01A0 */   lw    $t7, 0x1a0($sp)
/* 094598 7F061BA8 AFA3005C */  sw    $v1, 0x5c($sp)
/* 09459C 7F061BAC 0FC1B3A3 */  jal   modelGetNodeRwData
/* 0945A0 7F061BB0 AFA60040 */   sw    $a2, 0x40($sp)
/* 0945A4 7F061BB4 8FA3005C */  lw    $v1, 0x5c($sp)
/* 0945A8 7F061BB8 10400007 */  beqz  $v0, .L7F061BD8
/* 0945AC 7F061BBC 8FA60040 */   lw    $a2, 0x40($sp)
/* 0945B0 7F061BC0 8E190034 */  lw    $t9, 0x34($s0)
/* 0945B4 7F061BC4 240E0005 */  li    $t6, 5
/* 0945B8 7F061BC8 01C3C023 */  subu  $t8, $t6, $v1
/* 0945BC 7F061BCC 0338682A */  slt   $t5, $t9, $t8
/* 0945C0 7F061BD0 39AD0001 */  xori  $t5, $t5, 1
/* 0945C4 7F061BD4 AC4D0000 */  sw    $t5, ($v0)
.L7F061BD8:
/* 0945C8 7F061BD8 8FAF01A0 */  lw    $t7, 0x1a0($sp)
.L7F061BDC:
/* 0945CC 7F061BDC 8FA40044 */  lw    $a0, 0x44($sp)
/* 0945D0 7F061BE0 8DEE0008 */  lw    $t6, 8($t7)
/* 0945D4 7F061BE4 01C6C821 */  addu  $t9, $t6, $a2
/* 0945D8 7F061BE8 8F25005C */  lw    $a1, 0x5c($t9)
/* 0945DC 7F061BEC 50A0000E */  beql  $a1, $zero, .L7F061C28
/* 0945E0 7F061BF0 24630001 */   addiu $v1, $v1, 1
/* 0945E4 7F061BF4 AFA3005C */  sw    $v1, 0x5c($sp)
/* 0945E8 7F061BF8 0FC1B3A3 */  jal   modelGetNodeRwData
/* 0945EC 7F061BFC AFA60040 */   sw    $a2, 0x40($sp)
/* 0945F0 7F061C00 8FA3005C */  lw    $v1, 0x5c($sp)
/* 0945F4 7F061C04 10400007 */  beqz  $v0, .L7F061C24
/* 0945F8 7F061C08 8FA60040 */   lw    $a2, 0x40($sp)
/* 0945FC 7F061C0C 8E180034 */  lw    $t8, 0x34($s0)
/* 094600 7F061C10 240D0005 */  li    $t5, 5
/* 094604 7F061C14 01A37823 */  subu  $t7, $t5, $v1
/* 094608 7F061C18 030F702A */  slt   $t6, $t8, $t7
/* 09460C 7F061C1C 39CE0001 */  xori  $t6, $t6, 1
/* 094610 7F061C20 AC4E0000 */  sw    $t6, ($v0)
.L7F061C24:
/* 094614 7F061C24 24630001 */  addiu $v1, $v1, 1
.L7F061C28:
/* 094618 7F061C28 24010005 */  li    $at, 5
/* 09461C 7F061C2C 1461FFD7 */  bne   $v1, $at, .L7F061B8C
/* 094620 7F061C30 24C60004 */   addiu $a2, $a2, 4
.L7F061C34:
/* 094624 7F061C34 0FC1BCEC */  jal   modelUpdateNodeRelations
/* 094628 7F061C38 8FA40044 */   lw    $a0, 0x44($sp)
/* 09462C 7F061C3C 8219000C */  lb    $t9, 0xc($s0)
/* 094630 7F061C40 8FAD00FC */  lw    $t5, 0xfc($sp)
/* 094634 7F061C44 13200014 */  beqz  $t9, .L7F061C98
/* 094638 7F061C48 25B8FFFC */   addiu $t8, $t5, -4
/* 09463C 7F061C4C 2F010014 */  sltiu $at, $t8, 0x14
/* 094640 7F061C50 10200011 */  beqz  $at, .L7F061C98
/* 094644 7F061C54 0018C080 */   sll   $t8, $t8, 2
/* 094648 7F061C58 3C018005 */  lui   $at, %hi(jpt_weapon_bullet_type)
/* 09464C 7F061C5C 00380821 */  addu  $at, $at, $t8
/* 094650 7F061C60 8C389F78 */  lw    $t8, %lo(jpt_weapon_bullet_type)($at)
/* 094654 7F061C64 03000008 */  jr    $t8
/* 094658 7F061C68 00000000 */   nop
weapon_bullet_type_pistol:
/* 09465C 7F061C6C 0FC1882E */  jal   sub_GAME_7F061BF4
/* 094660 7F061C70 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 094664 7F061C74 8E0F0030 */  lw    $t7, 0x30($s0)
/* 094668 7F061C78 25EE0001 */  addiu $t6, $t7, 1
/* 09466C 7F061C7C 10000006 */  b     .L7F061C98
/* 094670 7F061C80 AE0E0030 */   sw    $t6, 0x30($s0)
weapon_bullet_type_none:
/* 094674 7F061C84 8E190030 */  lw    $t9, 0x30($s0)
/* 094678 7F061C88 272D0001 */  addiu $t5, $t9, 1
/* 09467C 7F061C8C AE0D0030 */  sw    $t5, 0x30($s0)
/* 094680 7F061C90 0FC1882E */  jal   sub_GAME_7F061BF4
/* 094684 7F061C94 8FA402A8 */   lw    $a0, 0x2a8($sp)
weapon_bullet_type_shotgun_mine:
.L7F061C98:
/* 094688 7F061C98 8FB800FC */  lw    $t8, 0xfc($sp)

/* 09468C 7F061C9C 24010019 */  li    $at, 25
/* 094690 7F061CA0 57010004 */  bnel  $t8, $at, .L7F061CB4
/* 094694 7F061CA4 820F000C */   lb    $t7, 0xc($s0)
/* 094698 7F061CA8 0FC17F78 */  jal   sub_GAME_7F05F928
/* 09469C 7F061CAC 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0946A0 7F061CB0 820F000C */  lb    $t7, 0xc($s0)
.L7F061CB4:
/* 0946A4 7F061CB4 3C048007 */  lui   $a0, %hi(g_CurrentPlayer) # $a0, 0x8007
/* 0946A8 7F061CB8 51E00046 */  beql  $t7, $zero, .L7F061DD4
/* 0946AC 7F061CBC 8FBF0034 */   lw    $ra, 0x34($sp)
/* 0946B0 7F061CC0 0FC22638 */  jal   bondviewGetPlayerStanHeight
/* 0946B4 7F061CC4 8C848BC0 */   lw    $a0, %lo(g_CurrentPlayer)($a0)
/* 0946B8 7F061CC8 44050000 */  mfc1  $a1, $f0
/* 0946BC 7F061CCC 0FC1A31A */  jal   sub_GAME_7F068508
/* 0946C0 7F061CD0 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0946C4 7F061CD4 8FAE00FC */  lw    $t6, 0xfc($sp)
/* 0946C8 7F061CD8 24010018 */  li    $at, 24
/* 0946CC 7F061CDC 8FB900FC */  lw    $t9, 0xfc($sp)
/* 0946D0 7F061CE0 55C10006 */  bnel  $t6, $at, .L7F061CFC
/* 0946D4 7F061CE4 2401001A */   li    $at, 26
/* 0946D8 7F061CE8 0FC17EFD */  jal   sub_GAME_7F05F73C
/* 0946DC 7F061CEC 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0946E0 7F061CF0 10000038 */  b     .L7F061DD4
/* 0946E4 7F061CF4 8FBF0034 */   lw    $ra, 0x34($sp)
/* 0946E8 7F061CF8 2401001A */  li    $at, 26
.L7F061CFC:
/* 0946EC 7F061CFC 17210005 */  bne   $t9, $at, .L7F061D14
/* 0946F0 7F061D00 8FAD00FC */   lw    $t5, 0xfc($sp)
/* 0946F4 7F061D04 0FC17CB7 */  jal   generate_player_thrown_grenade
/* 0946F8 7F061D08 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0946FC 7F061D0C 10000031 */  b     .L7F061DD4
/* 094700 7F061D10 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061D14:
/* 094704 7F061D14 24010019 */  li    $at, 25
/* 094708 7F061D18 15A10005 */  bne   $t5, $at, .L7F061D30
/* 09470C 7F061D1C 8FB800FC */   lw    $t8, 0xfc($sp)
/* 094710 7F061D20 0FC18007 */  jal   gunFireTankShell
/* 094714 7F061D24 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 094718 7F061D28 1000002A */  b     .L7F061DD4
/* 09471C 7F061D2C 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061D30:
/* 094720 7F061D30 24010003 */  li    $at, 3
/* 094724 7F061D34 17010005 */  bne   $t8, $at, .L7F061D4C
/* 094728 7F061D38 8FAF00FC */   lw    $t7, 0xfc($sp)
/* 09472C 7F061D3C 0FC17D55 */  jal   generate_player_thrown_knife
/* 094730 7F061D40 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 094734 7F061D44 10000023 */  b     .L7F061DD4
/* 094738 7F061D48 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061D4C:
/* 09473C 7F061D4C 2401001D */  li    $at, 29
/* 094740 7F061D50 11E1000F */  beq   $t7, $at, .L7F061D90
/* 094744 7F061D54 2401001C */   li    $at, 28
/* 094748 7F061D58 11E1000D */  beq   $t7, $at, .L7F061D90
/* 09474C 7F061D5C 2401001B */   li    $at, 27
/* 094750 7F061D60 11E1000B */  beq   $t7, $at, .L7F061D90
/* 094754 7F061D64 24010021 */   li    $at, 33
/* 094758 7F061D68 11E10009 */  beq   $t7, $at, .L7F061D90
/* 09475C 7F061D6C 2401002F */   li    $at, 47
/* 094760 7F061D70 11E10007 */  beq   $t7, $at, .L7F061D90
/* 094764 7F061D74 24010030 */   li    $at, 48
/* 094768 7F061D78 11E10005 */  beq   $t7, $at, .L7F061D90
/* 09476C 7F061D7C 2401003D */   li    $at, 61
/* 094770 7F061D80 11E10003 */  beq   $t7, $at, .L7F061D90
/* 094774 7F061D84 24010022 */   li    $at, 34
/* 094778 7F061D88 15E10005 */  bne   $t7, $at, .L7F061DA0
/* 09477C 7F061D8C 8FAE00FC */   lw    $t6, 0xfc($sp)
.L7F061D90:
/* 094780 7F061D90 0FC17E04 */  jal   generate_player_thrown_object
/* 094784 7F061D94 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 094788 7F061D98 1000000E */  b     .L7F061DD4
/* 09478C 7F061D9C 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061DA0:
/* 094790 7F061DA0 24010023 */  li    $at, 35
/* 094794 7F061DA4 15C10005 */  bne   $t6, $at, .L7F061DBC
/* 094798 7F061DA8 8FB900FC */   lw    $t9, 0xfc($sp)
/* 09479C 7F061DAC 0FC17EFD */  jal   sub_GAME_7F05F73C
/* 0947A0 7F061DB0 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0947A4 7F061DB4 10000007 */  b     .L7F061DD4
/* 0947A8 7F061DB8 8FBF0034 */   lw    $ra, 0x34($sp)
.L7F061DBC:
/* 0947AC 7F061DBC 24010024 */  li    $at, 36
/* 0947B0 7F061DC0 57210004 */  bnel  $t9, $at, .L7F061DD4
/* 0947B4 7F061DC4 8FBF0034 */   lw    $ra, 0x34($sp)
/* 0947B8 7F061DC8 0FC17EFD */  jal   sub_GAME_7F05F73C
/* 0947BC 7F061DCC 8FA402A8 */   lw    $a0, 0x2a8($sp)
/* 0947C0 7F061DD0 8FBF0034 */  lw    $ra, 0x34($sp)
.L7F061DD4:
/* 0947C4 7F061DD4 8FB00030 */  lw    $s0, 0x30($sp)
/* 0947C8 7F061DD8 27BD02A8 */  addiu $sp, $sp, 0x2a8
/* 0947CC 7F061DDC 03E00008 */  jr    $ra
/* 0947D0 7F061DE0 00000000 */   nop
)
#endif

#endif



void bondwalkFireBothHands(void)
{
    handles_firing_or_throwing_weapon_in_hand(GUNRIGHT);
    handles_firing_or_throwing_weapon_in_hand(GUNLEFT);
}






/**
 * @param arg0:
 * @param item:
 * @param arg2:
 * @param arg3:
 *
 * Address 0x7F061948.
 *
 * This function adjusts the length of the bullet beam that's rendered on screen.
 * This function is used for both player and guard beams.
 *
 * The watch laser has a very short beam, in accordance with its range.
 * The laser also has a shortened one, but it appears this is to avoid graphical glitches.
 * Other weapons have their bullet beam capped at 10000 max length, otherwise if the player
 * fires into the void, there may be graphical glitches with the beam.
 *
*/
void CapBeamLengthAndDecideIfRendered(struct ChrRecord_f180 *arg0, ITEM_IDS item, coord3d *arg2, coord3d *arg3)
{
    f32 phi_f12_2;

    //arg0->pos.x = arg2->x;
    //arg0->pos.y = arg2->y;
    //arg0->pos.z = arg2->z;

    //arg0->delta.x = arg3->x - arg2->x;
    //arg0->delta.y = arg3->y - arg2->y;
    //arg0->delta.z = arg3->z - arg2->z;

    //phi_f12_2 = sqrtf((arg0->delta.f[0] * arg0->delta.f[0]) + (arg0->delta.f[1] * arg0->delta.f[1]) + (arg0->delta.f[2] * arg0->delta.f[2]));

    //arg0->delta.x *= 1.0f / phi_f12_2;
    //arg0->delta.y *= 1.0f / phi_f12_2;
    //arg0->delta.z *= 1.0f / phi_f12_2;


    arg0->pos.f[0] = arg2->f[0];
    arg0->pos.f[1] = arg2->f[1];
    arg0->pos.f[2] = arg2->f[2];

    arg0->delta.f[0] = arg3->x - arg2->x;
    arg0->delta.f[1] = arg3->f[1] - arg2->f[1];
    arg0->delta.f[2] = arg3->f[2] - arg2->f[2];

    phi_f12_2 = sqrtf((arg0->delta.f[0] * arg0->delta.f[0]) + (arg0->delta.f[1] * arg0->delta.f[1]) + (arg0->delta.f[2] * arg0->delta.f[2]));

    arg0->delta.f[0] *= 1.0f / phi_f12_2;
    arg0->delta.f[1] *= 1.0f / phi_f12_2;
    arg0->delta.f[2] *= 1.0f / phi_f12_2;

    if (item == ITEM_WATCHLASER)
    {
        if (phi_f12_2 > 300.0f)
        {
            phi_f12_2 = 300.0f;
        }
    }
    else
    {
        if (phi_f12_2 > 10000.0f)
        {
            phi_f12_2 = 10000.0f;
        }
    }

    arg0->unk00 = 0;
    arg0->item_id = (s8) item;
    arg0->unk1c = phi_f12_2;

    if (phi_f12_2 < 500.0f)
    {
        phi_f12_2 = 500.0f;
    }

    if (item == ITEM_LASER)
    {
        arg0->unk20 = 0.25f * phi_f12_2;
        arg0->unk24 = 0.6f * phi_f12_2;

        if (arg0->unk24 > 3000.0f)
        {
            arg0->unk24 = 3000.0f;
        }

        // Laser beams are rendered more often than other normal weapons
        arg0->unk28 = (-0.1f - ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX) * 0.3f)) * phi_f12_2;
    }
    else if (item == ITEM_WATCHLASER)
    {
        arg0->unk24 = phi_f12_2;
        arg0->unk20 = 2.0f * phi_f12_2;

        if (phi_f12_2 > 3000.0f)
        {
            arg0->unk24 = 3000.0f;
        }

        // Always render the beam for the watch laser
        arg0->unk28 = 0.0f;
    }
    else
    {
        arg0->unk20 = 0.2f * phi_f12_2;
        arg0->unk24 = arg0->unk20;

        if (arg0->unk20 > 3000.0f)
        {
            arg0->unk24 = 3000.0f;
        }

        // Decide if a beam should be rendered for normal weapon bullets
        arg0->unk28 = ((2.0f * ((f32) (u32)randomGetNext() * (1.0f / UINT_MAX))) - 1.0f) * arg0->unk20;
    }

    if (arg0->unk1c <= arg0->unk28)
    {
        // No beam will be rendered
        arg0->unk00 = -1;
    }
}


void sub_GAME_7F061BF4(enum GUNHAND hand) {
    coord3d *field_2A18;
    Mtxf *player_matrix;
    struct hand *hand_ptr;
    f32 val;
    struct ChrRecord *chr;
    f32 diff1_z;
    f32 diff1_y;
    f32 diff1_x;
    f32 diff2_z;
    f32 diff2_y;
    f32 diff2_x;
    ChrRecord_f180 *field_A54;

    hand_ptr = &g_CurrentPlayer->hands[hand];
    player_matrix = camGetWorldToScreenMtxf();

    val = -((((hand_ptr->item_related.x * player_matrix->m[0][2]) + (hand_ptr->item_related.y * player_matrix->m[1][2])) + (hand_ptr->item_related.z * player_matrix->m[2][2])) + player_matrix->m[3][2]);
    if (val < hand_ptr->field_B64) { return; }

    field_A54 = &hand_ptr->field_A54;
    CapBeamLengthAndDecideIfRendered(
        field_A54,
        getCurrentPlayerWeaponId(hand),
        &hand_ptr->field_B58,
        &hand_ptr->item_related);

    if ((g_CurrentPlayer->prop->chr == NULL) || (getPlayerCount() < 2)) { return; }

    chr = g_CurrentPlayer->prop->chr;

    diff1_x = hand_ptr->item_related.x - g_CurrentPlayer->field_2A18[hand].x;
    diff1_y = hand_ptr->item_related.y - g_CurrentPlayer->field_2A18[hand].y;
    diff1_z = hand_ptr->item_related.z - g_CurrentPlayer->field_2A18[hand].z;
    guNormalize(&diff1_x, &diff1_y, &diff1_z);

    diff2_x = hand_ptr->item_related.x - hand_ptr->field_B58.x;
    diff2_y = hand_ptr->item_related.y - hand_ptr->field_B58.y;
    diff2_z = hand_ptr->item_related.z - hand_ptr->field_B58.z;
    guNormalize(&diff2_x, &diff2_y, &diff2_z);

    val = acosf(
        + (diff2_z * diff1_z)
        + ((diff1_x * diff2_x)
        + (diff1_y * diff2_y)));
    if (val > 0.08726647f) { return; }

    CapBeamLengthAndDecideIfRendered(
        &chr->unk180[hand],
        getCurrentPlayerWeaponId(hand),
        &g_CurrentPlayer->field_2A18[hand],
        &hand_ptr->item_related);
}


#ifdef NONMATCHING
void sub_GAME_7F061E18(void) {

}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.late_rodata
glabel D_80053EAC
.word 0x3fb50481 /*1.4141999*/
glabel D_80053EB0
.word 0x3dcccccd /*0.1*/
glabel D_80053EB4
.word 0x3f666666 /*0.89999998*/
glabel D_80053EB8
.word 0x3f666666 /*0.89999998*/
glabel D_80053EBC
.word 0x3f666666 /*0.89999998*/
glabel D_80053EC0
.word 0x3f666666 /*0.89999998*/
glabel D_80053EC4
.word 0x3f666666 /*0.89999998*/
glabel D_80053EC8
.word 0x3f666666 /*0.89999998*/
glabel D_80053ECC
.word 0x3fb50481 /*1.4141999*/
glabel D_80053ED0
.word 0x3f666666 /*0.89999998*/
.text
glabel sub_GAME_7F061E18
/* 096948 7F061E18 27BDFEA0 */  addiu $sp, $sp, -0x160
/* 09694C 7F061E1C AFBF002C */  sw    $ra, 0x2c($sp)
/* 096950 7F061E20 AFB10028 */  sw    $s1, 0x28($sp)
/* 096954 7F061E24 AFB00024 */  sw    $s0, 0x24($sp)
/* 096958 7F061E28 F7B40018 */  sdc1  $f20, 0x18($sp)
/* 09695C 7F061E2C AFA40160 */  sw    $a0, 0x160($sp)
/* 096960 7F061E30 AFA60168 */  sw    $a2, 0x168($sp)
/* 096964 7F061E34 80AB0000 */  lb    $t3, ($a1)
/* 096968 7F061E38 3C0E8003 */  lui   $t6, %hi(D_80035C98)
/* 09696C 7F061E3C 00A08825 */  move  $s1, $a1
/* 096970 7F061E40 05600328 */  bltz  $t3, .L7F062AE4
/* 096974 7F061E44 25CE5C98 */   addiu $t6, %lo(D_80035C98) # addiu $t6, $t6, 0x5c98
/* 096978 7F061E48 8DC10000 */  lw    $at, ($t6)
/* 09697C 7F061E4C 8DD90004 */  lw    $t9, 4($t6)
/* 096980 7F061E50 27A90108 */  addiu $t1, $sp, 0x108
/* 096984 7F061E54 AD210000 */  sw    $at, ($t1)
/* 096988 7F061E58 AD390004 */  sw    $t9, 4($t1)
/* 09698C 7F061E5C 8DD9000C */  lw    $t9, 0xc($t6)
/* 096990 7F061E60 8DC10008 */  lw    $at, 8($t6)
/* 096994 7F061E64 AD39000C */  sw    $t9, 0xc($t1)
/* 096998 7F061E68 0FC227F5 */  jal   bondviewGetCurrentPlayersPosition
/* 09699C 7F061E6C AD210008 */   sw    $at, 8($t1)
/* 0969A0 7F061E70 AFA200F8 */  sw    $v0, 0xf8($sp)
/* 0969A4 7F061E74 3C0D8003 */  lui   $t5, %hi(D_80035CA8)
/* 0969A8 7F061E78 25AD5CA8 */  addiu $t5, %lo(D_80035CA8) # addiu $t5, $t5, 0x5ca8
/* 0969AC 7F061E7C 8DA10000 */  lw    $at, ($t5)
/* 0969B0 7F061E80 C6200028 */  lwc1  $f0, 0x28($s1)
/* 0969B4 7F061E84 C6340024 */  lwc1  $f20, 0x24($s1)
/* 0969B8 7F061E88 27AF00C4 */  addiu $t7, $sp, 0xc4
/* 0969BC 7F061E8C ADE10000 */  sw    $at, ($t7)
/* 0969C0 7F061E90 8DA10008 */  lw    $at, 8($t5)
/* 0969C4 7F061E94 8DAB0004 */  lw    $t3, 4($t5)
/* 0969C8 7F061E98 3C098003 */  lui   $t1, %hi(D_80035CB4)
/* 0969CC 7F061E9C 25295CB4 */  addiu $t1, %lo(D_80035CB4) # addiu $t1, $t1, 0x5cb4
/* 0969D0 7F061EA0 ADE10008 */  sw    $at, 8($t7)
/* 0969D4 7F061EA4 ADEB0004 */  sw    $t3, 4($t7)
/* 0969D8 7F061EA8 8D210000 */  lw    $at, ($t1)
/* 0969DC 7F061EAC 27B800B8 */  addiu $t8, $sp, 0xb8
/* 0969E0 7F061EB0 8D2A0004 */  lw    $t2, 4($t1)
/* 0969E4 7F061EB4 AF010000 */  sw    $at, ($t8)
/* 0969E8 7F061EB8 8D210008 */  lw    $at, 8($t1)
/* 0969EC 7F061EBC AF0A0004 */  sw    $t2, 4($t8)
/* 0969F0 7F061EC0 3C0C8009 */  lui   $t4, %hi(flareimage3)
/* 0969F4 7F061EC4 AF010008 */  sw    $at, 8($t8)
/* 0969F8 7F061EC8 3C018005 */  lui   $at, %hi(D_80053EAC)
/* 0969FC 7F061ECC C4243EAC */  lwc1  $f4, %lo(D_80053EAC)($at)
/* 096A00 7F061ED0 8D8CD0D0 */  lw    $t4, %lo(flareimage3)($t4)
/* 096A04 7F061ED4 E7A000E8 */  swc1  $f0, 0xe8($sp)
/* 096A08 7F061ED8 E7A400B4 */  swc1  $f4, 0xb4($sp)
/* 096A0C 7F061EDC 0FC1E0F1 */  jal   camGetWorldToScreenMtxf
/* 096A10 7F061EE0 AFAC00B0 */   sw    $t4, 0xb0($sp)
/* 096A14 7F061EE4 AFA200A8 */  sw    $v0, 0xa8($sp)
/* 096A18 7F061EE8 82230001 */  lb    $v1, 1($s1)
/* 096A1C 7F061EEC 24010016 */  li    $at, 22
/* 096A20 7F061EF0 C7A000E8 */  lwc1  $f0, 0xe8($sp)
/* 096A24 7F061EF4 14610007 */  bne   $v1, $at, .L7F061F14
/* 096A28 7F061EF8 3C014248 */   li    $at, 0x42480000 # 50.000000
/* 096A2C 7F061EFC 44819000 */  mtc1  $at, $f18
/* 096A30 7F061F00 3C0F8009 */  lui   $t7, %hi(flareimage4)
/* 096A34 7F061F04 8DEFD0D4 */  lw    $t7, %lo(flareimage4)($t7)
/* 096A38 7F061F08 E7B200F4 */  swc1  $f18, 0xf4($sp)
/* 096A3C 7F061F0C 10000026 */  b     .L7F061FA8
/* 096A40 7F061F10 AFAF00B0 */   sw    $t7, 0xb0($sp)
.L7F061F14:
/* 096A44 7F061F14 24010017 */  li    $at, 23
/* 096A48 7F061F18 1461001F */  bne   $v1, $at, .L7F061F98
/* 096A4C 7F061F1C 3C0D8009 */   lui   $t5, %hi(flareimage4)
/* 096A50 7F061F20 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 096A54 7F061F24 44813000 */  mtc1  $at, $f6
/* 096A58 7F061F28 8DADD0D4 */  lw    $t5, %lo(flareimage4)($t5)
/* 096A5C 7F061F2C E7A000E8 */  swc1  $f0, 0xe8($sp)
/* 096A60 7F061F30 E7A600F4 */  swc1  $f6, 0xf4($sp)
/* 096A64 7F061F34 0C002914 */  jal   randomGetNext
/* 096A68 7F061F38 AFAD00B0 */   sw    $t5, 0xb0($sp)
/* 096A6C 7F061F3C 24010032 */  li    $at, 50
/* 096A70 7F061F40 0041001B */  divu  $zero, $v0, $at
/* 096A74 7F061F44 00005810 */  mfhi  $t3
/* 096A78 7F061F48 25790096 */  addiu $t9, $t3, 0x96
/* 096A7C 7F061F4C 0C002914 */  jal   randomGetNext
/* 096A80 7F061F50 A3B90117 */   sb    $t9, 0x117($sp)
/* 096A84 7F061F54 24010005 */  li    $at, 5
/* 096A88 7F061F58 0041001B */  divu  $zero, $v0, $at
/* 096A8C 7F061F5C 00007010 */  mfhi  $t6
/* 096A90 7F061F60 C7A000E8 */  lwc1  $f0, 0xe8($sp)
/* 096A94 7F061F64 55C00011 */  bnezl $t6, .L7F061FAC
/* 096A98 7F061F68 C6240004 */   lwc1  $f4, 4($s1)
/* 096A9C 7F061F6C 0C002914 */  jal   randomGetNext
/* 096AA0 7F061F70 E7A000E8 */   swc1  $f0, 0xe8($sp)
/* 096AA4 7F061F74 24010064 */  li    $at, 100
/* 096AA8 7F061F78 0041001B */  divu  $zero, $v0, $at
/* 096AAC 7F061F7C 0000C010 */  mfhi  $t8
/* 096AB0 7F061F80 240900FF */  li    $t1, 255
/* 096AB4 7F061F84 01381823 */  subu  $v1, $t1, $t8
/* 096AB8 7F061F88 A3A30115 */  sb    $v1, 0x115($sp)
/* 096ABC 7F061F8C A3A30114 */  sb    $v1, 0x114($sp)
/* 096AC0 7F061F90 10000005 */  b     .L7F061FA8
/* 096AC4 7F061F94 C7A000E8 */   lwc1  $f0, 0xe8($sp)
.L7F061F98:
/* 096AC8 7F061F98 3C0141F0 */  li    $at, 0x41F00000 # 30.000000
/* 096ACC 7F061F9C 44815000 */  mtc1  $at, $f10
/* 096AD0 7F061FA0 00000000 */  nop
/* 096AD4 7F061FA4 E7AA00F4 */  swc1  $f10, 0xf4($sp)
.L7F061FA8:
/* 096AD8 7F061FA8 C6240004 */  lwc1  $f4, 4($s1)
.L7F061FAC:
/* 096ADC 7F061FAC 44807000 */  mtc1  $zero, $f14
/* 096AE0 7F061FB0 E7A400FC */  swc1  $f4, 0xfc($sp)
/* 096AE4 7F061FB4 C6280008 */  lwc1  $f8, 8($s1)
/* 096AE8 7F061FB8 4600703C */  c.lt.s $f14, $f0
/* 096AEC 7F061FBC E7A80100 */  swc1  $f8, 0x100($sp)
/* 096AF0 7F061FC0 C626000C */  lwc1  $f6, 0xc($s1)
/* 096AF4 7F061FC4 45000011 */  bc1f  .L7F06200C
/* 096AF8 7F061FC8 E7A60104 */   swc1  $f6, 0x104($sp)
/* 096AFC 7F061FCC C6240010 */  lwc1  $f4, 0x10($s1)
/* 096B00 7F061FD0 C7AA00FC */  lwc1  $f10, 0xfc($sp)
/* 096B04 7F061FD4 46040202 */  mul.s $f8, $f0, $f4
/* 096B08 7F061FD8 C7A40100 */  lwc1  $f4, 0x100($sp)
/* 096B0C 7F061FDC 46085180 */  add.s $f6, $f10, $f8
/* 096B10 7F061FE0 E7A600FC */  swc1  $f6, 0xfc($sp)
/* 096B14 7F061FE4 C62A0014 */  lwc1  $f10, 0x14($s1)
/* 096B18 7F061FE8 460A0202 */  mul.s $f8, $f0, $f10
/* 096B1C 7F061FEC C7AA0104 */  lwc1  $f10, 0x104($sp)
/* 096B20 7F061FF0 46082180 */  add.s $f6, $f4, $f8
/* 096B24 7F061FF4 E7A60100 */  swc1  $f6, 0x100($sp)
/* 096B28 7F061FF8 C6240018 */  lwc1  $f4, 0x18($s1)
/* 096B2C 7F061FFC 46040202 */  mul.s $f8, $f0, $f4
/* 096B30 7F062000 46085180 */  add.s $f6, $f10, $f8
/* 096B34 7F062004 10000003 */  b     .L7F062014
/* 096B38 7F062008 E7A60104 */   swc1  $f6, 0x104($sp)
.L7F06200C:
/* 096B3C 7F06200C 4600A500 */  add.s $f20, $f20, $f0
/* 096B40 7F062010 46007006 */  mov.s $f0, $f14
.L7F062014:
/* 096B44 7F062014 46140100 */  add.s $f4, $f0, $f20
/* 096B48 7F062018 C622001C */  lwc1  $f2, 0x1c($s1)
/* 096B4C 7F06201C 4604103C */  c.lt.s $f2, $f4
/* 096B50 7F062020 00000000 */  nop
/* 096B54 7F062024 45020003 */  bc1fl .L7F062034
/* 096B58 7F062028 C62C0018 */   lwc1  $f12, 0x18($s1)
/* 096B5C 7F06202C 46001501 */  sub.s $f20, $f2, $f0
/* 096B60 7F062030 C62C0018 */  lwc1  $f12, 0x18($s1)
.L7F062034:
/* 096B64 7F062034 C7AA0104 */  lwc1  $f10, 0x104($sp)
/* 096B68 7F062038 8FA200F8 */  lw    $v0, 0xf8($sp)
/* 096B6C 7F06203C 460CA202 */  mul.s $f8, $f20, $f12
/* 096B70 7F062040 C6220014 */  lwc1  $f2, 0x14($s1)
/* 096B74 7F062044 C4440008 */  lwc1  $f4, 8($v0)
/* 096B78 7F062048 E7AA0030 */  swc1  $f10, 0x30($sp)
/* 096B7C 7F06204C 46085180 */  add.s $f6, $f10, $f8
/* 096B80 7F062050 C44A0004 */  lwc1  $f10, 4($v0)
/* 096B84 7F062054 46062201 */  sub.s $f8, $f4, $f6
/* 096B88 7F062058 46081102 */  mul.s $f4, $f2, $f8
/* 096B8C 7F06205C C7A80100 */  lwc1  $f8, 0x100($sp)
/* 096B90 7F062060 46141182 */  mul.s $f6, $f2, $f20
/* 096B94 7F062064 46083180 */  add.s $f6, $f6, $f8
/* 096B98 7F062068 46065281 */  sub.s $f10, $f10, $f6
/* 096B9C 7F06206C 460C5182 */  mul.s $f6, $f10, $f12
/* 096BA0 7F062070 46062281 */  sub.s $f10, $f4, $f6
/* 096BA4 7F062074 C7A400FC */  lwc1  $f4, 0xfc($sp)
/* 096BA8 7F062078 E7AA00D0 */  swc1  $f10, 0xd0($sp)
/* 096BAC 7F06207C C6200010 */  lwc1  $f0, 0x10($s1)
/* 096BB0 7F062080 C62C0018 */  lwc1  $f12, 0x18($s1)
/* 096BB4 7F062084 E7A80034 */  swc1  $f8, 0x34($sp)
/* 096BB8 7F062088 4600A182 */  mul.s $f6, $f20, $f0
/* 096BBC 7F06208C C4480000 */  lwc1  $f8, ($v0)
/* 096BC0 7F062090 E7AA0038 */  swc1  $f10, 0x38($sp)
/* 096BC4 7F062094 C7AA0030 */  lwc1  $f10, 0x30($sp)
/* 096BC8 7F062098 46062180 */  add.s $f6, $f4, $f6
/* 096BCC 7F06209C 46064201 */  sub.s $f8, $f8, $f6
/* 096BD0 7F0620A0 46086182 */  mul.s $f6, $f12, $f8
/* 096BD4 7F0620A4 00000000 */  nop
/* 096BD8 7F0620A8 46146202 */  mul.s $f8, $f12, $f20
/* 096BDC 7F0620AC 460A4200 */  add.s $f8, $f8, $f10
/* 096BE0 7F0620B0 C44A0008 */  lwc1  $f10, 8($v0)
/* 096BE4 7F0620B4 46085281 */  sub.s $f10, $f10, $f8
/* 096BE8 7F0620B8 46005202 */  mul.s $f8, $f10, $f0
/* 096BEC 7F0620BC 46083281 */  sub.s $f10, $f6, $f8
/* 096BF0 7F0620C0 C7A60034 */  lwc1  $f6, 0x34($sp)
/* 096BF4 7F0620C4 E7AA00D4 */  swc1  $f10, 0xd4($sp)
/* 096BF8 7F0620C8 C6220014 */  lwc1  $f2, 0x14($s1)
/* 096BFC 7F0620CC C6200010 */  lwc1  $f0, 0x10($s1)
/* 096C00 7F0620D0 4602A202 */  mul.s $f8, $f20, $f2
/* 096C04 7F0620D4 46083180 */  add.s $f6, $f6, $f8
/* 096C08 7F0620D8 C4480004 */  lwc1  $f8, 4($v0)
/* 096C0C 7F0620DC 46064201 */  sub.s $f8, $f8, $f6
/* 096C10 7F0620E0 46080182 */  mul.s $f6, $f0, $f8
/* 096C14 7F0620E4 00000000 */  nop
/* 096C18 7F0620E8 46140202 */  mul.s $f8, $f0, $f20
/* 096C1C 7F0620EC 46044200 */  add.s $f8, $f8, $f4
/* 096C20 7F0620F0 C4440000 */  lwc1  $f4, ($v0)
/* 096C24 7F0620F4 46082101 */  sub.s $f4, $f4, $f8
/* 096C28 7F0620F8 46022202 */  mul.s $f8, $f4, $f2
/* 096C2C 7F0620FC 46083101 */  sub.s $f4, $f6, $f8
/* 096C30 7F062100 C7A60038 */  lwc1  $f6, 0x38($sp)
/* 096C34 7F062104 46067032 */  c.eq.s $f14, $f6
/* 096C38 7F062108 E7A400D8 */  swc1  $f4, 0xd8($sp)
/* 096C3C 7F06210C 45000008 */  bc1f  .L7F062130
/* 096C40 7F062110 00000000 */   nop
/* 096C44 7F062114 460A7032 */  c.eq.s $f14, $f10
/* 096C48 7F062118 00000000 */  nop
/* 096C4C 7F06211C 45020005 */  bc1fl .L7F062134
/* 096C50 7F062120 27A400D0 */   addiu $a0, $sp, 0xd0
/* 096C54 7F062124 46047032 */  c.eq.s $f14, $f4
/* 096C58 7F062128 00000000 */  nop
/* 096C5C 7F06212C 4501000F */  bc1t  .L7F06216C
.L7F062130:
/* 096C60 7F062130 27A400D0 */   addiu $a0, $sp, 0xd0
.L7F062134:
/* 096C64 7F062134 27A500D4 */  addiu $a1, $sp, 0xd4
/* 096C68 7F062138 0C007DD4 */  jal   guNormalize
/* 096C6C 7F06213C 27A600D8 */   addiu $a2, $sp, 0xd8
/* 096C70 7F062140 C7A000F4 */  lwc1  $f0, 0xf4($sp)
/* 096C74 7F062144 C7A800D0 */  lwc1  $f8, 0xd0($sp)
/* 096C78 7F062148 C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 096C7C 7F06214C 46004182 */  mul.s $f6, $f8, $f0
/* 096C80 7F062150 C7A800D8 */  lwc1  $f8, 0xd8($sp)
/* 096C84 7F062154 46005102 */  mul.s $f4, $f10, $f0
/* 096C88 7F062158 E7A600D0 */  swc1  $f6, 0xd0($sp)
/* 096C8C 7F06215C 46004182 */  mul.s $f6, $f8, $f0
/* 096C90 7F062160 E7A400D4 */  swc1  $f4, 0xd4($sp)
/* 096C94 7F062164 10000005 */  b     .L7F06217C
/* 096C98 7F062168 E7A600D8 */   swc1  $f6, 0xd8($sp)
.L7F06216C:
/* 096C9C 7F06216C C7AA00F4 */  lwc1  $f10, 0xf4($sp)
/* 096CA0 7F062170 E7AE00D0 */  swc1  $f14, 0xd0($sp)
/* 096CA4 7F062174 E7AE00D8 */  swc1  $f14, 0xd8($sp)
/* 096CA8 7F062178 E7AA00D4 */  swc1  $f10, 0xd4($sp)
.L7F06217C:
/* 096CAC 7F06217C C6240014 */  lwc1  $f4, 0x14($s1)
/* 096CB0 7F062180 C7A800D8 */  lwc1  $f8, 0xd8($sp)
/* 096CB4 7F062184 C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 096CB8 7F062188 27A400DC */  addiu $a0, $sp, 0xdc
/* 096CBC 7F06218C 46082182 */  mul.s $f6, $f4, $f8
/* 096CC0 7F062190 C6240018 */  lwc1  $f4, 0x18($s1)
/* 096CC4 7F062194 27A500E0 */  addiu $a1, $sp, 0xe0
/* 096CC8 7F062198 27A600E4 */  addiu $a2, $sp, 0xe4
/* 096CCC 7F06219C 46045102 */  mul.s $f4, $f10, $f4
/* 096CD0 7F0621A0 46043181 */  sub.s $f6, $f6, $f4
/* 096CD4 7F0621A4 E7A600DC */  swc1  $f6, 0xdc($sp)
/* 096CD8 7F0621A8 C6240018 */  lwc1  $f4, 0x18($s1)
/* 096CDC 7F0621AC C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 096CE0 7F0621B0 E7AA0038 */  swc1  $f10, 0x38($sp)
/* 096CE4 7F0621B4 C62A0010 */  lwc1  $f10, 0x10($s1)
/* 096CE8 7F0621B8 46062102 */  mul.s $f4, $f4, $f6
/* 096CEC 7F0621BC 00000000 */  nop
/* 096CF0 7F0621C0 460A4202 */  mul.s $f8, $f8, $f10
/* 096CF4 7F0621C4 46082281 */  sub.s $f10, $f4, $f8
/* 096CF8 7F0621C8 C7A80038 */  lwc1  $f8, 0x38($sp)
/* 096CFC 7F0621CC E7AA00E0 */  swc1  $f10, 0xe0($sp)
/* 096D00 7F0621D0 C6240010 */  lwc1  $f4, 0x10($s1)
/* 096D04 7F0621D4 46082282 */  mul.s $f10, $f4, $f8
/* 096D08 7F0621D8 C6240014 */  lwc1  $f4, 0x14($s1)
/* 096D0C 7F0621DC 46043202 */  mul.s $f8, $f6, $f4
/* 096D10 7F0621E0 46085181 */  sub.s $f6, $f10, $f8
/* 096D14 7F0621E4 0C007DD4 */  jal   guNormalize
/* 096D18 7F0621E8 E7A600E4 */   swc1  $f6, 0xe4($sp)
/* 096D1C 7F0621EC C7A000F4 */  lwc1  $f0, 0xf4($sp)
/* 096D20 7F0621F0 C7A400DC */  lwc1  $f4, 0xdc($sp)
/* 096D24 7F0621F4 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 096D28 7F0621F8 24010016 */  li    $at, 22
/* 096D2C 7F0621FC 46002282 */  mul.s $f10, $f4, $f0
/* 096D30 7F062200 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 096D34 7F062204 46004182 */  mul.s $f6, $f8, $f0
/* 096D38 7F062208 E7AA00DC */  swc1  $f10, 0xdc($sp)
/* 096D3C 7F06220C 46002282 */  mul.s $f10, $f4, $f0
/* 096D40 7F062210 E7A600E0 */  swc1  $f6, 0xe0($sp)
/* 096D44 7F062214 E7AA00E4 */  swc1  $f10, 0xe4($sp)
/* 096D48 7F062218 822A0001 */  lb    $t2, 1($s1)
/* 096D4C 7F06221C 15410005 */  bne   $t2, $at, .L7F062234
/* 096D50 7F062220 00000000 */   nop
/* 096D54 7F062224 0FC2F5B1 */  jal   dynAllocate7F0BD6C4
/* 096D58 7F062228 24040008 */   li    $a0, 8
/* 096D5C 7F06222C 10000004 */  b     .L7F062240
/* 096D60 7F062230 00408025 */   move  $s0, $v0
.L7F062234:
/* 096D64 7F062234 0FC2F5B1 */  jal   dynAllocate7F0BD6C4
/* 096D68 7F062238 24040004 */   li    $a0, 4
/* 096D6C 7F06223C 00408025 */  move  $s0, $v0
.L7F062240:
/* 096D70 7F062240 0FC2F5B8 */  jal   dynAllocateMatrix
/* 096D74 7F062244 00000000 */   nop
/* 096D78 7F062248 AFA20158 */  sw    $v0, 0x158($sp)
/* 096D7C 7F06224C 27A400FC */  addiu $a0, $sp, 0xfc
/* 096D80 7F062250 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 096D84 7F062254 27A50118 */   addiu $a1, $sp, 0x118
/* 096D88 7F062258 3C018005 */  lui   $at, %hi(D_80053EB0)
/* 096D8C 7F06225C C42C3EB0 */  lwc1  $f12, %lo(D_80053EB0)($at)
/* 096D90 7F062260 0FC1629F */  jal   matrix_scalar_multiply
/* 096D94 7F062264 27A50118 */   addiu $a1, $sp, 0x118
/* 096D98 7F062268 8FA400A8 */  lw    $a0, 0xa8($sp)
/* 096D9C 7F06226C 0FC16026 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 096DA0 7F062270 27A50118 */   addiu $a1, $sp, 0x118
/* 096DA4 7F062274 27A40118 */  addiu $a0, $sp, 0x118
/* 096DA8 7F062278 0FC16327 */  jal   matrix_4x4_f32_to_s32
/* 096DAC 7F06227C 8FA50158 */   lw    $a1, 0x158($sp)
/* 096DB0 7F062280 27A20108 */  addiu $v0, $sp, 0x108
/* 096DB4 7F062284 8C410000 */  lw    $at, ($v0)
/* 096DB8 7F062288 AE010000 */  sw    $at, ($s0)
/* 096DBC 7F06228C 8C4D0004 */  lw    $t5, 4($v0)
/* 096DC0 7F062290 AE0D0004 */  sw    $t5, 4($s0)
/* 096DC4 7F062294 8C410008 */  lw    $at, 8($v0)
/* 096DC8 7F062298 AE010008 */  sw    $at, 8($s0)
/* 096DCC 7F06229C 8C4D000C */  lw    $t5, 0xc($v0)
/* 096DD0 7F0622A0 AE0D000C */  sw    $t5, 0xc($s0)
/* 096DD4 7F0622A4 8C410000 */  lw    $at, ($v0)
/* 096DD8 7F0622A8 AE010010 */  sw    $at, 0x10($s0)
/* 096DDC 7F0622AC 8C4E0004 */  lw    $t6, 4($v0)
/* 096DE0 7F0622B0 AE0E0014 */  sw    $t6, 0x14($s0)
/* 096DE4 7F0622B4 8C410008 */  lw    $at, 8($v0)
/* 096DE8 7F0622B8 AE010018 */  sw    $at, 0x18($s0)
/* 096DEC 7F0622BC 8C4E000C */  lw    $t6, 0xc($v0)
/* 096DF0 7F0622C0 AE0E001C */  sw    $t6, 0x1c($s0)
/* 096DF4 7F0622C4 8C410000 */  lw    $at, ($v0)
/* 096DF8 7F0622C8 AE010020 */  sw    $at, 0x20($s0)
/* 096DFC 7F0622CC 8C4A0004 */  lw    $t2, 4($v0)
/* 096E00 7F0622D0 AE0A0024 */  sw    $t2, 0x24($s0)
/* 096E04 7F0622D4 8C410008 */  lw    $at, 8($v0)
/* 096E08 7F0622D8 AE010028 */  sw    $at, 0x28($s0)
/* 096E0C 7F0622DC 8C4A000C */  lw    $t2, 0xc($v0)
/* 096E10 7F0622E0 AE0A002C */  sw    $t2, 0x2c($s0)
/* 096E14 7F0622E4 8C410000 */  lw    $at, ($v0)
/* 096E18 7F0622E8 AE010030 */  sw    $at, 0x30($s0)
/* 096E1C 7F0622EC 8C4D0004 */  lw    $t5, 4($v0)
/* 096E20 7F0622F0 AE0D0034 */  sw    $t5, 0x34($s0)
/* 096E24 7F0622F4 8C410008 */  lw    $at, 8($v0)
/* 096E28 7F0622F8 AE010038 */  sw    $at, 0x38($s0)
/* 096E2C 7F0622FC 8C4D000C */  lw    $t5, 0xc($v0)
/* 096E30 7F062300 24010016 */  li    $at, 22
/* 096E34 7F062304 AE0D003C */  sw    $t5, 0x3c($s0)
/* 096E38 7F062308 82230001 */  lb    $v1, 1($s1)
/* 096E3C 7F06230C 54610023 */  bnel  $v1, $at, .L7F06239C
/* 096E40 7F062310 24010017 */   li    $at, 23
/* 096E44 7F062314 8C410000 */  lw    $at, ($v0)
/* 096E48 7F062318 AE010040 */  sw    $at, 0x40($s0)
/* 096E4C 7F06231C 8C4B0004 */  lw    $t3, 4($v0)
/* 096E50 7F062320 AE0B0044 */  sw    $t3, 0x44($s0)
/* 096E54 7F062324 8C410008 */  lw    $at, 8($v0)
/* 096E58 7F062328 AE010048 */  sw    $at, 0x48($s0)
/* 096E5C 7F06232C 8C4B000C */  lw    $t3, 0xc($v0)
/* 096E60 7F062330 AE0B004C */  sw    $t3, 0x4c($s0)
/* 096E64 7F062334 8C410000 */  lw    $at, ($v0)
/* 096E68 7F062338 AE010050 */  sw    $at, 0x50($s0)
/* 096E6C 7F06233C 8C580004 */  lw    $t8, 4($v0)
/* 096E70 7F062340 AE180054 */  sw    $t8, 0x54($s0)
/* 096E74 7F062344 8C410008 */  lw    $at, 8($v0)
/* 096E78 7F062348 AE010058 */  sw    $at, 0x58($s0)
/* 096E7C 7F06234C 8C58000C */  lw    $t8, 0xc($v0)
/* 096E80 7F062350 AE18005C */  sw    $t8, 0x5c($s0)
/* 096E84 7F062354 8C410000 */  lw    $at, ($v0)
/* 096E88 7F062358 AE010060 */  sw    $at, 0x60($s0)
/* 096E8C 7F06235C 8C4A0004 */  lw    $t2, 4($v0)
/* 096E90 7F062360 AE0A0064 */  sw    $t2, 0x64($s0)
/* 096E94 7F062364 8C410008 */  lw    $at, 8($v0)
/* 096E98 7F062368 AE010068 */  sw    $at, 0x68($s0)
/* 096E9C 7F06236C 8C4A000C */  lw    $t2, 0xc($v0)
/* 096EA0 7F062370 AE0A006C */  sw    $t2, 0x6c($s0)
/* 096EA4 7F062374 8C410000 */  lw    $at, ($v0)
/* 096EA8 7F062378 AE010070 */  sw    $at, 0x70($s0)
/* 096EAC 7F06237C 8C4F0004 */  lw    $t7, 4($v0)
/* 096EB0 7F062380 AE0F0074 */  sw    $t7, 0x74($s0)
/* 096EB4 7F062384 8C410008 */  lw    $at, 8($v0)
/* 096EB8 7F062388 AE010078 */  sw    $at, 0x78($s0)
/* 096EBC 7F06238C 8C4F000C */  lw    $t7, 0xc($v0)
/* 096EC0 7F062390 AE0F007C */  sw    $t7, 0x7c($s0)
/* 096EC4 7F062394 82230001 */  lb    $v1, 1($s1)
/* 096EC8 7F062398 24010017 */  li    $at, 23
.L7F06239C:
/* 096ECC 7F06239C 5461004F */  bnel  $v1, $at, .L7F0624DC
/* 096ED0 7F0623A0 3C014120 */   lui   $at, 0x4120
/* 096ED4 7F0623A4 C6280010 */  lwc1  $f8, 0x10($s1)
/* 096ED8 7F0623A8 C7A400FC */  lwc1  $f4, 0xfc($sp)
/* 096EDC 7F0623AC 8FA400A8 */  lw    $a0, 0xa8($sp)
/* 096EE0 7F0623B0 46144182 */  mul.s $f6, $f8, $f20
/* 096EE4 7F0623B4 27A5009C */  addiu $a1, $sp, 0x9c
/* 096EE8 7F0623B8 46043280 */  add.s $f10, $f6, $f4
/* 096EEC 7F0623BC C7A40100 */  lwc1  $f4, 0x100($sp)
/* 096EF0 7F0623C0 E7AA009C */  swc1  $f10, 0x9c($sp)
/* 096EF4 7F0623C4 C6280014 */  lwc1  $f8, 0x14($s1)
/* 096EF8 7F0623C8 46144182 */  mul.s $f6, $f8, $f20
/* 096EFC 7F0623CC 46043280 */  add.s $f10, $f6, $f4
/* 096F00 7F0623D0 C7A40104 */  lwc1  $f4, 0x104($sp)
/* 096F04 7F0623D4 E7AA00A0 */  swc1  $f10, 0xa0($sp)
/* 096F08 7F0623D8 C6280018 */  lwc1  $f8, 0x18($s1)
/* 096F0C 7F0623DC 46144182 */  mul.s $f6, $f8, $f20
/* 096F10 7F0623E0 46043280 */  add.s $f10, $f6, $f4
/* 096F14 7F0623E4 0FC1611D */  jal   mtx4TransformVecInPlace
/* 096F18 7F0623E8 E7AA00A4 */   swc1  $f10, 0xa4($sp)
/* 096F1C 7F0623EC 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 096F20 7F0623F0 44813000 */  mtc1  $at, $f6
/* 096F24 7F0623F4 C7A800F4 */  lwc1  $f8, 0xf4($sp)
/* 096F28 7F0623F8 C7AE00A4 */  lwc1  $f14, 0xa4($sp)
/* 096F2C 7F0623FC 27A40088 */  addiu $a0, $sp, 0x88
/* 096F30 7F062400 46064003 */  div.s $f0, $f8, $f6
/* 096F34 7F062404 27A60090 */  addiu $a2, $sp, 0x90
/* 096F38 7F062408 46007087 */  neg.s $f2, $f14
/* 096F3C 7F06240C 44051000 */  mfc1  $a1, $f2
/* 096F40 7F062410 E7A0008C */  swc1  $f0, 0x8c($sp)
/* 096F44 7F062414 0FC1E03C */  jal   divide3DCoordinates
/* 096F48 7F062418 E7A00088 */   swc1  $f0, 0x88($sp)
/* 096F4C 7F06241C 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 096F50 7F062420 C7B00090 */  lwc1  $f16, 0x90($sp)
/* 096F54 7F062424 44812000 */  mtc1  $at, $f4
/* 096F58 7F062428 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 096F5C 7F06242C 4604803C */  c.lt.s $f16, $f4
/* 096F60 7F062430 00000000 */  nop
/* 096F64 7F062434 4500000E */  bc1f  .L7F062470
/* 096F68 7F062438 00000000 */   nop
/* 096F6C 7F06243C 44815000 */  mtc1  $at, $f10
/* 096F70 7F062440 C7A2009C */  lwc1  $f2, 0x9c($sp)
/* 096F74 7F062444 C7AC00A0 */  lwc1  $f12, 0xa0($sp)
/* 096F78 7F062448 460A8002 */  mul.s $f0, $f16, $f10
/* 096F7C 7F06244C C7AE00A4 */  lwc1  $f14, 0xa4($sp)
/* 096F80 7F062450 46001082 */  mul.s $f2, $f2, $f0
/* 096F84 7F062454 00000000 */  nop
/* 096F88 7F062458 46006302 */  mul.s $f12, $f12, $f0
/* 096F8C 7F06245C 00000000 */  nop
/* 096F90 7F062460 46007382 */  mul.s $f14, $f14, $f0
/* 096F94 7F062464 E7A2009C */  swc1  $f2, 0x9c($sp)
/* 096F98 7F062468 E7AC00A0 */  swc1  $f12, 0xa0($sp)
/* 096F9C 7F06246C E7AE00A4 */  swc1  $f14, 0xa4($sp)
.L7F062470:
/* 096FA0 7F062470 0FC1E111 */  jal   currentPlayerGetMatrix10D4
/* 096FA4 7F062474 00000000 */   nop
/* 096FA8 7F062478 00402025 */  move  $a0, $v0
/* 096FAC 7F06247C 0FC1611D */  jal   mtx4TransformVecInPlace
/* 096FB0 7F062480 27A5009C */   addiu $a1, $sp, 0x9c
/* 096FB4 7F062484 C7A2009C */  lwc1  $f2, 0x9c($sp)
/* 096FB8 7F062488 C7A800FC */  lwc1  $f8, 0xfc($sp)
/* 096FBC 7F06248C C7AC00A0 */  lwc1  $f12, 0xa0($sp)
/* 096FC0 7F062490 C7A60100 */  lwc1  $f6, 0x100($sp)
/* 096FC4 7F062494 46081081 */  sub.s $f2, $f2, $f8
/* 096FC8 7F062498 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 096FCC 7F06249C 44810000 */  mtc1  $at, $f0
/* 096FD0 7F0624A0 46066301 */  sub.s $f12, $f12, $f6
/* 096FD4 7F0624A4 C7AE00A4 */  lwc1  $f14, 0xa4($sp)
/* 096FD8 7F0624A8 C7A40104 */  lwc1  $f4, 0x104($sp)
/* 096FDC 7F0624AC 46001282 */  mul.s $f10, $f2, $f0
/* 096FE0 7F0624B0 E7AC00A0 */  swc1  $f12, 0xa0($sp)
/* 096FE4 7F0624B4 46047381 */  sub.s $f14, $f14, $f4
/* 096FE8 7F0624B8 46006202 */  mul.s $f8, $f12, $f0
/* 096FEC 7F0624BC E7A2009C */  swc1  $f2, 0x9c($sp)
/* 096FF0 7F0624C0 46007182 */  mul.s $f6, $f14, $f0
/* 096FF4 7F0624C4 E7AA00C4 */  swc1  $f10, 0xc4($sp)
/* 096FF8 7F0624C8 E7AE00A4 */  swc1  $f14, 0xa4($sp)
/* 096FFC 7F0624CC E7A800C8 */  swc1  $f8, 0xc8($sp)
/* 097000 7F0624D0 1000000E */  b     .L7F06250C
/* 097004 7F0624D4 E7A600CC */   swc1  $f6, 0xcc($sp)
/* 097008 7F0624D8 3C014120 */  li    $at, 0x41200000 # 10.000000
.L7F0624DC:
/* 09700C 7F0624DC 44812000 */  mtc1  $at, $f4
/* 097010 7F0624E0 C62A0010 */  lwc1  $f10, 0x10($s1)
/* 097014 7F0624E4 4604A002 */  mul.s $f0, $f20, $f4
/* 097018 7F0624E8 00000000 */  nop
/* 09701C 7F0624EC 46005202 */  mul.s $f8, $f10, $f0
/* 097020 7F0624F0 E7A800C4 */  swc1  $f8, 0xc4($sp)
/* 097024 7F0624F4 C6260014 */  lwc1  $f6, 0x14($s1)
/* 097028 7F0624F8 46003102 */  mul.s $f4, $f6, $f0
/* 09702C 7F0624FC E7A400C8 */  swc1  $f4, 0xc8($sp)
/* 097030 7F062500 C62A0018 */  lwc1  $f10, 0x18($s1)
/* 097034 7F062504 46005202 */  mul.s $f8, $f10, $f0
/* 097038 7F062508 E7A800CC */  swc1  $f8, 0xcc($sp)
.L7F06250C:
/* 09703C 7F06250C C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 097040 7F062510 8FA500B0 */  lw    $a1, 0xb0($sp)
/* 097044 7F062514 3C018005 */  lui   $at, %hi(D_80053EB4)
/* 097048 7F062518 4600310D */  trunc.w.s $f4, $f6
/* 09704C 7F06251C 44192000 */  mfc1  $t9, $f4
/* 097050 7F062520 00000000 */  nop
/* 097054 7F062524 A6190000 */  sh    $t9, ($s0)
/* 097058 7F062528 C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 09705C 7F06252C 4600520D */  trunc.w.s $f8, $f10
/* 097060 7F062530 440E4000 */  mfc1  $t6, $f8
/* 097064 7F062534 00000000 */  nop
/* 097068 7F062538 A60E0002 */  sh    $t6, 2($s0)
/* 09706C 7F06253C C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 097070 7F062540 4600310D */  trunc.w.s $f4, $f6
/* 097074 7F062544 44092000 */  mfc1  $t1, $f4
/* 097078 7F062548 00000000 */  nop
/* 09707C 7F06254C A6090004 */  sh    $t1, 4($s0)
/* 097080 7F062550 90AA0004 */  lbu   $t2, 4($a1)
/* 097084 7F062554 A600000A */  sh    $zero, 0xa($s0)
/* 097088 7F062558 000A6140 */  sll   $t4, $t2, 5
/* 09708C 7F06255C A60C0008 */  sh    $t4, 8($s0)
/* 097090 7F062560 C7AA00D0 */  lwc1  $f10, 0xd0($sp)
/* 097094 7F062564 46005207 */  neg.s $f8, $f10
/* 097098 7F062568 4600418D */  trunc.w.s $f6, $f8
/* 09709C 7F06256C 440D3000 */  mfc1  $t5, $f6
/* 0970A0 7F062570 00000000 */  nop
/* 0970A4 7F062574 A60D0010 */  sh    $t5, 0x10($s0)
/* 0970A8 7F062578 C7A400D4 */  lwc1  $f4, 0xd4($sp)
/* 0970AC 7F06257C 46002287 */  neg.s $f10, $f4
/* 0970B0 7F062580 4600520D */  trunc.w.s $f8, $f10
/* 0970B4 7F062584 440B4000 */  mfc1  $t3, $f8
/* 0970B8 7F062588 00000000 */  nop
/* 0970BC 7F06258C A60B0012 */  sh    $t3, 0x12($s0)
/* 0970C0 7F062590 C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 0970C4 7F062594 A6000018 */  sh    $zero, 0x18($s0)
/* 0970C8 7F062598 A600001A */  sh    $zero, 0x1a($s0)
/* 0970CC 7F06259C 46003107 */  neg.s $f4, $f6
/* 0970D0 7F0625A0 4600228D */  trunc.w.s $f10, $f4
/* 0970D4 7F0625A4 44185000 */  mfc1  $t8, $f10
/* 0970D8 7F0625A8 00000000 */  nop
/* 0970DC 7F0625AC A6180014 */  sh    $t8, 0x14($s0)
/* 0970E0 7F0625B0 C7A800D0 */  lwc1  $f8, 0xd0($sp)
/* 0970E4 7F0625B4 C4263EB4 */  lwc1  $f6, %lo(D_80053EB4)($at)
/* 0970E8 7F0625B8 C7AA00C4 */  lwc1  $f10, 0xc4($sp)
/* 0970EC 7F0625BC 3C018005 */  lui   $at, %hi(D_80053EB8)
/* 0970F0 7F0625C0 46064102 */  mul.s $f4, $f8, $f6
/* 0970F4 7F0625C4 460A2200 */  add.s $f8, $f4, $f10
/* 0970F8 7F0625C8 4600418D */  trunc.w.s $f6, $f8
/* 0970FC 7F0625CC 440A3000 */  mfc1  $t2, $f6
/* 097100 7F0625D0 00000000 */  nop
/* 097104 7F0625D4 A60A0020 */  sh    $t2, 0x20($s0)
/* 097108 7F0625D8 C7A400D4 */  lwc1  $f4, 0xd4($sp)
/* 09710C 7F0625DC C42A3EB8 */  lwc1  $f10, %lo(D_80053EB8)($at)
/* 097110 7F0625E0 C7A600C8 */  lwc1  $f6, 0xc8($sp)
/* 097114 7F0625E4 3C018005 */  lui   $at, %hi(D_80053EBC)
/* 097118 7F0625E8 460A2202 */  mul.s $f8, $f4, $f10
/* 09711C 7F0625EC 46064100 */  add.s $f4, $f8, $f6
/* 097120 7F0625F0 4600228D */  trunc.w.s $f10, $f4
/* 097124 7F0625F4 440F5000 */  mfc1  $t7, $f10
/* 097128 7F0625F8 00000000 */  nop
/* 09712C 7F0625FC A60F0022 */  sh    $t7, 0x22($s0)
/* 097130 7F062600 C7A800D8 */  lwc1  $f8, 0xd8($sp)
/* 097134 7F062604 C4263EBC */  lwc1  $f6, %lo(D_80053EBC)($at)
/* 097138 7F062608 C7AA00CC */  lwc1  $f10, 0xcc($sp)
/* 09713C 7F06260C 3C018005 */  lui   $at, %hi(D_80053EC0)
/* 097140 7F062610 46064102 */  mul.s $f4, $f8, $f6
/* 097144 7F062614 460A2200 */  add.s $f8, $f4, $f10
/* 097148 7F062618 4600418D */  trunc.w.s $f6, $f8
/* 09714C 7F06261C 44193000 */  mfc1  $t9, $f6
/* 097150 7F062620 00000000 */  nop
/* 097154 7F062624 A6190024 */  sh    $t9, 0x24($s0)
/* 097158 7F062628 90AB0004 */  lbu   $t3, 4($a1)
/* 09715C 7F06262C 000B7140 */  sll   $t6, $t3, 5
/* 097160 7F062630 A60E0028 */  sh    $t6, 0x28($s0)
/* 097164 7F062634 90B80005 */  lbu   $t8, 5($a1)
/* 097168 7F062638 00184940 */  sll   $t1, $t8, 5
/* 09716C 7F06263C A609002A */  sh    $t1, 0x2a($s0)
/* 097170 7F062640 C42A3EC0 */  lwc1  $f10, %lo(D_80053EC0)($at)
/* 097174 7F062644 C7A400D0 */  lwc1  $f4, 0xd0($sp)
/* 097178 7F062648 C7A600C4 */  lwc1  $f6, 0xc4($sp)
/* 09717C 7F06264C 3C018005 */  lui   $at, %hi(D_80053EC4)
/* 097180 7F062650 460A2202 */  mul.s $f8, $f4, $f10
/* 097184 7F062654 46083101 */  sub.s $f4, $f6, $f8
/* 097188 7F062658 4600228D */  trunc.w.s $f10, $f4
/* 09718C 7F06265C 440C5000 */  mfc1  $t4, $f10
/* 097190 7F062660 00000000 */  nop
/* 097194 7F062664 A60C0030 */  sh    $t4, 0x30($s0)
/* 097198 7F062668 C4283EC4 */  lwc1  $f8, %lo(D_80053EC4)($at)
/* 09719C 7F06266C C7A600D4 */  lwc1  $f6, 0xd4($sp)
/* 0971A0 7F062670 C7AA00C8 */  lwc1  $f10, 0xc8($sp)
/* 0971A4 7F062674 3C018005 */  lui   $at, %hi(D_80053EC8)
/* 0971A8 7F062678 46083102 */  mul.s $f4, $f6, $f8
/* 0971AC 7F06267C 46045181 */  sub.s $f6, $f10, $f4
/* 0971B0 7F062680 4600320D */  trunc.w.s $f8, $f6
/* 0971B4 7F062684 440D4000 */  mfc1  $t5, $f8
/* 0971B8 7F062688 00000000 */  nop
/* 0971BC 7F06268C A60D0032 */  sh    $t5, 0x32($s0)
/* 0971C0 7F062690 C4243EC8 */  lwc1  $f4, %lo(D_80053EC8)($at)
/* 0971C4 7F062694 C7AA00D8 */  lwc1  $f10, 0xd8($sp)
/* 0971C8 7F062698 C7A800CC */  lwc1  $f8, 0xcc($sp)
/* 0971CC 7F06269C A6000038 */  sh    $zero, 0x38($s0)
/* 0971D0 7F0626A0 46045182 */  mul.s $f6, $f10, $f4
/* 0971D4 7F0626A4 24010016 */  li    $at, 22
/* 0971D8 7F0626A8 46064281 */  sub.s $f10, $f8, $f6
/* 0971DC 7F0626AC 4600510D */  trunc.w.s $f4, $f10
/* 0971E0 7F0626B0 440B2000 */  mfc1  $t3, $f4
/* 0971E4 7F0626B4 00000000 */  nop
/* 0971E8 7F0626B8 A60B0034 */  sh    $t3, 0x34($s0)
/* 0971EC 7F0626BC 90AE0005 */  lbu   $t6, 5($a1)
/* 0971F0 7F0626C0 000EC140 */  sll   $t8, $t6, 5
/* 0971F4 7F0626C4 A618003A */  sh    $t8, 0x3a($s0)
/* 0971F8 7F0626C8 82290001 */  lb    $t1, 1($s1)
/* 0971FC 7F0626CC C7A800FC */  lwc1  $f8, 0xfc($sp)
/* 097200 7F0626D0 8FAA00F8 */  lw    $t2, 0xf8($sp)
/* 097204 7F0626D4 552100AD */  bnel  $t1, $at, .L7F06298C
/* 097208 7F0626D8 8FAC0160 */   lw    $t4, 0x160($sp)
/* 09720C 7F0626DC C54C0000 */  lwc1  $f12, ($t2)
/* 097210 7F0626E0 C5420004 */  lwc1  $f2, 4($t2)
/* 097214 7F0626E4 C7A60100 */  lwc1  $f6, 0x100($sp)
/* 097218 7F0626E8 46086381 */  sub.s $f14, $f12, $f8
/* 09721C 7F0626EC C5400008 */  lwc1  $f0, 8($t2)
/* 097220 7F0626F0 E7A80038 */  swc1  $f8, 0x38($sp)
/* 097224 7F0626F4 46061401 */  sub.s $f16, $f2, $f6
/* 097228 7F0626F8 460E7102 */  mul.s $f4, $f14, $f14
/* 09722C 7F0626FC C7AA0104 */  lwc1  $f10, 0x104($sp)
/* 097230 7F062700 3C018005 */  lui   $at, %hi(D_80053ECC)
/* 097234 7F062704 46108202 */  mul.s $f8, $f16, $f16
/* 097238 7F062708 460A0481 */  sub.s $f18, $f0, $f10
/* 09723C 7F06270C 46082100 */  add.s $f4, $f4, $f8
/* 097240 7F062710 46129202 */  mul.s $f8, $f18, $f18
/* 097244 7F062714 46082100 */  add.s $f4, $f4, $f8
/* 097248 7F062718 E7A40078 */  swc1  $f4, 0x78($sp)
/* 09724C 7F06271C C6280010 */  lwc1  $f8, 0x10($s1)
/* 097250 7F062720 E7A60034 */  swc1  $f6, 0x34($sp)
/* 097254 7F062724 C7A60038 */  lwc1  $f6, 0x38($sp)
/* 097258 7F062728 46144202 */  mul.s $f8, $f8, $f20
/* 09725C 7F06272C 46064200 */  add.s $f8, $f8, $f6
/* 097260 7F062730 C6260014 */  lwc1  $f6, 0x14($s1)
/* 097264 7F062734 46086381 */  sub.s $f14, $f12, $f8
/* 097268 7F062738 46143202 */  mul.s $f8, $f6, $f20
/* 09726C 7F06273C C7A60034 */  lwc1  $f6, 0x34($sp)
/* 097270 7F062740 46064200 */  add.s $f8, $f8, $f6
/* 097274 7F062744 C6260018 */  lwc1  $f6, 0x18($s1)
/* 097278 7F062748 46081401 */  sub.s $f16, $f2, $f8
/* 09727C 7F06274C 46143202 */  mul.s $f8, $f6, $f20
/* 097280 7F062750 460A4180 */  add.s $f6, $f8, $f10
/* 097284 7F062754 460E7202 */  mul.s $f8, $f14, $f14
/* 097288 7F062758 00000000 */  nop
/* 09728C 7F06275C 46108282 */  mul.s $f10, $f16, $f16
/* 097290 7F062760 46060481 */  sub.s $f18, $f0, $f6
/* 097294 7F062764 460A4180 */  add.s $f6, $f8, $f10
/* 097298 7F062768 46129202 */  mul.s $f8, $f18, $f18
/* 09729C 7F06276C 46083280 */  add.s $f10, $f6, $f8
/* 0972A0 7F062770 C7A600C4 */  lwc1  $f6, 0xc4($sp)
/* 0972A4 7F062774 4604503C */  c.lt.s $f10, $f4
/* 0972A8 7F062778 00000000 */  nop
/* 0972AC 7F06277C 4500000B */  bc1f  .L7F0627AC
/* 0972B0 7F062780 00000000 */   nop
/* 0972B4 7F062784 C4243ECC */  lwc1  $f4, %lo(D_80053ECC)($at)
/* 0972B8 7F062788 E7A600B8 */  swc1  $f6, 0xb8($sp)
/* 0972BC 7F06278C 3C018005 */  lui   $at, %hi(D_80053ED0)
/* 0972C0 7F062790 C4263ED0 */  lwc1  $f6, %lo(D_80053ED0)($at)
/* 0972C4 7F062794 C7A800C8 */  lwc1  $f8, 0xc8($sp)
/* 0972C8 7F062798 C7AA00CC */  lwc1  $f10, 0xcc($sp)
/* 0972CC 7F06279C 46062002 */  mul.s $f0, $f4, $f6
/* 0972D0 7F0627A0 E7A800BC */  swc1  $f8, 0xbc($sp)
/* 0972D4 7F0627A4 E7AA00C0 */  swc1  $f10, 0xc0($sp)
/* 0972D8 7F0627A8 E7A000B4 */  swc1  $f0, 0xb4($sp)
.L7F0627AC:
/* 0972DC 7F0627AC C7A000B4 */  lwc1  $f0, 0xb4($sp)
/* 0972E0 7F0627B0 C7A800DC */  lwc1  $f8, 0xdc($sp)
/* 0972E4 7F0627B4 C7A400B8 */  lwc1  $f4, 0xb8($sp)
/* 0972E8 7F0627B8 3C088009 */  lui   $t0, %hi(flareimage5)
/* 0972EC 7F0627BC 46004282 */  mul.s $f10, $f8, $f0
/* 0972F0 7F0627C0 2508D0D8 */  addiu $t0, %lo(flareimage5) # addiu $t0, $t0, -0x2f28
/* 0972F4 7F0627C4 46045180 */  add.s $f6, $f10, $f4
/* 0972F8 7F0627C8 4600320D */  trunc.w.s $f8, $f6
/* 0972FC 7F0627CC 440F4000 */  mfc1  $t7, $f8
/* 097300 7F0627D0 00000000 */  nop
/* 097304 7F0627D4 A60F0040 */  sh    $t7, 0x40($s0)
/* 097308 7F0627D8 C7AA00E0 */  lwc1  $f10, 0xe0($sp)
/* 09730C 7F0627DC C7A600BC */  lwc1  $f6, 0xbc($sp)
/* 097310 7F0627E0 46005102 */  mul.s $f4, $f10, $f0
/* 097314 7F0627E4 46062200 */  add.s $f8, $f4, $f6
/* 097318 7F0627E8 4600428D */  trunc.w.s $f10, $f8
/* 09731C 7F0627EC 44195000 */  mfc1  $t9, $f10
/* 097320 7F0627F0 00000000 */  nop
/* 097324 7F0627F4 A6190042 */  sh    $t9, 0x42($s0)
/* 097328 7F0627F8 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 09732C 7F0627FC C7A800C0 */  lwc1  $f8, 0xc0($sp)
/* 097330 7F062800 46002182 */  mul.s $f6, $f4, $f0
/* 097334 7F062804 46083280 */  add.s $f10, $f6, $f8
/* 097338 7F062808 4600510D */  trunc.w.s $f4, $f10
/* 09733C 7F06280C 440E2000 */  mfc1  $t6, $f4
/* 097340 7F062810 00000000 */  nop
/* 097344 7F062814 A60E0044 */  sh    $t6, 0x44($s0)
/* 097348 7F062818 8D180000 */  lw    $t8, ($t0)
/* 09734C 7F06281C 93090004 */  lbu   $t1, 4($t8)
/* 097350 7F062820 00095140 */  sll   $t2, $t1, 5
/* 097354 7F062824 A60A0048 */  sh    $t2, 0x48($s0)
/* 097358 7F062828 8D0C0000 */  lw    $t4, ($t0)
/* 09735C 7F06282C 918F0005 */  lbu   $t7, 5($t4)
/* 097360 7F062830 000F6940 */  sll   $t5, $t7, 5
/* 097364 7F062834 A60D004A */  sh    $t5, 0x4a($s0)
/* 097368 7F062838 C7A800DC */  lwc1  $f8, 0xdc($sp)
/* 09736C 7F06283C C7A600B8 */  lwc1  $f6, 0xb8($sp)
/* 097370 7F062840 46004282 */  mul.s $f10, $f8, $f0
/* 097374 7F062844 460A3101 */  sub.s $f4, $f6, $f10
/* 097378 7F062848 4600220D */  trunc.w.s $f8, $f4
/* 09737C 7F06284C 440B4000 */  mfc1  $t3, $f8
/* 097380 7F062850 00000000 */  nop
/* 097384 7F062854 A60B0050 */  sh    $t3, 0x50($s0)
/* 097388 7F062858 C7AA00E0 */  lwc1  $f10, 0xe0($sp)
/* 09738C 7F06285C C7A600BC */  lwc1  $f6, 0xbc($sp)
/* 097390 7F062860 46005102 */  mul.s $f4, $f10, $f0
/* 097394 7F062864 46043201 */  sub.s $f8, $f6, $f4
/* 097398 7F062868 4600428D */  trunc.w.s $f10, $f8
/* 09739C 7F06286C 44185000 */  mfc1  $t8, $f10
/* 0973A0 7F062870 00000000 */  nop
/* 0973A4 7F062874 A6180052 */  sh    $t8, 0x52($s0)
/* 0973A8 7F062878 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 0973AC 7F06287C C7A600C0 */  lwc1  $f6, 0xc0($sp)
/* 0973B0 7F062880 A6000058 */  sh    $zero, 0x58($s0)
/* 0973B4 7F062884 46002202 */  mul.s $f8, $f4, $f0
/* 0973B8 7F062888 A600005A */  sh    $zero, 0x5a($s0)
/* 0973BC 7F06288C 46083281 */  sub.s $f10, $f6, $f8
/* 0973C0 7F062890 4600510D */  trunc.w.s $f4, $f10
/* 0973C4 7F062894 440A2000 */  mfc1  $t2, $f4
/* 0973C8 7F062898 00000000 */  nop
/* 0973CC 7F06289C A60A0054 */  sh    $t2, 0x54($s0)
/* 0973D0 7F0628A0 C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 0973D4 7F0628A4 C7AA00B8 */  lwc1  $f10, 0xb8($sp)
/* 0973D8 7F0628A8 46003202 */  mul.s $f8, $f6, $f0
/* 0973DC 7F0628AC 460A4100 */  add.s $f4, $f8, $f10
/* 0973E0 7F0628B0 4600218D */  trunc.w.s $f6, $f4
/* 0973E4 7F0628B4 440F3000 */  mfc1  $t7, $f6
/* 0973E8 7F0628B8 00000000 */  nop
/* 0973EC 7F0628BC A60F0060 */  sh    $t7, 0x60($s0)
/* 0973F0 7F0628C0 C7A800D4 */  lwc1  $f8, 0xd4($sp)
/* 0973F4 7F0628C4 C7A400BC */  lwc1  $f4, 0xbc($sp)
/* 0973F8 7F0628C8 46004282 */  mul.s $f10, $f8, $f0
/* 0973FC 7F0628CC 46045180 */  add.s $f6, $f10, $f4
/* 097400 7F0628D0 4600320D */  trunc.w.s $f8, $f6
/* 097404 7F0628D4 44194000 */  mfc1  $t9, $f8
/* 097408 7F0628D8 00000000 */  nop
/* 09740C 7F0628DC A6190062 */  sh    $t9, 0x62($s0)
/* 097410 7F0628E0 C7AA00D8 */  lwc1  $f10, 0xd8($sp)
/* 097414 7F0628E4 C7A600C0 */  lwc1  $f6, 0xc0($sp)
/* 097418 7F0628E8 A6000068 */  sh    $zero, 0x68($s0)
/* 09741C 7F0628EC 46005102 */  mul.s $f4, $f10, $f0
/* 097420 7F0628F0 46062200 */  add.s $f8, $f4, $f6
/* 097424 7F0628F4 4600428D */  trunc.w.s $f10, $f8
/* 097428 7F0628F8 440E5000 */  mfc1  $t6, $f10
/* 09742C 7F0628FC 00000000 */  nop
/* 097430 7F062900 A60E0064 */  sh    $t6, 0x64($s0)
/* 097434 7F062904 8D180000 */  lw    $t8, ($t0)
/* 097438 7F062908 93090005 */  lbu   $t1, 5($t8)
/* 09743C 7F06290C 00095140 */  sll   $t2, $t1, 5
/* 097440 7F062910 A60A006A */  sh    $t2, 0x6a($s0)
/* 097444 7F062914 C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 097448 7F062918 C7A400B8 */  lwc1  $f4, 0xb8($sp)
/* 09744C 7F06291C 46003202 */  mul.s $f8, $f6, $f0
/* 097450 7F062920 46082281 */  sub.s $f10, $f4, $f8
/* 097454 7F062924 4600518D */  trunc.w.s $f6, $f10
/* 097458 7F062928 440F3000 */  mfc1  $t7, $f6
/* 09745C 7F06292C 00000000 */  nop
/* 097460 7F062930 A60F0070 */  sh    $t7, 0x70($s0)
/* 097464 7F062934 C7A800D4 */  lwc1  $f8, 0xd4($sp)
/* 097468 7F062938 C7A400BC */  lwc1  $f4, 0xbc($sp)
/* 09746C 7F06293C 46004282 */  mul.s $f10, $f8, $f0
/* 097470 7F062940 460A2181 */  sub.s $f6, $f4, $f10
/* 097474 7F062944 4600320D */  trunc.w.s $f8, $f6
/* 097478 7F062948 44194000 */  mfc1  $t9, $f8
/* 09747C 7F06294C 00000000 */  nop
/* 097480 7F062950 A6190072 */  sh    $t9, 0x72($s0)
/* 097484 7F062954 C7AA00D8 */  lwc1  $f10, 0xd8($sp)
/* 097488 7F062958 C7A400C0 */  lwc1  $f4, 0xc0($sp)
/* 09748C 7F06295C 46005182 */  mul.s $f6, $f10, $f0
/* 097490 7F062960 46062201 */  sub.s $f8, $f4, $f6
/* 097494 7F062964 4600428D */  trunc.w.s $f10, $f8
/* 097498 7F062968 440E5000 */  mfc1  $t6, $f10
/* 09749C 7F06296C 00000000 */  nop
/* 0974A0 7F062970 A60E0074 */  sh    $t6, 0x74($s0)
/* 0974A4 7F062974 8D180000 */  lw    $t8, ($t0)
/* 0974A8 7F062978 93090004 */  lbu   $t1, 4($t8)
/* 0974AC 7F06297C A600007A */  sh    $zero, 0x7a($s0)
/* 0974B0 7F062980 00095140 */  sll   $t2, $t1, 5
/* 0974B4 7F062984 A60A0078 */  sh    $t2, 0x78($s0)
/* 0974B8 7F062988 8FAC0160 */  lw    $t4, 0x160($sp)
.L7F06298C:
/* 0974BC 7F06298C 3C0DB600 */  lui   $t5, 0xb600
/* 0974C0 7F062990 24192000 */  li    $t9, 8192
/* 0974C4 7F062994 258F0008 */  addiu $t7, $t4, 8
/* 0974C8 7F062998 AFAF0160 */  sw    $t7, 0x160($sp)
/* 0974CC 7F06299C AD990004 */  sw    $t9, 4($t4)
/* 0974D0 7F0629A0 AD8D0000 */  sw    $t5, ($t4)
/* 0974D4 7F0629A4 8FAB0160 */  lw    $t3, 0x160($sp)
/* 0974D8 7F0629A8 3C180102 */  lui   $t8, (0x01020040 >> 16) # lui $t8, 0x102
/* 0974DC 7F0629AC 37180040 */  ori   $t8, (0x01020040 & 0xFFFF) # ori $t8, $t8, 0x40
/* 0974E0 7F0629B0 256E0008 */  addiu $t6, $t3, 8
/* 0974E4 7F0629B4 AFAE0160 */  sw    $t6, 0x160($sp)
/* 0974E8 7F0629B8 AD780000 */  sw    $t8, ($t3)
/* 0974EC 7F0629BC 8FA40158 */  lw    $a0, 0x158($sp)
/* 0974F0 7F0629C0 0C003A2C */  jal   osVirtualToPhysical
/* 0974F4 7F0629C4 AFAB006C */   sw    $t3, 0x6c($sp)
/* 0974F8 7F0629C8 8FA3006C */  lw    $v1, 0x6c($sp)
/* 0974FC 7F0629CC 3C088009 */  lui   $t0, %hi(flareimage5)
/* 097500 7F0629D0 24010016 */  li    $at, 22
/* 097504 7F0629D4 AC620004 */  sw    $v0, 4($v1)
/* 097508 7F0629D8 82290001 */  lb    $t1, 1($s1)
/* 09750C 7F0629DC 2508D0D8 */  addiu $t0, %lo(flareimage5) # addiu $t0, $t0, -0x2f28
/* 097510 7F0629E0 8FA500B0 */  lw    $a1, 0xb0($sp)
/* 097514 7F0629E4 15210029 */  bne   $t1, $at, .L7F062A8C
/* 097518 7F0629E8 27A40160 */   addiu $a0, $sp, 0x160
/* 09751C 7F0629EC 240A0002 */  li    $t2, 2
/* 097520 7F0629F0 AFAA0010 */  sw    $t2, 0x10($sp)
/* 097524 7F0629F4 27A40160 */  addiu $a0, $sp, 0x160
/* 097528 7F0629F8 8D050000 */  lw    $a1, ($t0)
/* 09752C 7F0629FC 24060004 */  li    $a2, 4
/* 097530 7F062A00 0FC1DB5A */  jal   texSelect
/* 097534 7F062A04 8FA70168 */   lw    $a3, 0x168($sp)
/* 097538 7F062A08 8FB10160 */  lw    $s1, 0x160($sp)
/* 09753C 7F062A0C 3C0D0470 */  lui   $t5, (0x04700080 >> 16) # lui $t5, 0x470
/* 097540 7F062A10 35AD0080 */  ori   $t5, (0x04700080 & 0xFFFF) # ori $t5, $t5, 0x80
/* 097544 7F062A14 262F0008 */  addiu $t7, $s1, 8
/* 097548 7F062A18 AFAF0160 */  sw    $t7, 0x160($sp)
/* 09754C 7F062A1C 02002025 */  move  $a0, $s0
/* 097550 7F062A20 0C003A2C */  jal   osVirtualToPhysical
/* 097554 7F062A24 AE2D0000 */   sw    $t5, ($s1)
/* 097558 7F062A28 AE220004 */  sw    $v0, 4($s1)
/* 09755C 7F062A2C 8FB90160 */  lw    $t9, 0x160($sp)
/* 097560 7F062A30 3C0EB100 */  lui   $t6, (0xB1000076 >> 16) # lui $t6, 0xb100          # gSP4Triangles(8,8,6,5,1,7,2,0,0,0,0,0
/* 097564 7F062A34 35CE0076 */  ori   $t6, (0xB1000076 & 0xFFFF) # ori $t6, $t6, 0x76
/* 097568 7F062A38 272B0008 */  addiu $t3, $t9, 8
/* 09756C 7F062A3C AFAB0160 */  sw    $t3, 0x160($sp)
/* 097570 7F062A40 24185454 */  li    $t8, 21588
/* 097574 7F062A44 AF380004 */  sw    $t8, 4($t9)                                       # ),
/* 097578 7F062A48 AF2E0000 */  sw    $t6, ($t9)
/* 09757C 7F062A4C 24090002 */  li    $t1, 2
/* 097580 7F062A50 AFA90010 */  sw    $t1, 0x10($sp)
/* 097584 7F062A54 8FA70168 */  lw    $a3, 0x168($sp)
/* 097588 7F062A58 8FA500B0 */  lw    $a1, 0xb0($sp)
/* 09758C 7F062A5C 27A40160 */  addiu $a0, $sp, 0x160
/* 097590 7F062A60 0FC1DB5A */  jal   texSelect
/* 097594 7F062A64 24060004 */   li    $a2, 4
/* 097598 7F062A68 8FAA0160 */  lw    $t2, 0x160($sp)
/* 09759C 7F062A6C 3C0FB100 */  lui   $t7, (0xB1000013 >> 16) # lui $t7, 0xb100          # gSP4Triangles(0,2,3,3,2,1,1,0,0,0,0
/* 0975A0 7F062A70 35EF0013 */  ori   $t7, (0xB1000013 & 0xFFFF) # ori $t7, $t7, 0x13
/* 0975A4 7F062A74 254C0008 */  addiu $t4, $t2, 8
/* 0975A8 7F062A78 AFAC0160 */  sw    $t4, 0x160($sp)
/* 0975AC 7F062A7C 240D3020 */  li    $t5, 12320
/* 0975B0 7F062A80 AD4D0004 */  sw    $t5, 4($t2)
/* 0975B4 7F062A84 10000017 */  b     .L7F062AE4
/* 0975B8 7F062A88 AD4F0000 */   sw    $t7, ($t2)                                        # ),
.L7F062A8C:
/* 0975BC 7F062A8C 24190002 */  li    $t9, 2
/* 0975C0 7F062A90 AFB90010 */  sw    $t9, 0x10($sp)
/* 0975C4 7F062A94 24060004 */  li    $a2, 4
/* 0975C8 7F062A98 0FC1DB5A */  jal   texSelect
/* 0975CC 7F062A9C 8FA70168 */   lw    $a3, 0x168($sp)
/* 0975D0 7F062AA0 8FB10160 */  lw    $s1, 0x160($sp)
/* 0975D4 7F062AA4 3C180430 */  lui   $t8, (0x04300040 >> 16) # lui $t8, 0x430
/* 0975D8 7F062AA8 37180040 */  ori   $t8, (0x04300040 & 0xFFFF) # ori $t8, $t8, 0x40
/* 0975DC 7F062AAC 262E0008 */  addiu $t6, $s1, 8
/* 0975E0 7F062AB0 AFAE0160 */  sw    $t6, 0x160($sp)
/* 0975E4 7F062AB4 02002025 */  move  $a0, $s0
/* 0975E8 7F062AB8 0C003A2C */  jal   osVirtualToPhysical
/* 0975EC 7F062ABC AE380000 */   sw    $t8, ($s1)
/* 0975F0 7F062AC0 AE220004 */  sw    $v0, 4($s1)
/* 0975F4 7F062AC4 8FA90160 */  lw    $t1, 0x160($sp)
/* 0975F8 7F062AC8 3C0CB100 */  lui   $t4, (0xB1000013 >> 16) # lui $t4, 0xb100
/* 0975FC 7F062ACC 358C0013 */  ori   $t4, (0xB1000013 & 0xFFFF) # ori $t4, $t4, 0x13
/* 097600 7F062AD0 252A0008 */  addiu $t2, $t1, 8
/* 097604 7F062AD4 AFAA0160 */  sw    $t2, 0x160($sp)
/* 097608 7F062AD8 240F3020 */  li    $t7, 12320
/* 09760C 7F062ADC AD2F0004 */  sw    $t7, 4($t1)
/* 097610 7F062AE0 AD2C0000 */  sw    $t4, ($t1)
.L7F062AE4:
/* 097614 7F062AE4 8FBF002C */  lw    $ra, 0x2c($sp)
/* 097618 7F062AE8 8FA20160 */  lw    $v0, 0x160($sp)
/* 09761C 7F062AEC D7B40018 */  ldc1  $f20, 0x18($sp)
/* 097620 7F062AF0 8FB00024 */  lw    $s0, 0x24($sp)
/* 097624 7F062AF4 8FB10028 */  lw    $s1, 0x28($sp)
/* 097628 7F062AF8 03E00008 */  jr    $ra
/* 09762C 7F062AFC 27BD0160 */   addiu $sp, $sp, 0x160
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.late_rodata
glabel D_80053EAC
.word 0x3fb50481 /*1.4141999*/
glabel D_80053EB0
.word 0x3dcccccd /*0.1*/
glabel D_80053EB4
.word 0x3f666666 /*0.89999998*/
glabel D_80053EB8
.word 0x3f666666 /*0.89999998*/
glabel D_80053EBC
.word 0x3f666666 /*0.89999998*/
glabel D_80053EC0
.word 0x3f666666 /*0.89999998*/
glabel D_80053EC4
.word 0x3f666666 /*0.89999998*/
glabel D_80053EC8
.word 0x3f666666 /*0.89999998*/
glabel D_80053ECC
.word 0x3fb50481 /*1.4141999*/
glabel D_80053ED0
.word 0x3f666666 /*0.89999998*/
.text
glabel sub_GAME_7F061E18
/* 094CCC 7F0622DC 27BDFEA0 */  addiu $sp, $sp, -0x160
/* 094CD0 7F0622E0 AFBF002C */  sw    $ra, 0x2c($sp)
/* 094CD4 7F0622E4 AFB10028 */  sw    $s1, 0x28($sp)
/* 094CD8 7F0622E8 AFB00024 */  sw    $s0, 0x24($sp)
/* 094CDC 7F0622EC F7B40018 */  sdc1  $f20, 0x18($sp)
/* 094CE0 7F0622F0 AFA40160 */  sw    $a0, 0x160($sp)
/* 094CE4 7F0622F4 AFA60168 */  sw    $a2, 0x168($sp)
/* 094CE8 7F0622F8 80AB0000 */  lb    $t3, ($a1)
/* 094CEC 7F0622FC 3C0E8003 */  lui   $t6, %hi(D_80035C98) # $t6, 0x8003
/* 094CF0 7F062300 00A08825 */  move  $s1, $a1
/* 094CF4 7F062304 05600328 */  bltz  $t3, .L7F062FA8
/* 094CF8 7F062308 25CE11E8 */   addiu $t6, %lo(D_80035C98) # addiu $t6, $t6, 0x11e8
/* 094CFC 7F06230C 8DC10000 */  lw    $at, ($t6)
/* 094D00 7F062310 8DD90004 */  lw    $t9, 4($t6)
/* 094D04 7F062314 27A90108 */  addiu $t1, $sp, 0x108
/* 094D08 7F062318 AD210000 */  sw    $at, ($t1)
/* 094D0C 7F06231C AD390004 */  sw    $t9, 4($t1)
/* 094D10 7F062320 8DD9000C */  lw    $t9, 0xc($t6)
/* 094D14 7F062324 8DC10008 */  lw    $at, 8($t6)
/* 094D18 7F062328 AD39000C */  sw    $t9, 0xc($t1)
/* 094D1C 7F06232C 0FC22868 */  jal   bondviewGetCurrentPlayersPosition
/* 094D20 7F062330 AD210008 */   sw    $at, 8($t1)
/* 094D24 7F062334 AFA200F8 */  sw    $v0, 0xf8($sp)
/* 094D28 7F062338 3C0D8003 */  lui   $t5, %hi(D_80035CA8) # $t5, 0x8003
/* 094D2C 7F06233C 25AD11F8 */  addiu $t5, %lo(D_80035CA8) # addiu $t5, $t5, 0x11f8
/* 094D30 7F062340 8DA10000 */  lw    $at, ($t5)
/* 094D34 7F062344 C6200028 */  lwc1  $f0, 0x28($s1)
/* 094D38 7F062348 C6340024 */  lwc1  $f20, 0x24($s1)
/* 094D3C 7F06234C 27AF00C4 */  addiu $t7, $sp, 0xc4
/* 094D40 7F062350 ADE10000 */  sw    $at, ($t7)
/* 094D44 7F062354 8DA10008 */  lw    $at, 8($t5)
/* 094D48 7F062358 8DAB0004 */  lw    $t3, 4($t5)
/* 094D4C 7F06235C 3C098003 */  lui   $t1, %hi(D_80035CB4) # $t1, 0x8003
/* 094D50 7F062360 25291204 */  addiu $t1, %lo(D_80035CB4) # addiu $t1, $t1, 0x1204
/* 094D54 7F062364 ADE10008 */  sw    $at, 8($t7)
/* 094D58 7F062368 ADEB0004 */  sw    $t3, 4($t7)
/* 094D5C 7F06236C 8D210000 */  lw    $at, ($t1)
/* 094D60 7F062370 27B800B8 */  addiu $t8, $sp, 0xb8
/* 094D64 7F062374 8D2A0004 */  lw    $t2, 4($t1)
/* 094D68 7F062378 AF010000 */  sw    $at, ($t8)
/* 094D6C 7F06237C 8D210008 */  lw    $at, 8($t1)
/* 094D70 7F062380 AF0A0004 */  sw    $t2, 4($t8)
/* 094D74 7F062384 3C0C8007 */  lui   $t4, %hi(flareimage3) # $t4, 0x8007
/* 094D78 7F062388 AF010008 */  sw    $at, 8($t8)
/* 094D7C 7F06238C 3C018005 */  lui   $at, %hi(D_80053EAC) # $at, 0x8005
/* 094D80 7F062390 C4249FEC */  lwc1  $f4, %lo(D_80053EAC)($at)
/* 094D84 7F062394 8D8C44B0 */  lw    $t4, %lo(flareimage3)($t4)
/* 094D88 7F062398 E7A000E8 */  swc1  $f0, 0xe8($sp)
/* 094D8C 7F06239C E7A400B4 */  swc1  $f4, 0xb4($sp)
/* 094D90 7F0623A0 0FC1E111 */  jal   camGetWorldToScreenMtxf
/* 094D94 7F0623A4 AFAC00B0 */   sw    $t4, 0xb0($sp)
/* 094D98 7F0623A8 AFA200A8 */  sw    $v0, 0xa8($sp)
/* 094D9C 7F0623AC 82230001 */  lb    $v1, 1($s1)
/* 094DA0 7F0623B0 24010016 */  li    $at, 22
/* 094DA4 7F0623B4 C7A000E8 */  lwc1  $f0, 0xe8($sp)
/* 094DA8 7F0623B8 14610007 */  bne   $v1, $at, .L7F0623D8
/* 094DAC 7F0623BC 3C014248 */   li    $at, 0x42480000 # 50.000000
/* 094DB0 7F0623C0 44819000 */  mtc1  $at, $f18
/* 094DB4 7F0623C4 3C0F8007 */  lui   $t7, %hi(flareimage4) # $t7, 0x8007
/* 094DB8 7F0623C8 8DEF44B4 */  lw    $t7, %lo(flareimage4)($t7)
/* 094DBC 7F0623CC E7B200F4 */  swc1  $f18, 0xf4($sp)
/* 094DC0 7F0623D0 10000026 */  b     .L7F06246C
/* 094DC4 7F0623D4 AFAF00B0 */   sw    $t7, 0xb0($sp)
.L7F0623D8:
/* 094DC8 7F0623D8 24010017 */  li    $at, 23
/* 094DCC 7F0623DC 1461001F */  bne   $v1, $at, .L7F06245C
/* 094DD0 7F0623E0 3C0D8007 */   lui   $t5, %hi(flareimage4) # $t5, 0x8007
/* 094DD4 7F0623E4 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 094DD8 7F0623E8 44813000 */  mtc1  $at, $f6
/* 094DDC 7F0623EC 8DAD44B4 */  lw    $t5, %lo(flareimage4)($t5)
/* 094DE0 7F0623F0 E7A000E8 */  swc1  $f0, 0xe8($sp)
/* 094DE4 7F0623F4 E7A600F4 */  swc1  $f6, 0xf4($sp)
/* 094DE8 7F0623F8 0C00262C */  jal   randomGetNext
/* 094DEC 7F0623FC AFAD00B0 */   sw    $t5, 0xb0($sp)
/* 094DF0 7F062400 24010032 */  li    $at, 50
/* 094DF4 7F062404 0041001B */  divu  $zero, $v0, $at
/* 094DF8 7F062408 00005810 */  mfhi  $t3
/* 094DFC 7F06240C 25790096 */  addiu $t9, $t3, 0x96
/* 094E00 7F062410 0C00262C */  jal   randomGetNext
/* 094E04 7F062414 A3B90117 */   sb    $t9, 0x117($sp)
/* 094E08 7F062418 24010005 */  li    $at, 5
/* 094E0C 7F06241C 0041001B */  divu  $zero, $v0, $at
/* 094E10 7F062420 00007010 */  mfhi  $t6
/* 094E14 7F062424 C7A000E8 */  lwc1  $f0, 0xe8($sp)
/* 094E18 7F062428 55C00011 */  bnezl $t6, .L7F062470
/* 094E1C 7F06242C C6240004 */   lwc1  $f4, 4($s1)
/* 094E20 7F062430 0C00262C */  jal   randomGetNext
/* 094E24 7F062434 E7A000E8 */   swc1  $f0, 0xe8($sp)
/* 094E28 7F062438 24010064 */  li    $at, 100
/* 094E2C 7F06243C 0041001B */  divu  $zero, $v0, $at
/* 094E30 7F062440 0000C010 */  mfhi  $t8
/* 094E34 7F062444 240900FF */  li    $t1, 255
/* 094E38 7F062448 01381823 */  subu  $v1, $t1, $t8
/* 094E3C 7F06244C A3A30115 */  sb    $v1, 0x115($sp)
/* 094E40 7F062450 A3A30114 */  sb    $v1, 0x114($sp)
/* 094E44 7F062454 10000005 */  b     .L7F06246C
/* 094E48 7F062458 C7A000E8 */   lwc1  $f0, 0xe8($sp)
.L7F06245C:
/* 094E4C 7F06245C 3C0141F0 */  li    $at, 0x41F00000 # 30.000000
/* 094E50 7F062460 44815000 */  mtc1  $at, $f10
/* 094E54 7F062464 00000000 */  nop
/* 094E58 7F062468 E7AA00F4 */  swc1  $f10, 0xf4($sp)
.L7F06246C:
/* 094E5C 7F06246C C6240004 */  lwc1  $f4, 4($s1)
.L7F062470:
/* 094E60 7F062470 44807000 */  mtc1  $zero, $f14
/* 094E64 7F062474 E7A400FC */  swc1  $f4, 0xfc($sp)
/* 094E68 7F062478 C6280008 */  lwc1  $f8, 8($s1)
/* 094E6C 7F06247C 4600703C */  c.lt.s $f14, $f0
/* 094E70 7F062480 E7A80100 */  swc1  $f8, 0x100($sp)
/* 094E74 7F062484 C626000C */  lwc1  $f6, 0xc($s1)
/* 094E78 7F062488 45000011 */  bc1f  .L7F0624D0
/* 094E7C 7F06248C E7A60104 */   swc1  $f6, 0x104($sp)
/* 094E80 7F062490 C6240010 */  lwc1  $f4, 0x10($s1)
/* 094E84 7F062494 C7AA00FC */  lwc1  $f10, 0xfc($sp)
/* 094E88 7F062498 46040202 */  mul.s $f8, $f0, $f4
/* 094E8C 7F06249C C7A40100 */  lwc1  $f4, 0x100($sp)
/* 094E90 7F0624A0 46085180 */  add.s $f6, $f10, $f8
/* 094E94 7F0624A4 E7A600FC */  swc1  $f6, 0xfc($sp)
/* 094E98 7F0624A8 C62A0014 */  lwc1  $f10, 0x14($s1)
/* 094E9C 7F0624AC 460A0202 */  mul.s $f8, $f0, $f10
/* 094EA0 7F0624B0 C7AA0104 */  lwc1  $f10, 0x104($sp)
/* 094EA4 7F0624B4 46082180 */  add.s $f6, $f4, $f8
/* 094EA8 7F0624B8 E7A60100 */  swc1  $f6, 0x100($sp)
/* 094EAC 7F0624BC C6240018 */  lwc1  $f4, 0x18($s1)
/* 094EB0 7F0624C0 46040202 */  mul.s $f8, $f0, $f4
/* 094EB4 7F0624C4 46085180 */  add.s $f6, $f10, $f8
/* 094EB8 7F0624C8 10000003 */  b     .L7F0624D8
/* 094EBC 7F0624CC E7A60104 */   swc1  $f6, 0x104($sp)
.L7F0624D0:
/* 094EC0 7F0624D0 4600A500 */  add.s $f20, $f20, $f0
/* 094EC4 7F0624D4 46007006 */  mov.s $f0, $f14
.L7F0624D8:
/* 094EC8 7F0624D8 46140100 */  add.s $f4, $f0, $f20
/* 094ECC 7F0624DC C622001C */  lwc1  $f2, 0x1c($s1)
/* 094ED0 7F0624E0 4604103C */  c.lt.s $f2, $f4
/* 094ED4 7F0624E4 00000000 */  nop
/* 094ED8 7F0624E8 45020003 */  bc1fl .L7F0624F8
/* 094EDC 7F0624EC C62C0018 */   lwc1  $f12, 0x18($s1)
/* 094EE0 7F0624F0 46001501 */  sub.s $f20, $f2, $f0
/* 094EE4 7F0624F4 C62C0018 */  lwc1  $f12, 0x18($s1)
.L7F0624F8:
/* 094EE8 7F0624F8 C7AA0104 */  lwc1  $f10, 0x104($sp)
/* 094EEC 7F0624FC 8FA200F8 */  lw    $v0, 0xf8($sp)
/* 094EF0 7F062500 460CA202 */  mul.s $f8, $f20, $f12
/* 094EF4 7F062504 C6220014 */  lwc1  $f2, 0x14($s1)
/* 094EF8 7F062508 C4440008 */  lwc1  $f4, 8($v0)
/* 094EFC 7F06250C E7AA0030 */  swc1  $f10, 0x30($sp)
/* 094F00 7F062510 46085180 */  add.s $f6, $f10, $f8
/* 094F04 7F062514 C44A0004 */  lwc1  $f10, 4($v0)
/* 094F08 7F062518 46062201 */  sub.s $f8, $f4, $f6
/* 094F0C 7F06251C 46081102 */  mul.s $f4, $f2, $f8
/* 094F10 7F062520 C7A80100 */  lwc1  $f8, 0x100($sp)
/* 094F14 7F062524 46141182 */  mul.s $f6, $f2, $f20
/* 094F18 7F062528 46083180 */  add.s $f6, $f6, $f8
/* 094F1C 7F06252C 46065281 */  sub.s $f10, $f10, $f6
/* 094F20 7F062530 460C5182 */  mul.s $f6, $f10, $f12
/* 094F24 7F062534 46062281 */  sub.s $f10, $f4, $f6
/* 094F28 7F062538 C7A400FC */  lwc1  $f4, 0xfc($sp)
/* 094F2C 7F06253C E7AA00D0 */  swc1  $f10, 0xd0($sp)
/* 094F30 7F062540 C6200010 */  lwc1  $f0, 0x10($s1)
/* 094F34 7F062544 C62C0018 */  lwc1  $f12, 0x18($s1)
/* 094F38 7F062548 E7A80034 */  swc1  $f8, 0x34($sp)
/* 094F3C 7F06254C 4600A182 */  mul.s $f6, $f20, $f0
/* 094F40 7F062550 C4480000 */  lwc1  $f8, ($v0)
/* 094F44 7F062554 E7AA0038 */  swc1  $f10, 0x38($sp)
/* 094F48 7F062558 C7AA0030 */  lwc1  $f10, 0x30($sp)
/* 094F4C 7F06255C 46062180 */  add.s $f6, $f4, $f6
/* 094F50 7F062560 46064201 */  sub.s $f8, $f8, $f6
/* 094F54 7F062564 46086182 */  mul.s $f6, $f12, $f8
/* 094F58 7F062568 00000000 */  nop
/* 094F5C 7F06256C 46146202 */  mul.s $f8, $f12, $f20
/* 094F60 7F062570 460A4200 */  add.s $f8, $f8, $f10
/* 094F64 7F062574 C44A0008 */  lwc1  $f10, 8($v0)
/* 094F68 7F062578 46085281 */  sub.s $f10, $f10, $f8
/* 094F6C 7F06257C 46005202 */  mul.s $f8, $f10, $f0
/* 094F70 7F062580 46083281 */  sub.s $f10, $f6, $f8
/* 094F74 7F062584 C7A60034 */  lwc1  $f6, 0x34($sp)
/* 094F78 7F062588 E7AA00D4 */  swc1  $f10, 0xd4($sp)
/* 094F7C 7F06258C C6220014 */  lwc1  $f2, 0x14($s1)
/* 094F80 7F062590 C6200010 */  lwc1  $f0, 0x10($s1)
/* 094F84 7F062594 4602A202 */  mul.s $f8, $f20, $f2
/* 094F88 7F062598 46083180 */  add.s $f6, $f6, $f8
/* 094F8C 7F06259C C4480004 */  lwc1  $f8, 4($v0)
/* 094F90 7F0625A0 46064201 */  sub.s $f8, $f8, $f6
/* 094F94 7F0625A4 46080182 */  mul.s $f6, $f0, $f8
/* 094F98 7F0625A8 00000000 */  nop
/* 094F9C 7F0625AC 46140202 */  mul.s $f8, $f0, $f20
/* 094FA0 7F0625B0 46044200 */  add.s $f8, $f8, $f4
/* 094FA4 7F0625B4 C4440000 */  lwc1  $f4, ($v0)
/* 094FA8 7F0625B8 46082101 */  sub.s $f4, $f4, $f8
/* 094FAC 7F0625BC 46022202 */  mul.s $f8, $f4, $f2
/* 094FB0 7F0625C0 46083101 */  sub.s $f4, $f6, $f8
/* 094FB4 7F0625C4 C7A60038 */  lwc1  $f6, 0x38($sp)
/* 094FB8 7F0625C8 46067032 */  c.eq.s $f14, $f6
/* 094FBC 7F0625CC E7A400D8 */  swc1  $f4, 0xd8($sp)
/* 094FC0 7F0625D0 45000008 */  bc1f  .L7F0625F4
/* 094FC4 7F0625D4 00000000 */   nop
/* 094FC8 7F0625D8 460A7032 */  c.eq.s $f14, $f10
/* 094FCC 7F0625DC 00000000 */  nop
/* 094FD0 7F0625E0 45020005 */  bc1fl .L7F0625F8
/* 094FD4 7F0625E4 27A400D0 */   addiu $a0, $sp, 0xd0
/* 094FD8 7F0625E8 46047032 */  c.eq.s $f14, $f4
/* 094FDC 7F0625EC 00000000 */  nop
/* 094FE0 7F0625F0 4501000F */  bc1t  .L7F062630
.L7F0625F4:
/* 094FE4 7F0625F4 27A400D0 */   addiu $a0, $sp, 0xd0
.L7F0625F8:
/* 094FE8 7F0625F8 27A500D4 */  addiu $a1, $sp, 0xd4
/* 094FEC 7F0625FC 0C0075F0 */  jal   guNormalize
/* 094FF0 7F062600 27A600D8 */   addiu $a2, $sp, 0xd8
/* 094FF4 7F062604 C7A000F4 */  lwc1  $f0, 0xf4($sp)
/* 094FF8 7F062608 C7A800D0 */  lwc1  $f8, 0xd0($sp)
/* 094FFC 7F06260C C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 095000 7F062610 46004182 */  mul.s $f6, $f8, $f0
/* 095004 7F062614 C7A800D8 */  lwc1  $f8, 0xd8($sp)
/* 095008 7F062618 46005102 */  mul.s $f4, $f10, $f0
/* 09500C 7F06261C E7A600D0 */  swc1  $f6, 0xd0($sp)
/* 095010 7F062620 46004182 */  mul.s $f6, $f8, $f0
/* 095014 7F062624 E7A400D4 */  swc1  $f4, 0xd4($sp)
/* 095018 7F062628 10000005 */  b     .L7F062640
/* 09501C 7F06262C E7A600D8 */   swc1  $f6, 0xd8($sp)
.L7F062630:
/* 095020 7F062630 C7AA00F4 */  lwc1  $f10, 0xf4($sp)
/* 095024 7F062634 E7AE00D0 */  swc1  $f14, 0xd0($sp)
/* 095028 7F062638 E7AE00D8 */  swc1  $f14, 0xd8($sp)
/* 09502C 7F06263C E7AA00D4 */  swc1  $f10, 0xd4($sp)
.L7F062640:
/* 095030 7F062640 C6240014 */  lwc1  $f4, 0x14($s1)
/* 095034 7F062644 C7A800D8 */  lwc1  $f8, 0xd8($sp)
/* 095038 7F062648 C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 09503C 7F06264C 27A400DC */  addiu $a0, $sp, 0xdc
/* 095040 7F062650 46082182 */  mul.s $f6, $f4, $f8
/* 095044 7F062654 C6240018 */  lwc1  $f4, 0x18($s1)
/* 095048 7F062658 27A500E0 */  addiu $a1, $sp, 0xe0
/* 09504C 7F06265C 27A600E4 */  addiu $a2, $sp, 0xe4
/* 095050 7F062660 46045102 */  mul.s $f4, $f10, $f4
/* 095054 7F062664 46043181 */  sub.s $f6, $f6, $f4
/* 095058 7F062668 E7A600DC */  swc1  $f6, 0xdc($sp)
/* 09505C 7F06266C C6240018 */  lwc1  $f4, 0x18($s1)
/* 095060 7F062670 C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 095064 7F062674 E7AA0038 */  swc1  $f10, 0x38($sp)
/* 095068 7F062678 C62A0010 */  lwc1  $f10, 0x10($s1)
/* 09506C 7F06267C 46062102 */  mul.s $f4, $f4, $f6
/* 095070 7F062680 00000000 */  nop
/* 095074 7F062684 460A4202 */  mul.s $f8, $f8, $f10
/* 095078 7F062688 46082281 */  sub.s $f10, $f4, $f8
/* 09507C 7F06268C C7A80038 */  lwc1  $f8, 0x38($sp)
/* 095080 7F062690 E7AA00E0 */  swc1  $f10, 0xe0($sp)
/* 095084 7F062694 C6240010 */  lwc1  $f4, 0x10($s1)
/* 095088 7F062698 46082282 */  mul.s $f10, $f4, $f8
/* 09508C 7F06269C C6240014 */  lwc1  $f4, 0x14($s1)
/* 095090 7F0626A0 46043202 */  mul.s $f8, $f6, $f4
/* 095094 7F0626A4 46085181 */  sub.s $f6, $f10, $f8
/* 095098 7F0626A8 0C0075F0 */  jal   guNormalize
/* 09509C 7F0626AC E7A600E4 */   swc1  $f6, 0xe4($sp)
/* 0950A0 7F0626B0 C7A000F4 */  lwc1  $f0, 0xf4($sp)
/* 0950A4 7F0626B4 C7A400DC */  lwc1  $f4, 0xdc($sp)
/* 0950A8 7F0626B8 C7A800E0 */  lwc1  $f8, 0xe0($sp)
/* 0950AC 7F0626BC 24010016 */  li    $at, 22
/* 0950B0 7F0626C0 46002282 */  mul.s $f10, $f4, $f0
/* 0950B4 7F0626C4 C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 0950B8 7F0626C8 46004182 */  mul.s $f6, $f8, $f0
/* 0950BC 7F0626CC E7AA00DC */  swc1  $f10, 0xdc($sp)
/* 0950C0 7F0626D0 46002282 */  mul.s $f10, $f4, $f0
/* 0950C4 7F0626D4 E7A600E0 */  swc1  $f6, 0xe0($sp)
/* 0950C8 7F0626D8 E7AA00E4 */  swc1  $f10, 0xe4($sp)
/* 0950CC 7F0626DC 822A0001 */  lb    $t2, 1($s1)
/* 0950D0 7F0626E0 15410005 */  bne   $t2, $at, .L7F0626F8
/* 0950D4 7F0626E4 00000000 */   nop
/* 0950D8 7F0626E8 0FC2F29D */  jal   dynAllocate7F0BD6C4
/* 0950DC 7F0626EC 24040008 */   li    $a0, 8
/* 0950E0 7F0626F0 10000004 */  b     .L7F062704
/* 0950E4 7F0626F4 00408025 */   move  $s0, $v0
.L7F0626F8:
/* 0950E8 7F0626F8 0FC2F29D */  jal   dynAllocate7F0BD6C4
/* 0950EC 7F0626FC 24040004 */   li    $a0, 4
/* 0950F0 7F062700 00408025 */  move  $s0, $v0
.L7F062704:
/* 0950F4 7F062704 0FC2F2A4 */  jal   dynAllocateMatrix
/* 0950F8 7F062708 00000000 */   nop
/* 0950FC 7F06270C AFA20158 */  sw    $v0, 0x158($sp)
/* 095100 7F062710 27A400FC */  addiu $a0, $sp, 0xfc
/* 095104 7F062714 0FC16383 */  jal   matrix_4x4_set_identity_and_position
/* 095108 7F062718 27A50118 */   addiu $a1, $sp, 0x118
/* 09510C 7F06271C 3C018005 */  lui   $at, %hi(D_80053EB0) # $at, 0x8005
/* 095110 7F062720 C42C9FF0 */  lwc1  $f12, %lo(D_80053EB0)($at)
/* 095114 7F062724 0FC163C9 */  jal   matrix_scalar_multiply
/* 095118 7F062728 27A50118 */   addiu $a1, $sp, 0x118
/* 09511C 7F06272C 8FA400A8 */  lw    $a0, 0xa8($sp)
/* 095120 7F062730 0FC16150 */  jal   matrix_4x4_multiply_homogeneous_in_place
/* 095124 7F062734 27A50118 */   addiu $a1, $sp, 0x118
/* 095128 7F062738 27A40118 */  addiu $a0, $sp, 0x118
/* 09512C 7F06273C 0FC16451 */  jal   matrix_4x4_f32_to_s32
/* 095130 7F062740 8FA50158 */   lw    $a1, 0x158($sp)
/* 095134 7F062744 27A20108 */  addiu $v0, $sp, 0x108
/* 095138 7F062748 8C410000 */  lw    $at, ($v0)
/* 09513C 7F06274C AE010000 */  sw    $at, ($s0)
/* 095140 7F062750 8C4D0004 */  lw    $t5, 4($v0)
/* 095144 7F062754 AE0D0004 */  sw    $t5, 4($s0)
/* 095148 7F062758 8C410008 */  lw    $at, 8($v0)
/* 09514C 7F06275C AE010008 */  sw    $at, 8($s0)
/* 095150 7F062760 8C4D000C */  lw    $t5, 0xc($v0)
/* 095154 7F062764 AE0D000C */  sw    $t5, 0xc($s0)
/* 095158 7F062768 8C410000 */  lw    $at, ($v0)
/* 09515C 7F06276C AE010010 */  sw    $at, 0x10($s0)
/* 095160 7F062770 8C4E0004 */  lw    $t6, 4($v0)
/* 095164 7F062774 AE0E0014 */  sw    $t6, 0x14($s0)
/* 095168 7F062778 8C410008 */  lw    $at, 8($v0)
/* 09516C 7F06277C AE010018 */  sw    $at, 0x18($s0)
/* 095170 7F062780 8C4E000C */  lw    $t6, 0xc($v0)
/* 095174 7F062784 AE0E001C */  sw    $t6, 0x1c($s0)
/* 095178 7F062788 8C410000 */  lw    $at, ($v0)
/* 09517C 7F06278C AE010020 */  sw    $at, 0x20($s0)
/* 095180 7F062790 8C4A0004 */  lw    $t2, 4($v0)
/* 095184 7F062794 AE0A0024 */  sw    $t2, 0x24($s0)
/* 095188 7F062798 8C410008 */  lw    $at, 8($v0)
/* 09518C 7F06279C AE010028 */  sw    $at, 0x28($s0)
/* 095190 7F0627A0 8C4A000C */  lw    $t2, 0xc($v0)
/* 095194 7F0627A4 AE0A002C */  sw    $t2, 0x2c($s0)
/* 095198 7F0627A8 8C410000 */  lw    $at, ($v0)
/* 09519C 7F0627AC AE010030 */  sw    $at, 0x30($s0)
/* 0951A0 7F0627B0 8C4D0004 */  lw    $t5, 4($v0)
/* 0951A4 7F0627B4 AE0D0034 */  sw    $t5, 0x34($s0)
/* 0951A8 7F0627B8 8C410008 */  lw    $at, 8($v0)
/* 0951AC 7F0627BC AE010038 */  sw    $at, 0x38($s0)
/* 0951B0 7F0627C0 8C4D000C */  lw    $t5, 0xc($v0)
/* 0951B4 7F0627C4 24010016 */  li    $at, 22
/* 0951B8 7F0627C8 AE0D003C */  sw    $t5, 0x3c($s0)
/* 0951BC 7F0627CC 82230001 */  lb    $v1, 1($s1)
/* 0951C0 7F0627D0 54610023 */  bnel  $v1, $at, .L7F062860
/* 0951C4 7F0627D4 24010017 */   li    $at, 23
/* 0951C8 7F0627D8 8C410000 */  lw    $at, ($v0)
/* 0951CC 7F0627DC AE010040 */  sw    $at, 0x40($s0)
/* 0951D0 7F0627E0 8C4B0004 */  lw    $t3, 4($v0)
/* 0951D4 7F0627E4 AE0B0044 */  sw    $t3, 0x44($s0)
/* 0951D8 7F0627E8 8C410008 */  lw    $at, 8($v0)
/* 0951DC 7F0627EC AE010048 */  sw    $at, 0x48($s0)
/* 0951E0 7F0627F0 8C4B000C */  lw    $t3, 0xc($v0)
/* 0951E4 7F0627F4 AE0B004C */  sw    $t3, 0x4c($s0)
/* 0951E8 7F0627F8 8C410000 */  lw    $at, ($v0)
/* 0951EC 7F0627FC AE010050 */  sw    $at, 0x50($s0)
/* 0951F0 7F062800 8C580004 */  lw    $t8, 4($v0)
/* 0951F4 7F062804 AE180054 */  sw    $t8, 0x54($s0)
/* 0951F8 7F062808 8C410008 */  lw    $at, 8($v0)
/* 0951FC 7F06280C AE010058 */  sw    $at, 0x58($s0)
/* 095200 7F062810 8C58000C */  lw    $t8, 0xc($v0)
/* 095204 7F062814 AE18005C */  sw    $t8, 0x5c($s0)
/* 095208 7F062818 8C410000 */  lw    $at, ($v0)
/* 09520C 7F06281C AE010060 */  sw    $at, 0x60($s0)
/* 095210 7F062820 8C4A0004 */  lw    $t2, 4($v0)
/* 095214 7F062824 AE0A0064 */  sw    $t2, 0x64($s0)
/* 095218 7F062828 8C410008 */  lw    $at, 8($v0)
/* 09521C 7F06282C AE010068 */  sw    $at, 0x68($s0)
/* 095220 7F062830 8C4A000C */  lw    $t2, 0xc($v0)
/* 095224 7F062834 AE0A006C */  sw    $t2, 0x6c($s0)
/* 095228 7F062838 8C410000 */  lw    $at, ($v0)
/* 09522C 7F06283C AE010070 */  sw    $at, 0x70($s0)
/* 095230 7F062840 8C4F0004 */  lw    $t7, 4($v0)
/* 095234 7F062844 AE0F0074 */  sw    $t7, 0x74($s0)
/* 095238 7F062848 8C410008 */  lw    $at, 8($v0)
/* 09523C 7F06284C AE010078 */  sw    $at, 0x78($s0)
/* 095240 7F062850 8C4F000C */  lw    $t7, 0xc($v0)
/* 095244 7F062854 AE0F007C */  sw    $t7, 0x7c($s0)
/* 095248 7F062858 82230001 */  lb    $v1, 1($s1)
/* 09524C 7F06285C 24010017 */  li    $at, 23
.L7F062860:
/* 095250 7F062860 5461004F */  bnel  $v1, $at, .L7F0629A0
/* 095254 7F062864 3C014120 */   lui   $at, 0x4120
/* 095258 7F062868 C6280010 */  lwc1  $f8, 0x10($s1)
/* 09525C 7F06286C C7A400FC */  lwc1  $f4, 0xfc($sp)
/* 095260 7F062870 8FA400A8 */  lw    $a0, 0xa8($sp)
/* 095264 7F062874 46144182 */  mul.s $f6, $f8, $f20
/* 095268 7F062878 27A5009C */  addiu $a1, $sp, 0x9c
/* 09526C 7F06287C 46043280 */  add.s $f10, $f6, $f4
/* 095270 7F062880 C7A40100 */  lwc1  $f4, 0x100($sp)
/* 095274 7F062884 E7AA009C */  swc1  $f10, 0x9c($sp)
/* 095278 7F062888 C6280014 */  lwc1  $f8, 0x14($s1)
/* 09527C 7F06288C 46144182 */  mul.s $f6, $f8, $f20
/* 095280 7F062890 46043280 */  add.s $f10, $f6, $f4
/* 095284 7F062894 C7A40104 */  lwc1  $f4, 0x104($sp)
/* 095288 7F062898 E7AA00A0 */  swc1  $f10, 0xa0($sp)
/* 09528C 7F06289C C6280018 */  lwc1  $f8, 0x18($s1)
/* 095290 7F0628A0 46144182 */  mul.s $f6, $f8, $f20
/* 095294 7F0628A4 46043280 */  add.s $f10, $f6, $f4
/* 095298 7F0628A8 0FC16247 */  jal   mtx4TransformVecInPlace
/* 09529C 7F0628AC E7AA00A4 */   swc1  $f10, 0xa4($sp)
/* 0952A0 7F0628B0 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 0952A4 7F0628B4 44813000 */  mtc1  $at, $f6
/* 0952A8 7F0628B8 C7A800F4 */  lwc1  $f8, 0xf4($sp)
/* 0952AC 7F0628BC C7AE00A4 */  lwc1  $f14, 0xa4($sp)
/* 0952B0 7F0628C0 27A40088 */  addiu $a0, $sp, 0x88
/* 0952B4 7F0628C4 46064003 */  div.s $f0, $f8, $f6
/* 0952B8 7F0628C8 27A60090 */  addiu $a2, $sp, 0x90
/* 0952BC 7F0628CC 46007087 */  neg.s $f2, $f14
/* 0952C0 7F0628D0 44051000 */  mfc1  $a1, $f2
/* 0952C4 7F0628D4 E7A0008C */  swc1  $f0, 0x8c($sp)
/* 0952C8 7F0628D8 0FC1E05C */  jal   divide3DCoordinates
/* 0952CC 7F0628DC E7A00088 */   swc1  $f0, 0x88($sp)
/* 0952D0 7F0628E0 3C014000 */  li    $at, 0x40000000 # 2.000000
/* 0952D4 7F0628E4 C7B00090 */  lwc1  $f16, 0x90($sp)
/* 0952D8 7F0628E8 44812000 */  mtc1  $at, $f4
/* 0952DC 7F0628EC 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 0952E0 7F0628F0 4604803C */  c.lt.s $f16, $f4
/* 0952E4 7F0628F4 00000000 */  nop
/* 0952E8 7F0628F8 4500000E */  bc1f  .L7F062934
/* 0952EC 7F0628FC 00000000 */   nop
/* 0952F0 7F062900 44815000 */  mtc1  $at, $f10
/* 0952F4 7F062904 C7A2009C */  lwc1  $f2, 0x9c($sp)
/* 0952F8 7F062908 C7AC00A0 */  lwc1  $f12, 0xa0($sp)
/* 0952FC 7F06290C 460A8002 */  mul.s $f0, $f16, $f10
/* 095300 7F062910 C7AE00A4 */  lwc1  $f14, 0xa4($sp)
/* 095304 7F062914 46001082 */  mul.s $f2, $f2, $f0
/* 095308 7F062918 00000000 */  nop
/* 09530C 7F06291C 46006302 */  mul.s $f12, $f12, $f0
/* 095310 7F062920 00000000 */  nop
/* 095314 7F062924 46007382 */  mul.s $f14, $f14, $f0
/* 095318 7F062928 E7A2009C */  swc1  $f2, 0x9c($sp)
/* 09531C 7F06292C E7AC00A0 */  swc1  $f12, 0xa0($sp)
/* 095320 7F062930 E7AE00A4 */  swc1  $f14, 0xa4($sp)
.L7F062934:
/* 095324 7F062934 0FC1E131 */  jal   currentPlayerGetMatrix10D4
/* 095328 7F062938 00000000 */   nop
/* 09532C 7F06293C 00402025 */  move  $a0, $v0
/* 095330 7F062940 0FC16247 */  jal   mtx4TransformVecInPlace
/* 095334 7F062944 27A5009C */   addiu $a1, $sp, 0x9c
/* 095338 7F062948 C7A2009C */  lwc1  $f2, 0x9c($sp)
/* 09533C 7F06294C C7A800FC */  lwc1  $f8, 0xfc($sp)
/* 095340 7F062950 C7AC00A0 */  lwc1  $f12, 0xa0($sp)
/* 095344 7F062954 C7A60100 */  lwc1  $f6, 0x100($sp)
/* 095348 7F062958 46081081 */  sub.s $f2, $f2, $f8
/* 09534C 7F06295C 3C014120 */  li    $at, 0x41200000 # 10.000000
/* 095350 7F062960 44810000 */  mtc1  $at, $f0
/* 095354 7F062964 46066301 */  sub.s $f12, $f12, $f6
/* 095358 7F062968 C7AE00A4 */  lwc1  $f14, 0xa4($sp)
/* 09535C 7F06296C C7A40104 */  lwc1  $f4, 0x104($sp)
/* 095360 7F062970 46001282 */  mul.s $f10, $f2, $f0
/* 095364 7F062974 E7AC00A0 */  swc1  $f12, 0xa0($sp)
/* 095368 7F062978 46047381 */  sub.s $f14, $f14, $f4
/* 09536C 7F06297C 46006202 */  mul.s $f8, $f12, $f0
/* 095370 7F062980 E7A2009C */  swc1  $f2, 0x9c($sp)
/* 095374 7F062984 46007182 */  mul.s $f6, $f14, $f0
/* 095378 7F062988 E7AA00C4 */  swc1  $f10, 0xc4($sp)
/* 09537C 7F06298C E7AE00A4 */  swc1  $f14, 0xa4($sp)
/* 095380 7F062990 E7A800C8 */  swc1  $f8, 0xc8($sp)
/* 095384 7F062994 1000000E */  b     .L7F0629D0
/* 095388 7F062998 E7A600CC */   swc1  $f6, 0xcc($sp)
/* 09538C 7F06299C 3C014120 */  li    $at, 0x41200000 # 10.000000
.L7F0629A0:
/* 095390 7F0629A0 44812000 */  mtc1  $at, $f4
/* 095394 7F0629A4 C62A0010 */  lwc1  $f10, 0x10($s1)
/* 095398 7F0629A8 4604A002 */  mul.s $f0, $f20, $f4
/* 09539C 7F0629AC 00000000 */  nop
/* 0953A0 7F0629B0 46005202 */  mul.s $f8, $f10, $f0
/* 0953A4 7F0629B4 E7A800C4 */  swc1  $f8, 0xc4($sp)
/* 0953A8 7F0629B8 C6260014 */  lwc1  $f6, 0x14($s1)
/* 0953AC 7F0629BC 46003102 */  mul.s $f4, $f6, $f0
/* 0953B0 7F0629C0 E7A400C8 */  swc1  $f4, 0xc8($sp)
/* 0953B4 7F0629C4 C62A0018 */  lwc1  $f10, 0x18($s1)
/* 0953B8 7F0629C8 46005202 */  mul.s $f8, $f10, $f0
/* 0953BC 7F0629CC E7A800CC */  swc1  $f8, 0xcc($sp)
.L7F0629D0:
/* 0953C0 7F0629D0 C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 0953C4 7F0629D4 8FA500B0 */  lw    $a1, 0xb0($sp)
/* 0953C8 7F0629D8 3C018005 */  lui   $at, %hi(D_80053EB4) # $at, 0x8005
/* 0953CC 7F0629DC 4600310D */  trunc.w.s $f4, $f6
/* 0953D0 7F0629E0 44192000 */  mfc1  $t9, $f4
/* 0953D4 7F0629E4 00000000 */  nop
/* 0953D8 7F0629E8 A6190000 */  sh    $t9, ($s0)
/* 0953DC 7F0629EC C7AA00D4 */  lwc1  $f10, 0xd4($sp)
/* 0953E0 7F0629F0 4600520D */  trunc.w.s $f8, $f10
/* 0953E4 7F0629F4 440E4000 */  mfc1  $t6, $f8
/* 0953E8 7F0629F8 00000000 */  nop
/* 0953EC 7F0629FC A60E0002 */  sh    $t6, 2($s0)
/* 0953F0 7F062A00 C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 0953F4 7F062A04 4600310D */  trunc.w.s $f4, $f6
/* 0953F8 7F062A08 44092000 */  mfc1  $t1, $f4
/* 0953FC 7F062A0C 00000000 */  nop
/* 095400 7F062A10 A6090004 */  sh    $t1, 4($s0)
/* 095404 7F062A14 90AA0004 */  lbu   $t2, 4($a1)
/* 095408 7F062A18 A600000A */  sh    $zero, 0xa($s0)
/* 09540C 7F062A1C 000A6140 */  sll   $t4, $t2, 5
/* 095410 7F062A20 A60C0008 */  sh    $t4, 8($s0)
/* 095414 7F062A24 C7AA00D0 */  lwc1  $f10, 0xd0($sp)
/* 095418 7F062A28 46005207 */  neg.s $f8, $f10
/* 09541C 7F062A2C 4600418D */  trunc.w.s $f6, $f8
/* 095420 7F062A30 440D3000 */  mfc1  $t5, $f6
/* 095424 7F062A34 00000000 */  nop
/* 095428 7F062A38 A60D0010 */  sh    $t5, 0x10($s0)
/* 09542C 7F062A3C C7A400D4 */  lwc1  $f4, 0xd4($sp)
/* 095430 7F062A40 46002287 */  neg.s $f10, $f4
/* 095434 7F062A44 4600520D */  trunc.w.s $f8, $f10
/* 095438 7F062A48 440B4000 */  mfc1  $t3, $f8
/* 09543C 7F062A4C 00000000 */  nop
/* 095440 7F062A50 A60B0012 */  sh    $t3, 0x12($s0)
/* 095444 7F062A54 C7A600D8 */  lwc1  $f6, 0xd8($sp)
/* 095448 7F062A58 A6000018 */  sh    $zero, 0x18($s0)
/* 09544C 7F062A5C A600001A */  sh    $zero, 0x1a($s0)
/* 095450 7F062A60 46003107 */  neg.s $f4, $f6
/* 095454 7F062A64 4600228D */  trunc.w.s $f10, $f4
/* 095458 7F062A68 44185000 */  mfc1  $t8, $f10
/* 09545C 7F062A6C 00000000 */  nop
/* 095460 7F062A70 A6180014 */  sh    $t8, 0x14($s0)
/* 095464 7F062A74 C7A800D0 */  lwc1  $f8, 0xd0($sp)
/* 095468 7F062A78 C4269FF4 */  lwc1  $f6, %lo(D_80053EB4)($at)
/* 09546C 7F062A7C C7AA00C4 */  lwc1  $f10, 0xc4($sp)
/* 095470 7F062A80 3C018005 */  lui   $at, %hi(D_80053EB8) # $at, 0x8005
/* 095474 7F062A84 46064102 */  mul.s $f4, $f8, $f6
/* 095478 7F062A88 460A2200 */  add.s $f8, $f4, $f10
/* 09547C 7F062A8C 4600418D */  trunc.w.s $f6, $f8
/* 095480 7F062A90 440A3000 */  mfc1  $t2, $f6
/* 095484 7F062A94 00000000 */  nop
/* 095488 7F062A98 A60A0020 */  sh    $t2, 0x20($s0)
/* 09548C 7F062A9C C7A400D4 */  lwc1  $f4, 0xd4($sp)
/* 095490 7F062AA0 C42A9FF8 */  lwc1  $f10, %lo(D_80053EB8)($at)
/* 095494 7F062AA4 C7A600C8 */  lwc1  $f6, 0xc8($sp)
/* 095498 7F062AA8 3C018005 */  lui   $at, %hi(D_80053EBC) # $at, 0x8005
/* 09549C 7F062AAC 460A2202 */  mul.s $f8, $f4, $f10
/* 0954A0 7F062AB0 46064100 */  add.s $f4, $f8, $f6
/* 0954A4 7F062AB4 4600228D */  trunc.w.s $f10, $f4
/* 0954A8 7F062AB8 440F5000 */  mfc1  $t7, $f10
/* 0954AC 7F062ABC 00000000 */  nop
/* 0954B0 7F062AC0 A60F0022 */  sh    $t7, 0x22($s0)
/* 0954B4 7F062AC4 C7A800D8 */  lwc1  $f8, 0xd8($sp)
/* 0954B8 7F062AC8 C4269FFC */  lwc1  $f6, %lo(D_80053EBC)($at)
/* 0954BC 7F062ACC C7AA00CC */  lwc1  $f10, 0xcc($sp)
/* 0954C0 7F062AD0 3C018005 */  lui   $at, %hi(D_80053EC0) # $at, 0x8005
/* 0954C4 7F062AD4 46064102 */  mul.s $f4, $f8, $f6
/* 0954C8 7F062AD8 460A2200 */  add.s $f8, $f4, $f10
/* 0954CC 7F062ADC 4600418D */  trunc.w.s $f6, $f8
/* 0954D0 7F062AE0 44193000 */  mfc1  $t9, $f6
/* 0954D4 7F062AE4 00000000 */  nop
/* 0954D8 7F062AE8 A6190024 */  sh    $t9, 0x24($s0)
/* 0954DC 7F062AEC 90AB0004 */  lbu   $t3, 4($a1)
/* 0954E0 7F062AF0 000B7140 */  sll   $t6, $t3, 5
/* 0954E4 7F062AF4 A60E0028 */  sh    $t6, 0x28($s0)
/* 0954E8 7F062AF8 90B80005 */  lbu   $t8, 5($a1)
/* 0954EC 7F062AFC 00184940 */  sll   $t1, $t8, 5
/* 0954F0 7F062B00 A609002A */  sh    $t1, 0x2a($s0)
/* 0954F4 7F062B04 C42AA000 */  lwc1  $f10, %lo(D_80053EC0)($at)
/* 0954F8 7F062B08 C7A400D0 */  lwc1  $f4, 0xd0($sp)
/* 0954FC 7F062B0C C7A600C4 */  lwc1  $f6, 0xc4($sp)
/* 095500 7F062B10 3C018005 */  lui   $at, %hi(D_80053EC4) # $at, 0x8005
/* 095504 7F062B14 460A2202 */  mul.s $f8, $f4, $f10
/* 095508 7F062B18 46083101 */  sub.s $f4, $f6, $f8
/* 09550C 7F062B1C 4600228D */  trunc.w.s $f10, $f4
/* 095510 7F062B20 440C5000 */  mfc1  $t4, $f10
/* 095514 7F062B24 00000000 */  nop
/* 095518 7F062B28 A60C0030 */  sh    $t4, 0x30($s0)
/* 09551C 7F062B2C C428A004 */  lwc1  $f8, %lo(D_80053EC4)($at)
/* 095520 7F062B30 C7A600D4 */  lwc1  $f6, 0xd4($sp)
/* 095524 7F062B34 C7AA00C8 */  lwc1  $f10, 0xc8($sp)
/* 095528 7F062B38 3C018005 */  lui   $at, %hi(D_80053EC8) # $at, 0x8005
/* 09552C 7F062B3C 46083102 */  mul.s $f4, $f6, $f8
/* 095530 7F062B40 46045181 */  sub.s $f6, $f10, $f4
/* 095534 7F062B44 4600320D */  trunc.w.s $f8, $f6
/* 095538 7F062B48 440D4000 */  mfc1  $t5, $f8
/* 09553C 7F062B4C 00000000 */  nop
/* 095540 7F062B50 A60D0032 */  sh    $t5, 0x32($s0)
/* 095544 7F062B54 C424A008 */  lwc1  $f4, %lo(D_80053EC8)($at)
/* 095548 7F062B58 C7AA00D8 */  lwc1  $f10, 0xd8($sp)
/* 09554C 7F062B5C C7A800CC */  lwc1  $f8, 0xcc($sp)
/* 095550 7F062B60 A6000038 */  sh    $zero, 0x38($s0)
/* 095554 7F062B64 46045182 */  mul.s $f6, $f10, $f4
/* 095558 7F062B68 24010016 */  li    $at, 22
/* 09555C 7F062B6C 46064281 */  sub.s $f10, $f8, $f6
/* 095560 7F062B70 4600510D */  trunc.w.s $f4, $f10
/* 095564 7F062B74 440B2000 */  mfc1  $t3, $f4
/* 095568 7F062B78 00000000 */  nop
/* 09556C 7F062B7C A60B0034 */  sh    $t3, 0x34($s0)
/* 095570 7F062B80 90AE0005 */  lbu   $t6, 5($a1)
/* 095574 7F062B84 000EC140 */  sll   $t8, $t6, 5
/* 095578 7F062B88 A618003A */  sh    $t8, 0x3a($s0)
/* 09557C 7F062B8C 82290001 */  lb    $t1, 1($s1)
/* 095580 7F062B90 C7A800FC */  lwc1  $f8, 0xfc($sp)
/* 095584 7F062B94 8FAA00F8 */  lw    $t2, 0xf8($sp)
/* 095588 7F062B98 552100AD */  bnel  $t1, $at, .L7F062E50
/* 09558C 7F062B9C 8FAC0160 */   lw    $t4, 0x160($sp)
/* 095590 7F062BA0 C54C0000 */  lwc1  $f12, ($t2)
/* 095594 7F062BA4 C5420004 */  lwc1  $f2, 4($t2)
/* 095598 7F062BA8 C7A60100 */  lwc1  $f6, 0x100($sp)
/* 09559C 7F062BAC 46086381 */  sub.s $f14, $f12, $f8
/* 0955A0 7F062BB0 C5400008 */  lwc1  $f0, 8($t2)
/* 0955A4 7F062BB4 E7A80038 */  swc1  $f8, 0x38($sp)
/* 0955A8 7F062BB8 46061401 */  sub.s $f16, $f2, $f6
/* 0955AC 7F062BBC 460E7102 */  mul.s $f4, $f14, $f14
/* 0955B0 7F062BC0 C7AA0104 */  lwc1  $f10, 0x104($sp)
/* 0955B4 7F062BC4 3C018005 */  lui   $at, %hi(D_80053ECC) # $at, 0x8005
/* 0955B8 7F062BC8 46108202 */  mul.s $f8, $f16, $f16
/* 0955BC 7F062BCC 460A0481 */  sub.s $f18, $f0, $f10
/* 0955C0 7F062BD0 46082100 */  add.s $f4, $f4, $f8
/* 0955C4 7F062BD4 46129202 */  mul.s $f8, $f18, $f18
/* 0955C8 7F062BD8 46082100 */  add.s $f4, $f4, $f8
/* 0955CC 7F062BDC E7A40078 */  swc1  $f4, 0x78($sp)
/* 0955D0 7F062BE0 C6280010 */  lwc1  $f8, 0x10($s1)
/* 0955D4 7F062BE4 E7A60034 */  swc1  $f6, 0x34($sp)
/* 0955D8 7F062BE8 C7A60038 */  lwc1  $f6, 0x38($sp)
/* 0955DC 7F062BEC 46144202 */  mul.s $f8, $f8, $f20
/* 0955E0 7F062BF0 46064200 */  add.s $f8, $f8, $f6
/* 0955E4 7F062BF4 C6260014 */  lwc1  $f6, 0x14($s1)
/* 0955E8 7F062BF8 46086381 */  sub.s $f14, $f12, $f8
/* 0955EC 7F062BFC 46143202 */  mul.s $f8, $f6, $f20
/* 0955F0 7F062C00 C7A60034 */  lwc1  $f6, 0x34($sp)
/* 0955F4 7F062C04 46064200 */  add.s $f8, $f8, $f6
/* 0955F8 7F062C08 C6260018 */  lwc1  $f6, 0x18($s1)
/* 0955FC 7F062C0C 46081401 */  sub.s $f16, $f2, $f8
/* 095600 7F062C10 46143202 */  mul.s $f8, $f6, $f20
/* 095604 7F062C14 460A4180 */  add.s $f6, $f8, $f10
/* 095608 7F062C18 460E7202 */  mul.s $f8, $f14, $f14
/* 09560C 7F062C1C 00000000 */  nop
/* 095610 7F062C20 46108282 */  mul.s $f10, $f16, $f16
/* 095614 7F062C24 46060481 */  sub.s $f18, $f0, $f6
/* 095618 7F062C28 460A4180 */  add.s $f6, $f8, $f10
/* 09561C 7F062C2C 46129202 */  mul.s $f8, $f18, $f18
/* 095620 7F062C30 46083280 */  add.s $f10, $f6, $f8
/* 095624 7F062C34 C7A600C4 */  lwc1  $f6, 0xc4($sp)
/* 095628 7F062C38 4604503C */  c.lt.s $f10, $f4
/* 09562C 7F062C3C 00000000 */  nop
/* 095630 7F062C40 4500000B */  bc1f  .L7F062C70
/* 095634 7F062C44 00000000 */   nop
/* 095638 7F062C48 C424A00C */  lwc1  $f4, %lo(D_80053ECC)($at)
/* 09563C 7F062C4C E7A600B8 */  swc1  $f6, 0xb8($sp)
/* 095640 7F062C50 3C018005 */  lui   $at, %hi(D_80053ED0) # $at, 0x8005
/* 095644 7F062C54 C426A010 */  lwc1  $f6, %lo(D_80053ED0)($at)
/* 095648 7F062C58 C7A800C8 */  lwc1  $f8, 0xc8($sp)
/* 09564C 7F062C5C C7AA00CC */  lwc1  $f10, 0xcc($sp)
/* 095650 7F062C60 46062002 */  mul.s $f0, $f4, $f6
/* 095654 7F062C64 E7A800BC */  swc1  $f8, 0xbc($sp)
/* 095658 7F062C68 E7AA00C0 */  swc1  $f10, 0xc0($sp)
/* 09565C 7F062C6C E7A000B4 */  swc1  $f0, 0xb4($sp)
.L7F062C70:
/* 095660 7F062C70 C7A000B4 */  lwc1  $f0, 0xb4($sp)
/* 095664 7F062C74 C7A800DC */  lwc1  $f8, 0xdc($sp)
/* 095668 7F062C78 C7A400B8 */  lwc1  $f4, 0xb8($sp)
/* 09566C 7F062C7C 3C088007 */  lui   $t0, %hi(flareimage5) # $t0, 0x8007
/* 095670 7F062C80 46004282 */  mul.s $f10, $f8, $f0
/* 095674 7F062C84 250844B8 */  addiu $t0, %lo(flareimage5) # addiu $t0, $t0, 0x44b8
/* 095678 7F062C88 46045180 */  add.s $f6, $f10, $f4
/* 09567C 7F062C8C 4600320D */  trunc.w.s $f8, $f6
/* 095680 7F062C90 440F4000 */  mfc1  $t7, $f8
/* 095684 7F062C94 00000000 */  nop
/* 095688 7F062C98 A60F0040 */  sh    $t7, 0x40($s0)
/* 09568C 7F062C9C C7AA00E0 */  lwc1  $f10, 0xe0($sp)
/* 095690 7F062CA0 C7A600BC */  lwc1  $f6, 0xbc($sp)
/* 095694 7F062CA4 46005102 */  mul.s $f4, $f10, $f0
/* 095698 7F062CA8 46062200 */  add.s $f8, $f4, $f6
/* 09569C 7F062CAC 4600428D */  trunc.w.s $f10, $f8
/* 0956A0 7F062CB0 44195000 */  mfc1  $t9, $f10
/* 0956A4 7F062CB4 00000000 */  nop
/* 0956A8 7F062CB8 A6190042 */  sh    $t9, 0x42($s0)
/* 0956AC 7F062CBC C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 0956B0 7F062CC0 C7A800C0 */  lwc1  $f8, 0xc0($sp)
/* 0956B4 7F062CC4 46002182 */  mul.s $f6, $f4, $f0
/* 0956B8 7F062CC8 46083280 */  add.s $f10, $f6, $f8
/* 0956BC 7F062CCC 4600510D */  trunc.w.s $f4, $f10
/* 0956C0 7F062CD0 440E2000 */  mfc1  $t6, $f4
/* 0956C4 7F062CD4 00000000 */  nop
/* 0956C8 7F062CD8 A60E0044 */  sh    $t6, 0x44($s0)
/* 0956CC 7F062CDC 8D180000 */  lw    $t8, ($t0)
/* 0956D0 7F062CE0 93090004 */  lbu   $t1, 4($t8)
/* 0956D4 7F062CE4 00095140 */  sll   $t2, $t1, 5
/* 0956D8 7F062CE8 A60A0048 */  sh    $t2, 0x48($s0)
/* 0956DC 7F062CEC 8D0C0000 */  lw    $t4, ($t0)
/* 0956E0 7F062CF0 918F0005 */  lbu   $t7, 5($t4)
/* 0956E4 7F062CF4 000F6940 */  sll   $t5, $t7, 5
/* 0956E8 7F062CF8 A60D004A */  sh    $t5, 0x4a($s0)
/* 0956EC 7F062CFC C7A800DC */  lwc1  $f8, 0xdc($sp)
/* 0956F0 7F062D00 C7A600B8 */  lwc1  $f6, 0xb8($sp)
/* 0956F4 7F062D04 46004282 */  mul.s $f10, $f8, $f0
/* 0956F8 7F062D08 460A3101 */  sub.s $f4, $f6, $f10
/* 0956FC 7F062D0C 4600220D */  trunc.w.s $f8, $f4
/* 095700 7F062D10 440B4000 */  mfc1  $t3, $f8
/* 095704 7F062D14 00000000 */  nop
/* 095708 7F062D18 A60B0050 */  sh    $t3, 0x50($s0)
/* 09570C 7F062D1C C7AA00E0 */  lwc1  $f10, 0xe0($sp)
/* 095710 7F062D20 C7A600BC */  lwc1  $f6, 0xbc($sp)
/* 095714 7F062D24 46005102 */  mul.s $f4, $f10, $f0
/* 095718 7F062D28 46043201 */  sub.s $f8, $f6, $f4
/* 09571C 7F062D2C 4600428D */  trunc.w.s $f10, $f8
/* 095720 7F062D30 44185000 */  mfc1  $t8, $f10
/* 095724 7F062D34 00000000 */  nop
/* 095728 7F062D38 A6180052 */  sh    $t8, 0x52($s0)
/* 09572C 7F062D3C C7A400E4 */  lwc1  $f4, 0xe4($sp)
/* 095730 7F062D40 C7A600C0 */  lwc1  $f6, 0xc0($sp)
/* 095734 7F062D44 A6000058 */  sh    $zero, 0x58($s0)
/* 095738 7F062D48 46002202 */  mul.s $f8, $f4, $f0
/* 09573C 7F062D4C A600005A */  sh    $zero, 0x5a($s0)
/* 095740 7F062D50 46083281 */  sub.s $f10, $f6, $f8
/* 095744 7F062D54 4600510D */  trunc.w.s $f4, $f10
/* 095748 7F062D58 440A2000 */  mfc1  $t2, $f4
/* 09574C 7F062D5C 00000000 */  nop
/* 095750 7F062D60 A60A0054 */  sh    $t2, 0x54($s0)
/* 095754 7F062D64 C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 095758 7F062D68 C7AA00B8 */  lwc1  $f10, 0xb8($sp)
/* 09575C 7F062D6C 46003202 */  mul.s $f8, $f6, $f0
/* 095760 7F062D70 460A4100 */  add.s $f4, $f8, $f10
/* 095764 7F062D74 4600218D */  trunc.w.s $f6, $f4
/* 095768 7F062D78 440F3000 */  mfc1  $t7, $f6
/* 09576C 7F062D7C 00000000 */  nop
/* 095770 7F062D80 A60F0060 */  sh    $t7, 0x60($s0)
/* 095774 7F062D84 C7A800D4 */  lwc1  $f8, 0xd4($sp)
/* 095778 7F062D88 C7A400BC */  lwc1  $f4, 0xbc($sp)
/* 09577C 7F062D8C 46004282 */  mul.s $f10, $f8, $f0
/* 095780 7F062D90 46045180 */  add.s $f6, $f10, $f4
/* 095784 7F062D94 4600320D */  trunc.w.s $f8, $f6
/* 095788 7F062D98 44194000 */  mfc1  $t9, $f8
/* 09578C 7F062D9C 00000000 */  nop
/* 095790 7F062DA0 A6190062 */  sh    $t9, 0x62($s0)
/* 095794 7F062DA4 C7AA00D8 */  lwc1  $f10, 0xd8($sp)
/* 095798 7F062DA8 C7A600C0 */  lwc1  $f6, 0xc0($sp)
/* 09579C 7F062DAC A6000068 */  sh    $zero, 0x68($s0)
/* 0957A0 7F062DB0 46005102 */  mul.s $f4, $f10, $f0
/* 0957A4 7F062DB4 46062200 */  add.s $f8, $f4, $f6
/* 0957A8 7F062DB8 4600428D */  trunc.w.s $f10, $f8
/* 0957AC 7F062DBC 440E5000 */  mfc1  $t6, $f10
/* 0957B0 7F062DC0 00000000 */  nop
/* 0957B4 7F062DC4 A60E0064 */  sh    $t6, 0x64($s0)
/* 0957B8 7F062DC8 8D180000 */  lw    $t8, ($t0)
/* 0957BC 7F062DCC 93090005 */  lbu   $t1, 5($t8)
/* 0957C0 7F062DD0 00095140 */  sll   $t2, $t1, 5
/* 0957C4 7F062DD4 A60A006A */  sh    $t2, 0x6a($s0)
/* 0957C8 7F062DD8 C7A600D0 */  lwc1  $f6, 0xd0($sp)
/* 0957CC 7F062DDC C7A400B8 */  lwc1  $f4, 0xb8($sp)
/* 0957D0 7F062DE0 46003202 */  mul.s $f8, $f6, $f0
/* 0957D4 7F062DE4 46082281 */  sub.s $f10, $f4, $f8
/* 0957D8 7F062DE8 4600518D */  trunc.w.s $f6, $f10
/* 0957DC 7F062DEC 440F3000 */  mfc1  $t7, $f6
/* 0957E0 7F062DF0 00000000 */  nop
/* 0957E4 7F062DF4 A60F0070 */  sh    $t7, 0x70($s0)
/* 0957E8 7F062DF8 C7A800D4 */  lwc1  $f8, 0xd4($sp)
/* 0957EC 7F062DFC C7A400BC */  lwc1  $f4, 0xbc($sp)
/* 0957F0 7F062E00 46004282 */  mul.s $f10, $f8, $f0
/* 0957F4 7F062E04 460A2181 */  sub.s $f6, $f4, $f10
/* 0957F8 7F062E08 4600320D */  trunc.w.s $f8, $f6
/* 0957FC 7F062E0C 44194000 */  mfc1  $t9, $f8
/* 095800 7F062E10 00000000 */  nop
/* 095804 7F062E14 A6190072 */  sh    $t9, 0x72($s0)
/* 095808 7F062E18 C7AA00D8 */  lwc1  $f10, 0xd8($sp)
/* 09580C 7F062E1C C7A400C0 */  lwc1  $f4, 0xc0($sp)
/* 095810 7F062E20 46005182 */  mul.s $f6, $f10, $f0
/* 095814 7F062E24 46062201 */  sub.s $f8, $f4, $f6
/* 095818 7F062E28 4600428D */  trunc.w.s $f10, $f8
/* 09581C 7F062E2C 440E5000 */  mfc1  $t6, $f10
/* 095820 7F062E30 00000000 */  nop
/* 095824 7F062E34 A60E0074 */  sh    $t6, 0x74($s0)
/* 095828 7F062E38 8D180000 */  lw    $t8, ($t0)
/* 09582C 7F062E3C 93090004 */  lbu   $t1, 4($t8)
/* 095830 7F062E40 A600007A */  sh    $zero, 0x7a($s0)
/* 095834 7F062E44 00095140 */  sll   $t2, $t1, 5
/* 095838 7F062E48 A60A0078 */  sh    $t2, 0x78($s0)
/* 09583C 7F062E4C 8FAC0160 */  lw    $t4, 0x160($sp)
.L7F062E50:
/* 095840 7F062E50 3C0DB600 */  lui   $t5, 0xb600
/* 095844 7F062E54 24192000 */  li    $t9, 8192
/* 095848 7F062E58 258F0008 */  addiu $t7, $t4, 8
/* 09584C 7F062E5C AFAF0160 */  sw    $t7, 0x160($sp)
/* 095850 7F062E60 AD990004 */  sw    $t9, 4($t4)
/* 095854 7F062E64 AD8D0000 */  sw    $t5, ($t4)
/* 095858 7F062E68 8FAB0160 */  lw    $t3, 0x160($sp)
/* 09585C 7F062E6C 3C180102 */  lui   $t8, (0x01020040 >> 16) # lui $t8, 0x102
/* 095860 7F062E70 37180040 */  ori   $t8, (0x01020040 & 0xFFFF) # ori $t8, $t8, 0x40
/* 095864 7F062E74 256E0008 */  addiu $t6, $t3, 8
/* 095868 7F062E78 AFAE0160 */  sw    $t6, 0x160($sp)
/* 09586C 7F062E7C AD780000 */  sw    $t8, ($t3)
/* 095870 7F062E80 8FA40158 */  lw    $a0, 0x158($sp)
/* 095874 7F062E84 0C003838 */  jal   osVirtualToPhysical
/* 095878 7F062E88 AFAB006C */   sw    $t3, 0x6c($sp)
/* 09587C 7F062E8C 8FA3006C */  lw    $v1, 0x6c($sp)
/* 095880 7F062E90 3C088007 */  lui   $t0, %hi(flareimage5) # $t0, 0x8007
/* 095884 7F062E94 24010016 */  li    $at, 22
/* 095888 7F062E98 AC620004 */  sw    $v0, 4($v1)
/* 09588C 7F062E9C 82290001 */  lb    $t1, 1($s1)
/* 095890 7F062EA0 250844B8 */  addiu $t0, %lo(flareimage5) # addiu $t0, $t0, 0x44b8
/* 095894 7F062EA4 8FA500B0 */  lw    $a1, 0xb0($sp)
/* 095898 7F062EA8 15210029 */  bne   $t1, $at, .L7F062F50
/* 09589C 7F062EAC 27A40160 */   addiu $a0, $sp, 0x160
/* 0958A0 7F062EB0 240A0002 */  li    $t2, 2
/* 0958A4 7F062EB4 AFAA0010 */  sw    $t2, 0x10($sp)
/* 0958A8 7F062EB8 27A40160 */  addiu $a0, $sp, 0x160
/* 0958AC 7F062EBC 8D050000 */  lw    $a1, ($t0)
/* 0958B0 7F062EC0 24060004 */  li    $a2, 4
/* 0958B4 7F062EC4 0FC1DB7A */  jal   texSelect
/* 0958B8 7F062EC8 8FA70168 */   lw    $a3, 0x168($sp)
/* 0958BC 7F062ECC 8FB10160 */  lw    $s1, 0x160($sp)
/* 0958C0 7F062ED0 3C0D0470 */  lui   $t5, (0x04700080 >> 16) # lui $t5, 0x470
/* 0958C4 7F062ED4 35AD0080 */  ori   $t5, (0x04700080 & 0xFFFF) # ori $t5, $t5, 0x80
/* 0958C8 7F062ED8 262F0008 */  addiu $t7, $s1, 8
/* 0958CC 7F062EDC AFAF0160 */  sw    $t7, 0x160($sp)
/* 0958D0 7F062EE0 02002025 */  move  $a0, $s0
/* 0958D4 7F062EE4 0C003838 */  jal   osVirtualToPhysical
/* 0958D8 7F062EE8 AE2D0000 */   sw    $t5, ($s1)
/* 0958DC 7F062EEC AE220004 */  sw    $v0, 4($s1)
/* 0958E0 7F062EF0 8FB90160 */  lw    $t9, 0x160($sp)
/* 0958E4 7F062EF4 3C0EB100 */  lui   $t6, (0xB1000076 >> 16) # lui $t6, 0xb100
/* 0958E8 7F062EF8 35CE0076 */  ori   $t6, (0xB1000076 & 0xFFFF) # ori $t6, $t6, 0x76
/* 0958EC 7F062EFC 272B0008 */  addiu $t3, $t9, 8
/* 0958F0 7F062F00 AFAB0160 */  sw    $t3, 0x160($sp)
/* 0958F4 7F062F04 24185454 */  li    $t8, 21588
/* 0958F8 7F062F08 AF380004 */  sw    $t8, 4($t9)
/* 0958FC 7F062F0C AF2E0000 */  sw    $t6, ($t9)
/* 095900 7F062F10 24090002 */  li    $t1, 2
/* 095904 7F062F14 AFA90010 */  sw    $t1, 0x10($sp)
/* 095908 7F062F18 8FA70168 */  lw    $a3, 0x168($sp)
/* 09590C 7F062F1C 8FA500B0 */  lw    $a1, 0xb0($sp)
/* 095910 7F062F20 27A40160 */  addiu $a0, $sp, 0x160
/* 095914 7F062F24 0FC1DB7A */  jal   texSelect
/* 095918 7F062F28 24060004 */   li    $a2, 4
/* 09591C 7F062F2C 8FAA0160 */  lw    $t2, 0x160($sp)
/* 095920 7F062F30 3C0FB100 */  lui   $t7, (0xB1000013 >> 16) # lui $t7, 0xb100
/* 095924 7F062F34 35EF0013 */  ori   $t7, (0xB1000013 & 0xFFFF) # ori $t7, $t7, 0x13
/* 095928 7F062F38 254C0008 */  addiu $t4, $t2, 8
/* 09592C 7F062F3C AFAC0160 */  sw    $t4, 0x160($sp)
/* 095930 7F062F40 240D3020 */  li    $t5, 12320
/* 095934 7F062F44 AD4D0004 */  sw    $t5, 4($t2)
/* 095938 7F062F48 10000017 */  b     .L7F062FA8
/* 09593C 7F062F4C AD4F0000 */   sw    $t7, ($t2)
.L7F062F50:
/* 095940 7F062F50 24190002 */  li    $t9, 2
/* 095944 7F062F54 AFB90010 */  sw    $t9, 0x10($sp)
/* 095948 7F062F58 24060004 */  li    $a2, 4
/* 09594C 7F062F5C 0FC1DB7A */  jal   texSelect
/* 095950 7F062F60 8FA70168 */   lw    $a3, 0x168($sp)
/* 095954 7F062F64 8FB10160 */  lw    $s1, 0x160($sp)
/* 095958 7F062F68 3C180430 */  lui   $t8, (0x04300040 >> 16) # lui $t8, 0x430
/* 09595C 7F062F6C 37180040 */  ori   $t8, (0x04300040 & 0xFFFF) # ori $t8, $t8, 0x40
/* 095960 7F062F70 262E0008 */  addiu $t6, $s1, 8
/* 095964 7F062F74 AFAE0160 */  sw    $t6, 0x160($sp)
/* 095968 7F062F78 02002025 */  move  $a0, $s0
/* 09596C 7F062F7C 0C003838 */  jal   osVirtualToPhysical
/* 095970 7F062F80 AE380000 */   sw    $t8, ($s1)
/* 095974 7F062F84 AE220004 */  sw    $v0, 4($s1)
/* 095978 7F062F88 8FA90160 */  lw    $t1, 0x160($sp)
/* 09597C 7F062F8C 3C0CB100 */  lui   $t4, (0xB1000013 >> 16) # lui $t4, 0xb100
/* 095980 7F062F90 358C0013 */  ori   $t4, (0xB1000013 & 0xFFFF) # ori $t4, $t4, 0x13
/* 095984 7F062F94 252A0008 */  addiu $t2, $t1, 8
/* 095988 7F062F98 AFAA0160 */  sw    $t2, 0x160($sp)
/* 09598C 7F062F9C 240F3020 */  li    $t7, 12320
/* 095990 7F062FA0 AD2F0004 */  sw    $t7, 4($t1)
/* 095994 7F062FA4 AD2C0000 */  sw    $t4, ($t1)
.L7F062FA8:
/* 095998 7F062FA8 8FBF002C */  lw    $ra, 0x2c($sp)
/* 09599C 7F062FAC 8FA20160 */  lw    $v0, 0x160($sp)
/* 0959A0 7F062FB0 D7B40018 */  ldc1  $f20, 0x18($sp)
/* 0959A4 7F062FB4 8FB00024 */  lw    $s0, 0x24($sp)
/* 0959A8 7F062FB8 8FB10028 */  lw    $s1, 0x28($sp)
/* 0959AC 7F062FBC 03E00008 */  jr    $ra
/* 0959B0 7F062FC0 27BD0160 */   addiu $sp, $sp, 0x160
)
#endif
#endif

/*
* Address: 0x7F062B00
*/
void sub_GAME_7F062B00(ChrRecord_f180* arg0)
{
    if (arg0->unk00 >= 0)
    {
        if (g_ClockTimer < 3)
        {
#ifdef VERSION_US
            arg0->unk28 += arg0->unk20 * g_GlobalTimerDelta;
#else
            arg0->unk28 += arg0->unk20 * g_JP_GlobalTimerDelta;
#endif
        }
        else
        {
            arg0->unk28 += arg0->unk20 * (2.0f + ((f32) randomGetNext() * 2.3283064e-10f * 0.5f));
        }

        if (arg0->unk1c <= arg0->unk28)
        {
            arg0->unk00 = -1;
            return;
        }

        arg0->unk00++;
    }
}


#ifdef NONMATCHING
void sub_GAME_7F062BE4(void) {

}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.text
glabel sub_GAME_7F062BE4
/* 097714 7F062BE4 27BDFF30 */  addiu $sp, $sp, -0xd0
/* 097718 7F062BE8 AFB7003C */  sw    $s7, 0x3c($sp)
/* 09771C 7F062BEC 3C0F8003 */  lui   $t7, %hi(D_80035CC0)
/* 097720 7F062BF0 27B7008C */  addiu $s7, $sp, 0x8c
/* 097724 7F062BF4 AFBF0044 */  sw    $ra, 0x44($sp)
/* 097728 7F062BF8 AFBE0040 */  sw    $fp, 0x40($sp)
/* 09772C 7F062BFC AFB60038 */  sw    $s6, 0x38($sp)
/* 097730 7F062C00 AFB50034 */  sw    $s5, 0x34($sp)
/* 097734 7F062C04 AFB40030 */  sw    $s4, 0x30($sp)
/* 097738 7F062C08 AFB3002C */  sw    $s3, 0x2c($sp)
/* 09773C 7F062C0C AFB20028 */  sw    $s2, 0x28($sp)
/* 097740 7F062C10 AFB10024 */  sw    $s1, 0x24($sp)
/* 097744 7F062C14 AFB00020 */  sw    $s0, 0x20($sp)
/* 097748 7F062C18 AFA400D0 */  sw    $a0, 0xd0($sp)
/* 09774C 7F062C1C 25EF5CC0 */  addiu $t7, %lo(D_80035CC0) # addiu $t7, $t7, 0x5cc0
/* 097750 7F062C20 8C900000 */  lw    $s0, ($a0)
/* 097754 7F062C24 25E8003C */  addiu $t0, $t7, 0x3c
/* 097758 7F062C28 02E04825 */  move  $t1, $s7
.L7F062C2C:
/* 09775C 7F062C2C 8DE10000 */  lw    $at, ($t7)
/* 097760 7F062C30 25EF000C */  addiu $t7, $t7, 0xc
/* 097764 7F062C34 2529000C */  addiu $t1, $t1, 0xc
/* 097768 7F062C38 AD21FFF4 */  sw    $at, -0xc($t1)
/* 09776C 7F062C3C 8DE1FFF8 */  lw    $at, -8($t7)
/* 097770 7F062C40 AD21FFF8 */  sw    $at, -8($t1)
/* 097774 7F062C44 8DE1FFFC */  lw    $at, -4($t7)
/* 097778 7F062C48 15E8FFF8 */  bne   $t7, $t0, .L7F062C2C
/* 09777C 7F062C4C AD21FFFC */   sw    $at, -4($t1)
/* 097780 7F062C50 8DE10000 */  lw    $at, ($t7)
/* 097784 7F062C54 3C1E8008 */  lui   $fp, %hi(g_CurrentPlayer)
/* 097788 7F062C58 27DEA0B0 */  addiu $fp, %lo(g_CurrentPlayer) # addiu $fp, $fp, -0x5f50
/* 09778C 7F062C5C 0000A825 */  move  $s5, $zero
/* 097790 7F062C60 0000B025 */  move  $s6, $zero
/* 097794 7F062C64 AD210000 */  sw    $at, ($t1)
.L7F062C68:
/* 097798 7F062C68 8FCA0000 */  lw    $t2, ($fp)
/* 09779C 7F062C6C 02A02025 */  move  $a0, $s5
/* 0977A0 7F062C70 01569821 */  addu  $s3, $t2, $s6
/* 0977A4 7F062C74 0FC17691 */  jal   get_item_in_hand_or_watch_menu
/* 0977A8 7F062C78 26730870 */   addiu $s3, $s3, 0x870
/* 0977AC 7F062C7C 826B000F */  lb    $t3, 0xf($s3)
/* 0977B0 7F062C80 0040A025 */  move  $s4, $v0
/* 0977B4 7F062C84 24010017 */  li    $at, 23
/* 0977B8 7F062C88 516000CD */  beql  $t3, $zero, .L7F062FC0
/* 0977BC 7F062C8C 26B50001 */   addiu $s5, $s5, 1
/* 0977C0 7F062C90 10410005 */  beq   $v0, $at, .L7F062CA8
/* 0977C4 7F062C94 02002025 */   move  $a0, $s0
/* 0977C8 7F062C98 266501E4 */  addiu $a1, $s3, 0x1e4
/* 0977CC 7F062C9C 0FC18786 */  jal   sub_GAME_7F061E18
/* 0977D0 7F062CA0 00003025 */   move  $a2, $zero
/* 0977D4 7F062CA4 00408025 */  move  $s0, $v0
.L7F062CA8:
/* 0977D8 7F062CA8 24010013 */  li    $at, 19
/* 0977DC 7F062CAC 1281000C */  beq   $s4, $at, .L7F062CE0
/* 0977E0 7F062CB0 02001025 */   move  $v0, $s0
/* 0977E4 7F062CB4 24010012 */  li    $at, 18
/* 0977E8 7F062CB8 12810009 */  beq   $s4, $at, .L7F062CE0
/* 0977EC 7F062CBC 24010002 */   li    $at, 2
/* 0977F0 7F062CC0 12810007 */  beq   $s4, $at, .L7F062CE0
/* 0977F4 7F062CC4 24010003 */   li    $at, 3
/* 0977F8 7F062CC8 12810005 */  beq   $s4, $at, .L7F062CE0
/* 0977FC 7F062CCC 24010014 */   li    $at, 20
/* 097800 7F062CD0 12810003 */  beq   $s4, $at, .L7F062CE0
/* 097804 7F062CD4 24010015 */   li    $at, 21
/* 097808 7F062CD8 56810028 */  bnel  $s4, $at, .L7F062D7C
/* 09780C 7F062CDC 3C0BBC00 */   lui   $t3, 0xbc00
.L7F062CE0:
/* 097810 7F062CE0 26100008 */  addiu $s0, $s0, 8
/* 097814 7F062CE4 3C0CBC00 */  lui   $t4, (0xBC000002 >> 16) # lui $t4, 0xbc00
/* 097818 7F062CE8 3C0D8000 */  lui   $t5, (0x80000040 >> 16) # lui $t5, 0x8000
/* 09781C 7F062CEC 35AD0040 */  ori   $t5, (0x80000040 & 0xFFFF) # ori $t5, $t5, 0x40
/* 097820 7F062CF0 358C0002 */  ori   $t4, (0xBC000002 & 0xFFFF) # ori $t4, $t4, 2
/* 097824 7F062CF4 02001825 */  move  $v1, $s0
/* 097828 7F062CF8 26100008 */  addiu $s0, $s0, 8
/* 09782C 7F062CFC AC4C0000 */  sw    $t4, ($v0)
/* 097830 7F062D00 AC4D0004 */  sw    $t5, 4($v0)
/* 097834 7F062D04 3C0E0386 */  lui   $t6, (0x03860010 >> 16) # lui $t6, 0x386
/* 097838 7F062D08 3C198003 */  lui   $t9, %hi(g_WeaponEnvmapLight + 0x8)
/* 09783C 7F062D0C 27392448 */  addiu $t9, %lo(g_WeaponEnvmapLight + 0x8) # addiu $t9, $t9, 0x2448
/* 097840 7F062D10 35CE0010 */  ori   $t6, (0x03860010 & 0xFFFF) # ori $t6, $t6, 0x10
/* 097844 7F062D14 02002025 */  move  $a0, $s0
/* 097848 7F062D18 AC6E0000 */  sw    $t6, ($v1)
/* 09784C 7F062D1C AC790004 */  sw    $t9, 4($v1)
/* 097850 7F062D20 3C180388 */  lui   $t8, (0x03880010 >> 16) # lui $t8, 0x388
/* 097854 7F062D24 3C088003 */  lui   $t0, %hi(g_WeaponEnvmapLight)
/* 097858 7F062D28 25082440 */  addiu $t0, %lo(g_WeaponEnvmapLight) # addiu $t0, $t0, 0x2440
/* 09785C 7F062D2C 37180010 */  ori   $t8, (0x03880010 & 0xFFFF) # ori $t8, $t8, 0x10
/* 097860 7F062D30 26100008 */  addiu $s0, $s0, 8
/* 097864 7F062D34 3C0F0384 */  lui   $t7, (0x03840010 >> 16) # lui $t7, 0x384
/* 097868 7F062D38 AC980000 */  sw    $t8, ($a0)
/* 09786C 7F062D3C AC880004 */  sw    $t0, 4($a0)
/* 097870 7F062D40 35EF0010 */  ori   $t7, (0x03840010 & 0xFFFF) # ori $t7, $t7, 0x10
/* 097874 7F062D44 02008825 */  move  $s1, $s0
/* 097878 7F062D48 AE2F0000 */  sw    $t7, ($s1)
/* 09787C 7F062D4C 0FC1E11D */  jal   sub_GAME_7F078474
/* 097880 7F062D50 26100008 */   addiu $s0, $s0, 8
/* 097884 7F062D54 3C090382 */  lui   $t1, (0x03820010 >> 16) # lui $t1, 0x382
/* 097888 7F062D58 35290010 */  ori   $t1, (0x03820010 & 0xFFFF) # ori $t1, $t1, 0x10
/* 09788C 7F062D5C AE220004 */  sw    $v0, 4($s1)
/* 097890 7F062D60 02009025 */  move  $s2, $s0
/* 097894 7F062D64 AE490000 */  sw    $t1, ($s2)
/* 097898 7F062D68 0FC1E11D */  jal   sub_GAME_7F078474
/* 09789C 7F062D6C 26100008 */   addiu $s0, $s0, 8
/* 0978A0 7F062D70 244A0010 */  addiu $t2, $v0, 0x10
/* 0978A4 7F062D74 AE4A0004 */  sw    $t2, 4($s2)
/* 0978A8 7F062D78 3C0BBC00 */  lui   $t3, (0xBC00000E >> 16) # lui $t3, 0xbc00
.L7F062D7C:
/* 0978AC 7F062D7C 3C014396 */  li    $at, 0x43960000 # 300.000000
/* 0978B0 7F062D80 44817000 */  mtc1  $at, $f14
/* 0978B4 7F062D84 356B000E */  ori   $t3, (0xBC00000E & 0xFFFF) # ori $t3, $t3, 0xe
/* 0978B8 7F062D88 02008825 */  move  $s1, $s0
/* 0978BC 7F062D8C 44806000 */  mtc1  $zero, $f12
/* 0978C0 7F062D90 AE2B0000 */  sw    $t3, ($s1)
/* 0978C4 7F062D94 0FC1665F */  jal   matrix_4x4_calc_depth_scale
/* 0978C8 7F062D98 26100008 */   addiu $s0, $s0, 8
/* 0978CC 7F062D9C AE220004 */  sw    $v0, 4($s1)
/* 0978D0 7F062DA0 8E630300 */  lw    $v1, 0x300($s3)
/* 0978D4 7F062DA4 846C000C */  lh    $t4, 0xc($v1)
/* 0978D8 7F062DA8 29810011 */  slti  $at, $t4, 0x11
/* 0978DC 7F062DAC 5420002D */  bnezl $at, .L7F062E64
/* 0978E0 7F062DB0 8FC20000 */   lw    $v0, ($fp)
/* 0978E4 7F062DB4 8C620008 */  lw    $v0, 8($v1)
/* 0978E8 7F062DB8 267102F8 */  addiu $s1, $s3, 0x2f8
/* 0978EC 7F062DBC 02202025 */  move  $a0, $s1
/* 0978F0 7F062DC0 8C4D0040 */  lw    $t5, 0x40($v0)
/* 0978F4 7F062DC4 51A00027 */  beql  $t5, $zero, .L7F062E64
/* 0978F8 7F062DC8 8FC20000 */   lw    $v0, ($fp)
/* 0978FC 7F062DCC 0FC1B1E7 */  jal   modelGetNodeRwData
/* 097900 7F062DD0 8C450044 */   lw    $a1, 0x44($v0)
/* 097904 7F062DD4 10400003 */  beqz  $v0, .L7F062DE4
/* 097908 7F062DD8 24010019 */   li    $at, 25
/* 09790C 7F062DDC 240E0001 */  li    $t6, 1
/* 097910 7F062DE0 AC4E0000 */  sw    $t6, ($v0)
.L7F062DE4:
/* 097914 7F062DE4 16810013 */  bne   $s4, $at, .L7F062E34
/* 097918 7F062DE8 02202025 */   move  $a0, $s1
/* 09791C 7F062DEC 3C048007 */  lui   $a0, %hi(g_UnknownAnimController)
/* 097920 7F062DF0 3C058009 */  lui   $a1, %hi(crosshairimage)
/* 097924 7F062DF4 8CA5D114 */  lw    $a1, %lo(crosshairimage)($a1)
/* 097928 7F062DF8 0FC127D0 */  jal   save_img_index_to_obj_ani_slot
/* 09792C 7F062DFC 24845C10 */   addiu $a0, %lo(g_UnknownAnimController) # addiu $a0, $a0, 0x5c10
/* 097930 7F062E00 8E790300 */  lw    $t9, 0x300($s3)
/* 097934 7F062E04 3C068007 */  lui   $a2, %hi(g_UnknownAnimController)
/* 097938 7F062E08 24080004 */  li    $t0, 4
/* 09793C 7F062E0C 8F380008 */  lw    $t8, 8($t9)
/* 097940 7F062E10 24C65C10 */  addiu $a2, %lo(g_UnknownAnimController) # addiu $a2, $a2, 0x5c10
/* 097944 7F062E14 02202025 */  move  $a0, $s1
/* 097948 7F062E18 8F050040 */  lw    $a1, 0x40($t8)
/* 09794C 7F062E1C AFA80014 */  sw    $t0, 0x14($sp)
/* 097950 7F062E20 AFA00010 */  sw    $zero, 0x10($sp)
/* 097954 7F062E24 0FC127D2 */  jal   process_monitor_animation_microcode
/* 097958 7F062E28 02003825 */   move  $a3, $s0
/* 09795C 7F062E2C 1000000C */  b     .L7F062E60
/* 097960 7F062E30 00408025 */   move  $s0, $v0
.L7F062E34:
/* 097964 7F062E34 8E6F0300 */  lw    $t7, 0x300($s3)
/* 097968 7F062E38 3C068007 */  lui   $a2, %hi(g_TaserAnimController)
/* 09796C 7F062E3C 240A0001 */  li    $t2, 1
/* 097970 7F062E40 8DE90008 */  lw    $t1, 8($t7)
/* 097974 7F062E44 24C65C88 */  addiu $a2, %lo(g_TaserAnimController) # addiu $a2, $a2, 0x5c88
/* 097978 7F062E48 02003825 */  move  $a3, $s0
/* 09797C 7F062E4C 8D250040 */  lw    $a1, 0x40($t1)
/* 097980 7F062E50 AFAA0014 */  sw    $t2, 0x14($sp)
/* 097984 7F062E54 0FC127D2 */  jal   process_monitor_animation_microcode
/* 097988 7F062E58 AFA00010 */   sw    $zero, 0x10($sp)
/* 09798C 7F062E5C 00408025 */  move  $s0, $v0
.L7F062E60:
/* 097990 7F062E60 8FC20000 */  lw    $v0, ($fp)
.L7F062E64:
/* 097994 7F062E64 240B0004 */  li    $t3, 4
/* 097998 7F062E68 AFB00098 */  sw    $s0, 0x98($sp)
/* 09799C 7F062E6C AFAB00BC */  sw    $t3, 0xbc($sp)
/* 0979A0 7F062E70 904D0FDC */  lbu   $t5, 0xfdc($v0)
/* 0979A4 7F062E74 90580FDD */  lbu   $t8, 0xfdd($v0)
/* 0979A8 7F062E78 904C0FDF */  lbu   $t4, 0xfdf($v0)
/* 0979AC 7F062E7C 90490FDE */  lbu   $t1, 0xfde($v0)
/* 0979B0 7F062E80 000D7600 */  sll   $t6, $t5, 0x18
/* 0979B4 7F062E84 00184400 */  sll   $t0, $t8, 0x10
/* 0979B8 7F062E88 018EC825 */  or    $t9, $t4, $t6
/* 0979BC 7F062E8C 03287825 */  or    $t7, $t9, $t0
/* 0979C0 7F062E90 00095200 */  sll   $t2, $t1, 8
/* 0979C4 7F062E94 01EA5825 */  or    $t3, $t7, $t2
/* 0979C8 7F062E98 AFAB00C0 */  sw    $t3, 0xc0($sp)
/* 0979CC 7F062E9C AFA00090 */  sw    $zero, 0x90($sp)
/* 0979D0 7F062EA0 0FC16319 */  jal   matrix_4x4_7F058C64
/* 0979D4 7F062EA4 267102F8 */   addiu $s1, $s3, 0x2f8
/* 0979D8 7F062EA8 24010019 */  li    $at, 25
/* 0979DC 7F062EAC 56810011 */  bnel  $s4, $at, .L7F062EF4
/* 0979E0 7F062EB0 02802025 */   move  $a0, $s4
/* 0979E4 7F062EB4 8E620220 */  lw    $v0, 0x220($s3)
/* 0979E8 7F062EB8 5040000E */  beql  $v0, $zero, .L7F062EF4
/* 0979EC 7F062EBC 02802025 */   move  $a0, $s4
/* 0979F0 7F062EC0 8C500014 */  lw    $s0, 0x14($v0)
/* 0979F4 7F062EC4 02E02025 */  move  $a0, $s7
/* 0979F8 7F062EC8 0FC1D1A1 */  jal   subdraw
/* 0979FC 7F062ECC 02002825 */   move  $a1, $s0
/* 097A00 7F062ED0 8E0D0008 */  lw    $t5, 8($s0)
/* 097A04 7F062ED4 8E04000C */  lw    $a0, 0xc($s0)
/* 097A08 7F062ED8 0FC22F52 */  jal   bondviewTransformManyPosToViewMatrix
/* 097A0C 7F062EDC 85A5000E */   lh    $a1, 0xe($t5)
/* 097A10 7F062EE0 8E6C0224 */  lw    $t4, 0x224($s3)
/* 097A14 7F062EE4 51800003 */  beql  $t4, $zero, .L7F062EF4
/* 097A18 7F062EE8 02802025 */   move  $a0, $s4
/* 097A1C 7F062EEC AE600220 */  sw    $zero, 0x220($s3)
/* 097A20 7F062EF0 02802025 */  move  $a0, $s4
.L7F062EF4:
/* 097A24 7F062EF4 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 097A28 7F062EF8 24050400 */   li    $a1, 1024
/* 097A2C 7F062EFC 1040000E */  beqz  $v0, .L7F062F38
/* 097A30 7F062F00 02E02025 */   move  $a0, $s7
/* 097A34 7F062F04 8FAE0098 */  lw    $t6, 0x98($sp)
/* 097A38 7F062F08 3C19B600 */  lui   $t9, 0xb600
/* 097A3C 7F062F0C 24083000 */  li    $t0, 12288
/* 097A40 7F062F10 25D80008 */  addiu $t8, $t6, 8
/* 097A44 7F062F14 AFB80098 */  sw    $t8, 0x98($sp)
/* 097A48 7F062F18 ADC80004 */  sw    $t0, 4($t6)
/* 097A4C 7F062F1C 16A00004 */  bnez  $s5, .L7F062F30
/* 097A50 7F062F20 ADD90000 */   sw    $t9, ($t6)
/* 097A54 7F062F24 24090003 */  li    $t1, 3
/* 097A58 7F062F28 10000003 */  b     .L7F062F38
/* 097A5C 7F062F2C AFA900C8 */   sw    $t1, 0xc8($sp)
.L7F062F30:
/* 097A60 7F062F30 240F0002 */  li    $t7, 2
/* 097A64 7F062F34 AFAF00C8 */  sw    $t7, 0xc8($sp)
.L7F062F38:
/* 097A68 7F062F38 0FC1D1A1 */  jal   subdraw
/* 097A6C 7F062F3C 02202825 */   move  $a1, $s1
/* 097A70 7F062F40 8FB00098 */  lw    $s0, 0x98($sp)
/* 097A74 7F062F44 02802025 */  move  $a0, $s4
/* 097A78 7F062F48 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 097A7C 7F062F4C 24050400 */   li    $a1, 1024
/* 097A80 7F062F50 10400006 */  beqz  $v0, .L7F062F6C
/* 097A84 7F062F54 3C0AB600 */   lui   $t2, 0xb600
/* 097A88 7F062F58 02001025 */  move  $v0, $s0
/* 097A8C 7F062F5C 240B3000 */  li    $t3, 12288
/* 097A90 7F062F60 AC4B0004 */  sw    $t3, 4($v0)
/* 097A94 7F062F64 AC4A0000 */  sw    $t2, ($v0)
/* 097A98 7F062F68 26100008 */  addiu $s0, $s0, 8
.L7F062F6C:
/* 097A9C 7F062F6C 8E6D0300 */  lw    $t5, 0x300($s3)
/* 097AA0 7F062F70 8E640304 */  lw    $a0, 0x304($s3)
/* 097AA4 7F062F74 0FC22F52 */  jal   bondviewTransformManyPosToViewMatrix
/* 097AA8 7F062F78 85A5000E */   lh    $a1, 0xe($t5)
/* 097AAC 7F062F7C 0FC16322 */  jal   matrix_4x4_7F058C88
/* 097AB0 7F062F80 00000000 */   nop
/* 097AB4 7F062F84 3C0CBC00 */  lui   $t4, (0xBC00000E >> 16) # lui $t4, 0xbc00
/* 097AB8 7F062F88 358C000E */  ori   $t4, (0xBC00000E & 0xFFFF) # ori $t4, $t4, 0xe
/* 097ABC 7F062F8C 02008825 */  move  $s1, $s0
/* 097AC0 7F062F90 AE2C0000 */  sw    $t4, ($s1)
/* 097AC4 7F062F94 0C000F13 */  jal   viGetPerspNorm
/* 097AC8 7F062F98 26100008 */   addiu $s0, $s0, 8
/* 097ACC 7F062F9C 24010017 */  li    $at, 23
/* 097AD0 7F062FA0 16810006 */  bne   $s4, $at, .L7F062FBC
/* 097AD4 7F062FA4 AE220004 */   sw    $v0, 4($s1)
/* 097AD8 7F062FA8 02002025 */  move  $a0, $s0
/* 097ADC 7F062FAC 266501E4 */  addiu $a1, $s3, 0x1e4
/* 097AE0 7F062FB0 0FC18786 */  jal   sub_GAME_7F061E18
/* 097AE4 7F062FB4 00003025 */   move  $a2, $zero
/* 097AE8 7F062FB8 00408025 */  move  $s0, $v0
.L7F062FBC:
/* 097AEC 7F062FBC 26B50001 */  addiu $s5, $s5, 1
.L7F062FC0:
/* 097AF0 7F062FC0 24010002 */  li    $at, 2
/* 097AF4 7F062FC4 16A1FF28 */  bne   $s5, $at, .L7F062C68
/* 097AF8 7F062FC8 26D603A8 */   addiu $s6, $s6, 0x3a8
/* 097AFC 7F062FCC 8FAE00D0 */  lw    $t6, 0xd0($sp)
/* 097B00 7F062FD0 ADD00000 */  sw    $s0, ($t6)
/* 097B04 7F062FD4 8FBF0044 */  lw    $ra, 0x44($sp)
/* 097B08 7F062FD8 8FBE0040 */  lw    $fp, 0x40($sp)
/* 097B0C 7F062FDC 8FB7003C */  lw    $s7, 0x3c($sp)
/* 097B10 7F062FE0 8FB60038 */  lw    $s6, 0x38($sp)
/* 097B14 7F062FE4 8FB50034 */  lw    $s5, 0x34($sp)
/* 097B18 7F062FE8 8FB40030 */  lw    $s4, 0x30($sp)
/* 097B1C 7F062FEC 8FB3002C */  lw    $s3, 0x2c($sp)
/* 097B20 7F062FF0 8FB20028 */  lw    $s2, 0x28($sp)
/* 097B24 7F062FF4 8FB10024 */  lw    $s1, 0x24($sp)
/* 097B28 7F062FF8 8FB00020 */  lw    $s0, 0x20($sp)
/* 097B2C 7F062FFC 03E00008 */  jr    $ra
/* 097B30 7F063000 27BD00D0 */   addiu $sp, $sp, 0xd0
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.text
glabel sub_GAME_7F062BE4
/* 095A98 7F0630A8 27BDFF30 */  addiu $sp, $sp, -0xd0
/* 095A9C 7F0630AC AFB7003C */  sw    $s7, 0x3c($sp)
/* 095AA0 7F0630B0 3C0F8003 */  lui   $t7, %hi(D_80035CC0) # $t7, 0x8003
/* 095AA4 7F0630B4 27B7008C */  addiu $s7, $sp, 0x8c
/* 095AA8 7F0630B8 AFBF0044 */  sw    $ra, 0x44($sp)
/* 095AAC 7F0630BC AFBE0040 */  sw    $fp, 0x40($sp)
/* 095AB0 7F0630C0 AFB60038 */  sw    $s6, 0x38($sp)
/* 095AB4 7F0630C4 AFB50034 */  sw    $s5, 0x34($sp)
/* 095AB8 7F0630C8 AFB40030 */  sw    $s4, 0x30($sp)
/* 095ABC 7F0630CC AFB3002C */  sw    $s3, 0x2c($sp)
/* 095AC0 7F0630D0 AFB20028 */  sw    $s2, 0x28($sp)
/* 095AC4 7F0630D4 AFB10024 */  sw    $s1, 0x24($sp)
/* 095AC8 7F0630D8 AFB00020 */  sw    $s0, 0x20($sp)
/* 095ACC 7F0630DC AFA400D0 */  sw    $a0, 0xd0($sp)
/* 095AD0 7F0630E0 25EF1210 */  addiu $t7, %lo(D_80035CC0) # addiu $t7, $t7, 0x1210
/* 095AD4 7F0630E4 8C900000 */  lw    $s0, ($a0)
/* 095AD8 7F0630E8 25E8003C */  addiu $t0, $t7, 0x3c
/* 095ADC 7F0630EC 02E04825 */  move  $t1, $s7
.L7F0630F0:
/* 095AE0 7F0630F0 8DE10000 */  lw    $at, ($t7)
/* 095AE4 7F0630F4 25EF000C */  addiu $t7, $t7, 0xc
/* 095AE8 7F0630F8 2529000C */  addiu $t1, $t1, 0xc
/* 095AEC 7F0630FC AD21FFF4 */  sw    $at, -0xc($t1)
/* 095AF0 7F063100 8DE1FFF8 */  lw    $at, -8($t7)
/* 095AF4 7F063104 AD21FFF8 */  sw    $at, -8($t1)
/* 095AF8 7F063108 8DE1FFFC */  lw    $at, -4($t7)
/* 095AFC 7F06310C 15E8FFF8 */  bne   $t7, $t0, .L7F0630F0
/* 095B00 7F063110 AD21FFFC */   sw    $at, -4($t1)
/* 095B04 7F063114 8DE10000 */  lw    $at, ($t7)
/* 095B08 7F063118 3C1E8007 */  lui   $fp, %hi(g_CurrentPlayer) # $fp, 0x8007
/* 095B0C 7F06311C 27DE8BC0 */  addiu $fp, %lo(g_CurrentPlayer) # addiu $fp, $fp, -0x7440
/* 095B10 7F063120 0000A825 */  move  $s5, $zero
/* 095B14 7F063124 0000B025 */  move  $s6, $zero
/* 095B18 7F063128 AD210000 */  sw    $at, ($t1)
.L7F06312C:
/* 095B1C 7F06312C 8FCA0000 */  lw    $t2, ($fp)
/* 095B20 7F063130 02A02025 */  move  $a0, $s5
/* 095B24 7F063134 01569821 */  addu  $s3, $t2, $s6
/* 095B28 7F063138 0FC177BF */  jal   get_item_in_hand_or_watch_menu
/* 095B2C 7F06313C 26730868 */   addiu $s3, $s3, 0x868
/* 095B30 7F063140 826B000F */  lb    $t3, 0xf($s3)
/* 095B34 7F063144 0040A025 */  move  $s4, $v0
/* 095B38 7F063148 24010017 */  li    $at, 23
/* 095B3C 7F06314C 516000CD */  beql  $t3, $zero, .L7F063484
/* 095B40 7F063150 26B50001 */   addiu $s5, $s5, 1
/* 095B44 7F063154 10410005 */  beq   $v0, $at, .L7F06316C
/* 095B48 7F063158 02002025 */   move  $a0, $s0
/* 095B4C 7F06315C 266501E4 */  addiu $a1, $s3, 0x1e4
/* 095B50 7F063160 0FC188B7 */  jal   sub_GAME_7F061E18
/* 095B54 7F063164 00003025 */   move  $a2, $zero
/* 095B58 7F063168 00408025 */  move  $s0, $v0
.L7F06316C:
/* 095B5C 7F06316C 24010013 */  li    $at, 19
/* 095B60 7F063170 1281000C */  beq   $s4, $at, .L7F0631A4
/* 095B64 7F063174 02001025 */   move  $v0, $s0
/* 095B68 7F063178 24010012 */  li    $at, 18
/* 095B6C 7F06317C 12810009 */  beq   $s4, $at, .L7F0631A4
/* 095B70 7F063180 24010002 */   li    $at, 2
/* 095B74 7F063184 12810007 */  beq   $s4, $at, .L7F0631A4
/* 095B78 7F063188 24010003 */   li    $at, 3
/* 095B7C 7F06318C 12810005 */  beq   $s4, $at, .L7F0631A4
/* 095B80 7F063190 24010014 */   li    $at, 20
/* 095B84 7F063194 12810003 */  beq   $s4, $at, .L7F0631A4
/* 095B88 7F063198 24010015 */   li    $at, 21
/* 095B8C 7F06319C 56810028 */  bnel  $s4, $at, .Leu7F063240
/* 095B90 7F0631A0 3C0BBC00 */   lui   $t3, 0xbc00
.L7F0631A4:
/* 095B94 7F0631A4 26100008 */  addiu $s0, $s0, 8
/* 095B98 7F0631A8 3C0CBC00 */  lui   $t4, (0xBC000002 >> 16) # lui $t4, 0xbc00
/* 095B9C 7F0631AC 3C0D8000 */  lui   $t5, (0x80000040 >> 16) # lui $t5, 0x8000
/* 095BA0 7F0631B0 35AD0040 */  ori   $t5, (0x80000040 & 0xFFFF) # ori $t5, $t5, 0x40
/* 095BA4 7F0631B4 358C0002 */  ori   $t4, (0xBC000002 & 0xFFFF) # ori $t4, $t4, 2
/* 095BA8 7F0631B8 02001825 */  move  $v1, $s0
/* 095BAC 7F0631BC 26100008 */  addiu $s0, $s0, 8
/* 095BB0 7F0631C0 AC4C0000 */  sw    $t4, ($v0)
/* 095BB4 7F0631C4 AC4D0004 */  sw    $t5, 4($v0)
/* 095BB8 7F0631C8 3C0E0386 */  lui   $t6, (0x03860010 >> 16) # lui $t6, 0x386
/* 095BBC 7F0631CC 3C198003 */  lui   $t9, %hi(g_WeaponEnvmapLight + 0x8) # $t9, 0x8003
/* 095BC0 7F0631D0 2739D998 */  addiu $t9, %lo(g_WeaponEnvmapLight + 0x8) # addiu $t9, $t9, -0x2668
/* 095BC4 7F0631D4 35CE0010 */  ori   $t6, (0x03860010 & 0xFFFF) # ori $t6, $t6, 0x10
/* 095BC8 7F0631D8 02002025 */  move  $a0, $s0
/* 095BCC 7F0631DC AC6E0000 */  sw    $t6, ($v1)
/* 095BD0 7F0631E0 AC790004 */  sw    $t9, 4($v1)
/* 095BD4 7F0631E4 3C180388 */  lui   $t8, (0x03880010 >> 16) # lui $t8, 0x388
/* 095BD8 7F0631E8 3C088003 */  lui   $t0, %hi(g_WeaponEnvmapLight) # $t0, 0x8003
/* 095BDC 7F0631EC 2508D990 */  addiu $t0, %lo(g_WeaponEnvmapLight) # addiu $t0, $t0, -0x2670
/* 095BE0 7F0631F0 37180010 */  ori   $t8, (0x03880010 & 0xFFFF) # ori $t8, $t8, 0x10
/* 095BE4 7F0631F4 26100008 */  addiu $s0, $s0, 8
/* 095BE8 7F0631F8 3C0F0384 */  lui   $t7, (0x03840010 >> 16) # lui $t7, 0x384
/* 095BEC 7F0631FC AC980000 */  sw    $t8, ($a0)
/* 095BF0 7F063200 AC880004 */  sw    $t0, 4($a0)
/* 095BF4 7F063204 35EF0010 */  ori   $t7, (0x03840010 & 0xFFFF) # ori $t7, $t7, 0x10
/* 095BF8 7F063208 02008825 */  move  $s1, $s0
/* 095BFC 7F06320C AE2F0000 */  sw    $t7, ($s1)
/* 095C00 7F063210 0FC1E13D */  jal   sub_GAME_7F078474
/* 095C04 7F063214 26100008 */   addiu $s0, $s0, 8
/* 095C08 7F063218 3C090382 */  lui   $t1, (0x03820010 >> 16) # lui $t1, 0x382
/* 095C0C 7F06321C 35290010 */  ori   $t1, (0x03820010 & 0xFFFF) # ori $t1, $t1, 0x10
/* 095C10 7F063220 AE220004 */  sw    $v0, 4($s1)
/* 095C14 7F063224 02009025 */  move  $s2, $s0
/* 095C18 7F063228 AE490000 */  sw    $t1, ($s2)
/* 095C1C 7F06322C 0FC1E13D */  jal   sub_GAME_7F078474
/* 095C20 7F063230 26100008 */   addiu $s0, $s0, 8
/* 095C24 7F063234 244A0010 */  addiu $t2, $v0, 0x10
/* 095C28 7F063238 AE4A0004 */  sw    $t2, 4($s2)
/* 095C2C 7F06323C 3C0BBC00 */  lui   $t3, (0xBC00000E >> 16) # lui $t3, 0xbc00
.Leu7F063240:
/* 095C30 7F063240 3C014396 */  li    $at, 0x43960000 # 300.000000
/* 095C34 7F063244 44817000 */  mtc1  $at, $f14
/* 095C38 7F063248 356B000E */  ori   $t3, (0xBC00000E & 0xFFFF) # ori $t3, $t3, 0xe
/* 095C3C 7F06324C 02008825 */  move  $s1, $s0
/* 095C40 7F063250 44806000 */  mtc1  $zero, $f12
/* 095C44 7F063254 AE2B0000 */  sw    $t3, ($s1)
/* 095C48 7F063258 0FC16789 */  jal   matrix_4x4_calc_depth_scale
/* 095C4C 7F06325C 26100008 */   addiu $s0, $s0, 8
/* 095C50 7F063260 AE220004 */  sw    $v0, 4($s1)
/* 095C54 7F063264 8E630300 */  lw    $v1, 0x300($s3)
/* 095C58 7F063268 846C000C */  lh    $t4, 0xc($v1)
/* 095C5C 7F06326C 29810011 */  slti  $at, $t4, 0x11
/* 095C60 7F063270 5420002D */  bnezl $at, .L7F063328
/* 095C64 7F063274 8FC20000 */   lw    $v0, ($fp)
/* 095C68 7F063278 8C620008 */  lw    $v0, 8($v1)
/* 095C6C 7F06327C 267102F8 */  addiu $s1, $s3, 0x2f8
/* 095C70 7F063280 02202025 */  move  $a0, $s1
/* 095C74 7F063284 8C4D0040 */  lw    $t5, 0x40($v0)
/* 095C78 7F063288 51A00027 */  beql  $t5, $zero, .L7F063328
/* 095C7C 7F06328C 8FC20000 */   lw    $v0, ($fp)
/* 095C80 7F063290 0FC1B3A3 */  jal   modelGetNodeRwData
/* 095C84 7F063294 8C450044 */   lw    $a1, 0x44($v0)
/* 095C88 7F063298 10400003 */  beqz  $v0, .L7F0632A8
/* 095C8C 7F06329C 24010019 */   li    $at, 25
/* 095C90 7F0632A0 240E0001 */  li    $t6, 1
/* 095C94 7F0632A4 AC4E0000 */  sw    $t6, ($v0)
.L7F0632A8:
/* 095C98 7F0632A8 16810013 */  bne   $s4, $at, .L7F0632F8
/* 095C9C 7F0632AC 02202025 */   move  $a0, $s1
/* 095CA0 7F0632B0 3C048006 */  lui   $a0, %hi(g_UnknownAnimController) # $a0, 0x8006
/* 095CA4 7F0632B4 3C058007 */  lui   $a1, %hi(crosshairimage) # $a1, 0x8007
/* 095CA8 7F0632B8 8CA544F4 */  lw    $a1, %lo(crosshairimage)($a1)
/* 095CAC 7F0632BC 0FC12847 */  jal   save_img_index_to_obj_ani_slot
/* 095CB0 7F0632C0 24844B50 */   addiu $a0, %lo(g_UnknownAnimController) # addiu $a0, $a0, 0x4b50
/* 095CB4 7F0632C4 8E790300 */  lw    $t9, 0x300($s3)
/* 095CB8 7F0632C8 3C068006 */  lui   $a2, %hi(g_UnknownAnimController) # $a2, 0x8006
/* 095CBC 7F0632CC 24080004 */  li    $t0, 4
/* 095CC0 7F0632D0 8F380008 */  lw    $t8, 8($t9)
/* 095CC4 7F0632D4 24C64B50 */  addiu $a2, %lo(g_UnknownAnimController) # addiu $a2, $a2, 0x4b50
/* 095CC8 7F0632D8 02202025 */  move  $a0, $s1
/* 095CCC 7F0632DC 8F050040 */  lw    $a1, 0x40($t8)
/* 095CD0 7F0632E0 AFA80014 */  sw    $t0, 0x14($sp)
/* 095CD4 7F0632E4 AFA00010 */  sw    $zero, 0x10($sp)
/* 095CD8 7F0632E8 0FC12849 */  jal   process_monitor_animation_microcode
/* 095CDC 7F0632EC 02003825 */   move  $a3, $s0
/* 095CE0 7F0632F0 1000000C */  b     .L7F063324
/* 095CE4 7F0632F4 00408025 */   move  $s0, $v0
.L7F0632F8:
/* 095CE8 7F0632F8 8E6F0300 */  lw    $t7, 0x300($s3)
/* 095CEC 7F0632FC 3C068006 */  lui   $a2, %hi(g_TaserAnimController) # $a2, 0x8006
/* 095CF0 7F063300 240A0001 */  li    $t2, 1
/* 095CF4 7F063304 8DE90008 */  lw    $t1, 8($t7)
/* 095CF8 7F063308 24C64BC8 */  addiu $a2, %lo(g_TaserAnimController) # addiu $a2, $a2, 0x4bc8
/* 095CFC 7F06330C 02003825 */  move  $a3, $s0
/* 095D00 7F063310 8D250040 */  lw    $a1, 0x40($t1)
/* 095D04 7F063314 AFAA0014 */  sw    $t2, 0x14($sp)
/* 095D08 7F063318 0FC12849 */  jal   process_monitor_animation_microcode
/* 095D0C 7F06331C AFA00010 */   sw    $zero, 0x10($sp)
/* 095D10 7F063320 00408025 */  move  $s0, $v0
.L7F063324:
/* 095D14 7F063324 8FC20000 */  lw    $v0, ($fp)
.L7F063328:
/* 095D18 7F063328 240B0004 */  li    $t3, 4
/* 095D1C 7F06332C AFB00098 */  sw    $s0, 0x98($sp)
/* 095D20 7F063330 AFAB00BC */  sw    $t3, 0xbc($sp)
/* 095D24 7F063334 904D0FD4 */  lbu   $t5, 0xfd4($v0)
/* 095D28 7F063338 90580FD5 */  lbu   $t8, 0xfd5($v0)
/* 095D2C 7F06333C 904C0FD7 */  lbu   $t4, 0xfd7($v0)
/* 095D30 7F063340 90490FD6 */  lbu   $t1, 0xfd6($v0)
/* 095D34 7F063344 000D7600 */  sll   $t6, $t5, 0x18
/* 095D38 7F063348 00184400 */  sll   $t0, $t8, 0x10
/* 095D3C 7F06334C 018EC825 */  or    $t9, $t4, $t6
/* 095D40 7F063350 03287825 */  or    $t7, $t9, $t0
/* 095D44 7F063354 00095200 */  sll   $t2, $t1, 8
/* 095D48 7F063358 01EA5825 */  or    $t3, $t7, $t2
/* 095D4C 7F06335C AFAB00C0 */  sw    $t3, 0xc0($sp)
/* 095D50 7F063360 AFA00090 */  sw    $zero, 0x90($sp)
/* 095D54 7F063364 0FC16443 */  jal   matrix_4x4_7F058C64
/* 095D58 7F063368 267102F8 */   addiu $s1, $s3, 0x2f8
/* 095D5C 7F06336C 24010019 */  li    $at, 25
/* 095D60 7F063370 56810011 */  bnel  $s4, $at, .L7F0633B8
/* 095D64 7F063374 02802025 */   move  $a0, $s4
/* 095D68 7F063378 8E620220 */  lw    $v0, 0x220($s3)
/* 095D6C 7F06337C 5040000E */  beql  $v0, $zero, .L7F0633B8
/* 095D70 7F063380 02802025 */   move  $a0, $s4
/* 095D74 7F063384 8C500014 */  lw    $s0, 0x14($v0)
/* 095D78 7F063388 02E02025 */  move  $a0, $s7
/* 095D7C 7F06338C 0FC1D1D6 */  jal   subdraw
/* 095D80 7F063390 02002825 */   move  $a1, $s0
/* 095D84 7F063394 8E0D0008 */  lw    $t5, 8($s0)
/* 095D88 7F063398 8E04000C */  lw    $a0, 0xc($s0)
/* 095D8C 7F06339C 0FC2300F */  jal   bondviewTransformManyPosToViewMatrix
/* 095D90 7F0633A0 85A5000E */   lh    $a1, 0xe($t5)
/* 095D94 7F0633A4 8E6C0224 */  lw    $t4, 0x224($s3)
/* 095D98 7F0633A8 51800003 */  beql  $t4, $zero, .L7F0633B8
/* 095D9C 7F0633AC 02802025 */   move  $a0, $s4
/* 095DA0 7F0633B0 AE600220 */  sw    $zero, 0x220($s3)
/* 095DA4 7F0633B4 02802025 */  move  $a0, $s4
.L7F0633B8:
/* 095DA8 7F0633B8 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 095DAC 7F0633BC 24050400 */   li    $a1, 1024
/* 095DB0 7F0633C0 1040000E */  beqz  $v0, .L7F0633FC
/* 095DB4 7F0633C4 02E02025 */   move  $a0, $s7
/* 095DB8 7F0633C8 8FAE0098 */  lw    $t6, 0x98($sp)
/* 095DBC 7F0633CC 3C19B600 */  lui   $t9, 0xb600
/* 095DC0 7F0633D0 24083000 */  li    $t0, 12288
/* 095DC4 7F0633D4 25D80008 */  addiu $t8, $t6, 8
/* 095DC8 7F0633D8 AFB80098 */  sw    $t8, 0x98($sp)
/* 095DCC 7F0633DC ADC80004 */  sw    $t0, 4($t6)
/* 095DD0 7F0633E0 16A00004 */  bnez  $s5, .L7F0633F4
/* 095DD4 7F0633E4 ADD90000 */   sw    $t9, ($t6)
/* 095DD8 7F0633E8 24090003 */  li    $t1, 3
/* 095DDC 7F0633EC 10000003 */  b     .L7F0633FC
/* 095DE0 7F0633F0 AFA900C8 */   sw    $t1, 0xc8($sp)
.L7F0633F4:
/* 095DE4 7F0633F4 240F0002 */  li    $t7, 2
/* 095DE8 7F0633F8 AFAF00C8 */  sw    $t7, 0xc8($sp)
.L7F0633FC:
/* 095DEC 7F0633FC 0FC1D1D6 */  jal   subdraw
/* 095DF0 7F063400 02202825 */   move  $a1, $s1
/* 095DF4 7F063404 8FB00098 */  lw    $s0, 0x98($sp)
/* 095DF8 7F063408 02802025 */  move  $a0, $s4
/* 095DFC 7F06340C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 095E00 7F063410 24050400 */   li    $a1, 1024
/* 095E04 7F063414 10400006 */  beqz  $v0, .L7F063430
/* 095E08 7F063418 3C0AB600 */   lui   $t2, 0xb600
/* 095E0C 7F06341C 02001025 */  move  $v0, $s0
/* 095E10 7F063420 240B3000 */  li    $t3, 12288
/* 095E14 7F063424 AC4B0004 */  sw    $t3, 4($v0)
/* 095E18 7F063428 AC4A0000 */  sw    $t2, ($v0)
/* 095E1C 7F06342C 26100008 */  addiu $s0, $s0, 8
.L7F063430:
/* 095E20 7F063430 8E6D0300 */  lw    $t5, 0x300($s3)
/* 095E24 7F063434 8E640304 */  lw    $a0, 0x304($s3)
/* 095E28 7F063438 0FC2300F */  jal   bondviewTransformManyPosToViewMatrix
/* 095E2C 7F06343C 85A5000E */   lh    $a1, 0xe($t5)
/* 095E30 7F063440 0FC1644C */  jal   matrix_4x4_7F058C88
/* 095E34 7F063444 00000000 */   nop
/* 095E38 7F063448 3C0CBC00 */  lui   $t4, (0xBC00000E >> 16) # lui $t4, 0xbc00
/* 095E3C 7F06344C 358C000E */  ori   $t4, (0xBC00000E & 0xFFFF) # ori $t4, $t4, 0xe
/* 095E40 7F063450 02008825 */  move  $s1, $s0
/* 095E44 7F063454 AE2C0000 */  sw    $t4, ($s1)
/* 095E48 7F063458 0C000DA7 */  jal   viGetPerspNorm
/* 095E4C 7F06345C 26100008 */   addiu $s0, $s0, 8
/* 095E50 7F063460 24010017 */  li    $at, 23
/* 095E54 7F063464 16810006 */  bne   $s4, $at, .L7F063480
/* 095E58 7F063468 AE220004 */   sw    $v0, 4($s1)
/* 095E5C 7F06346C 02002025 */  move  $a0, $s0
/* 095E60 7F063470 266501E4 */  addiu $a1, $s3, 0x1e4
/* 095E64 7F063474 0FC188B7 */  jal   sub_GAME_7F061E18
/* 095E68 7F063478 00003025 */   move  $a2, $zero
/* 095E6C 7F06347C 00408025 */  move  $s0, $v0
.L7F063480:
/* 095E70 7F063480 26B50001 */  addiu $s5, $s5, 1
.L7F063484:
/* 095E74 7F063484 24010002 */  li    $at, 2
/* 095E78 7F063488 16A1FF28 */  bne   $s5, $at, .L7F06312C
/* 095E7C 7F06348C 26D603A8 */   addiu $s6, $s6, 0x3a8
/* 095E80 7F063490 8FAE00D0 */  lw    $t6, 0xd0($sp)
/* 095E84 7F063494 ADD00000 */  sw    $s0, ($t6)
/* 095E88 7F063498 8FBF0044 */  lw    $ra, 0x44($sp)
/* 095E8C 7F06349C 8FBE0040 */  lw    $fp, 0x40($sp)
/* 095E90 7F0634A0 8FB7003C */  lw    $s7, 0x3c($sp)
/* 095E94 7F0634A4 8FB60038 */  lw    $s6, 0x38($sp)
/* 095E98 7F0634A8 8FB50034 */  lw    $s5, 0x34($sp)
/* 095E9C 7F0634AC 8FB40030 */  lw    $s4, 0x30($sp)
/* 095EA0 7F0634B0 8FB3002C */  lw    $s3, 0x2c($sp)
/* 095EA4 7F0634B4 8FB20028 */  lw    $s2, 0x28($sp)
/* 095EA8 7F0634B8 8FB10024 */  lw    $s1, 0x24($sp)
/* 095EAC 7F0634BC 8FB00020 */  lw    $s0, 0x20($sp)
/* 095EB0 7F0634C0 03E00008 */  jr    $ra
/* 095EB4 7F0634C4 27BD00D0 */   addiu $sp, $sp, 0xd0
)
#endif
#endif





#ifdef NONMATCHING
void set_enviro_fog_for_items_in_solo_watch_menu(void) {

}
#else
GLOBAL_ASM(
.text
glabel set_enviro_fog_for_items_in_solo_watch_menu
/* 097B34 7F063004 27BDFE68 */  addiu $sp, $sp, -0x198
/* 097B38 7F063008 3C0F8003 */  lui   $t7, %hi(D_80035D00)
/* 097B3C 7F06300C 25EF5D00 */  addiu $t7, %lo(D_80035D00) # addiu $t7, $t7, 0x5d00
/* 097B40 7F063010 AFBF0024 */  sw    $ra, 0x24($sp)
/* 097B44 7F063014 AFB30020 */  sw    $s3, 0x20($sp)
/* 097B48 7F063018 AFB2001C */  sw    $s2, 0x1c($sp)
/* 097B4C 7F06301C AFB10018 */  sw    $s1, 0x18($sp)
/* 097B50 7F063020 AFB00014 */  sw    $s0, 0x14($sp)
/* 097B54 7F063024 AFA40198 */  sw    $a0, 0x198($sp)
/* 097B58 7F063028 AFA601A0 */  sw    $a2, 0x1a0($sp)
/* 097B5C 7F06302C AFA701A4 */  sw    $a3, 0x1a4($sp)
/* 097B60 7F063030 25E8003C */  addiu $t0, $t7, 0x3c
/* 097B64 7F063034 27AE0158 */  addiu $t6, $sp, 0x158
.L7F063038:
/* 097B68 7F063038 8DE10000 */  lw    $at, ($t7)
/* 097B6C 7F06303C 25EF000C */  addiu $t7, $t7, 0xc
/* 097B70 7F063040 25CE000C */  addiu $t6, $t6, 0xc
/* 097B74 7F063044 ADC1FFF4 */  sw    $at, -0xc($t6)
/* 097B78 7F063048 8DE1FFF8 */  lw    $at, -8($t7)
/* 097B7C 7F06304C ADC1FFF8 */  sw    $at, -8($t6)
/* 097B80 7F063050 8DE1FFFC */  lw    $at, -4($t7)
/* 097B84 7F063054 15E8FFF8 */  bne   $t7, $t0, .L7F063038
/* 097B88 7F063058 ADC1FFFC */   sw    $at, -4($t6)
/* 097B8C 7F06305C 8DE10000 */  lw    $at, ($t7)
/* 097B90 7F063060 00002025 */  move  $a0, $zero
/* 097B94 7F063064 ADC10000 */  sw    $at, ($t6)
/* 097B98 7F063068 2401001E */  li    $at, 30
/* 097B9C 7F06306C 10A10003 */  beq   $a1, $at, .L7F06307C
/* 097BA0 7F063070 24010017 */   li    $at, 23
/* 097BA4 7F063074 14A10002 */  bne   $a1, $at, .L7F063080
/* 097BA8 7F063078 00000000 */   nop
.L7F06307C:
/* 097BAC 7F06307C 2405003C */  li    $a1, 60
.L7F063080:
/* 097BB0 7F063080 0FC176A3 */  jal   sub_GAME_7F05DA8C
/* 097BB4 7F063084 AFA5019C */   sw    $a1, 0x19c($sp)
/* 097BB8 7F063088 0FC173AF */  jal   Gun_hand_without_item
/* 097BBC 7F06308C 00002025 */   move  $a0, $zero
/* 097BC0 7F063090 10400005 */  beqz  $v0, .L7F0630A8
/* 097BC4 7F063094 00000000 */   nop
/* 097BC8 7F063098 0FC173C0 */  jal   get_itemtype_in_hand
/* 097BCC 7F06309C 00002025 */   move  $a0, $zero
/* 097BD0 7F0630A0 14400003 */  bnez  $v0, .L7F0630B0
/* 097BD4 7F0630A4 3C128008 */   lui   $s2, %hi(g_CurrentPlayer)
.L7F0630A8:
/* 097BD8 7F0630A8 10000104 */  b     .L7F0634BC
/* 097BDC 7F0630AC 8FA20198 */   lw    $v0, 0x198($sp)
.L7F0630B0:
/* 097BE0 7F0630B0 8E52A0B0 */  lw    $s2, %lo(g_CurrentPlayer)($s2)
/* 097BE4 7F0630B4 8FA4019C */  lw    $a0, 0x19c($sp)
/* 097BE8 7F0630B8 0FC17412 */  jal   get_ptr_weapon_model_header_line
/* 097BEC 7F0630BC 26520810 */   addiu $s2, $s2, 0x810
/* 097BF0 7F0630C0 104000FD */  beqz  $v0, .L7F0634B8
/* 097BF4 7F0630C4 8FA4019C */   lw    $a0, 0x19c($sp)
/* 097BF8 7F0630C8 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 097BFC 7F0630CC 24054000 */   li    $a1, 16384
/* 097C00 7F0630D0 544000FA */  bnezl $v0, .L7F0634BC
/* 097C04 7F0630D4 8FA20198 */   lw    $v0, 0x198($sp)
/* 097C08 7F0630D8 8644000E */  lh    $a0, 0xe($s2)
/* 097C0C 7F0630DC 00044980 */  sll   $t1, $a0, 6
/* 097C10 7F0630E0 0FC2F5C5 */  jal   dynAllocate
/* 097C14 7F0630E4 01202025 */   move  $a0, $t1
/* 097C18 7F0630E8 864A000E */  lh    $t2, 0xe($s2)
/* 097C1C 7F0630EC 00408025 */  move  $s0, $v0
/* 097C20 7F0630F0 00008825 */  move  $s1, $zero
/* 097C24 7F0630F4 19400009 */  blez  $t2, .L7F06311C
/* 097C28 7F0630F8 00115980 */   sll   $t3, $s1, 6
.L7F0630FC:
/* 097C2C 7F0630FC 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 097C30 7F063100 01702021 */   addu  $a0, $t3, $s0
/* 097C34 7F063104 864C000E */  lh    $t4, 0xe($s2)
/* 097C38 7F063108 26310001 */  addiu $s1, $s1, 1
/* 097C3C 7F06310C 022C082A */  slt   $at, $s1, $t4
/* 097C40 7F063110 5420FFFA */  bnezl $at, .L7F0630FC
/* 097C44 7F063114 00115980 */   sll   $t3, $s1, 6
/* 097C48 7F063118 00008825 */  move  $s1, $zero
.L7F06311C:
/* 097C4C 7F06311C AFB00144 */  sw    $s0, 0x144($sp)
/* 097C50 7F063120 0FC1D73D */  jal   modelCalculateRwDataLen
/* 097C54 7F063124 02402025 */   move  $a0, $s2
/* 097C58 7F063128 27B30138 */  addiu $s3, $sp, 0x138
/* 097C5C 7F06312C 02602025 */  move  $a0, $s3
/* 097C60 7F063130 02402825 */  move  $a1, $s2
/* 097C64 7F063134 0FC1D7DA */  jal   modelInit
/* 097C68 7F063138 27A600B8 */   addiu $a2, $sp, 0xb8
/* 097C6C 7F06313C 02602025 */  move  $a0, $s3
/* 097C70 7F063140 0FC17A5E */  jal   sub_GAME_7F05E978
/* 097C74 7F063144 00002825 */   move  $a1, $zero
/* 097C78 7F063148 02602025 */  move  $a0, $s3
/* 097C7C 7F06314C 0FC17AA5 */  jal   sub_GAME_7F05EA94
/* 097C80 7F063150 24050001 */   li    $a1, 1
/* 097C84 7F063154 8E4D0008 */  lw    $t5, 8($s2)
/* 097C88 7F063158 8DA50004 */  lw    $a1, 4($t5)
/* 097C8C 7F06315C 50A00007 */  beql  $a1, $zero, .L7F06317C
/* 097C90 7F063160 8FA401A0 */   lw    $a0, 0x1a0($sp)
/* 097C94 7F063164 0FC1B1E7 */  jal   modelGetNodeRwData
/* 097C98 7F063168 02602025 */   move  $a0, $s3
/* 097C9C 7F06316C 50400003 */  beql  $v0, $zero, .L7F06317C
/* 097CA0 7F063170 8FA401A0 */   lw    $a0, 0x1a0($sp)
/* 097CA4 7F063174 AC400000 */  sw    $zero, ($v0)
/* 097CA8 7F063178 8FA401A0 */  lw    $a0, 0x1a0($sp)
.L7F06317C:
/* 097CAC 7F06317C 0FC16008 */  jal   matrix_4x4_copy
/* 097CB0 7F063180 02002825 */   move  $a1, $s0
/* 097CB4 7F063184 8E580004 */  lw    $t8, 4($s2)
/* 097CB8 7F063188 3C198004 */  lui   $t9, %hi(skeleton_gun_revolver)
/* 097CBC 7F06318C 2739C76C */  addiu $t9, %lo(skeleton_gun_revolver) # addiu $t9, $t9, -0x3894
/* 097CC0 7F063190 57380018 */  bnel  $t9, $t8, .L7F0631F4
/* 097CC4 7F063194 8E420008 */   lw    $v0, 8($s2)
/* 097CC8 7F063198 8E420008 */  lw    $v0, 8($s2)
/* 097CCC 7F06319C 27A50074 */  addiu $a1, $sp, 0x74
/* 097CD0 7F0631A0 8C430010 */  lw    $v1, 0x10($v0)
/* 097CD4 7F0631A4 50600009 */  beql  $v1, $zero, .L7F0631CC
/* 097CD8 7F0631A8 8C430014 */   lw    $v1, 0x14($v0)
/* 097CDC 7F0631AC 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 097CE0 7F0631B0 8C640004 */   lw    $a0, 4($v1)
/* 097CE4 7F0631B4 8FA401A0 */  lw    $a0, 0x1a0($sp)
/* 097CE8 7F0631B8 27A50074 */  addiu $a1, $sp, 0x74
/* 097CEC 7F0631BC 0FC16032 */  jal   matrix_4x4_multiply
/* 097CF0 7F0631C0 260600C0 */   addiu $a2, $s0, 0xc0
/* 097CF4 7F0631C4 8E420008 */  lw    $v0, 8($s2)
/* 097CF8 7F0631C8 8C430014 */  lw    $v1, 0x14($v0)
.L7F0631CC:
/* 097CFC 7F0631CC 27A50074 */  addiu $a1, $sp, 0x74
/* 097D00 7F0631D0 50600008 */  beql  $v1, $zero, .L7F0631F4
/* 097D04 7F0631D4 8E420008 */   lw    $v0, 8($s2)
/* 097D08 7F0631D8 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 097D0C 7F0631DC 8C640004 */   lw    $a0, 4($v1)
/* 097D10 7F0631E0 8FA401A0 */  lw    $a0, 0x1a0($sp)
/* 097D14 7F0631E4 27A50074 */  addiu $a1, $sp, 0x74
/* 097D18 7F0631E8 0FC16032 */  jal   matrix_4x4_multiply
/* 097D1C 7F0631EC 26060100 */   addiu $a2, $s0, 0x100
/* 097D20 7F0631F0 8E420008 */  lw    $v0, 8($s2)
.L7F0631F4:
/* 097D24 7F0631F4 8C440018 */  lw    $a0, 0x18($v0)
/* 097D28 7F0631F8 50800011 */  beql  $a0, $zero, .L7F063240
/* 097D2C 7F0631FC 8C44001C */   lw    $a0, 0x1c($v0)
/* 097D30 7F063200 8C880004 */  lw    $t0, 4($a0)
/* 097D34 7F063204 00002825 */  move  $a1, $zero
/* 097D38 7F063208 0FC1B15C */  jal   modelFindNodeMtxIndex
/* 097D3C 7F06320C AFA8005C */   sw    $t0, 0x5c($sp)
/* 097D40 7F063210 AFA20058 */  sw    $v0, 0x58($sp)
/* 097D44 7F063214 8FA4005C */  lw    $a0, 0x5c($sp)
/* 097D48 7F063218 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 097D4C 7F06321C 27A50074 */   addiu $a1, $sp, 0x74
/* 097D50 7F063220 8FAF0058 */  lw    $t7, 0x58($sp)
/* 097D54 7F063224 8FA401A0 */  lw    $a0, 0x1a0($sp)
/* 097D58 7F063228 27A50074 */  addiu $a1, $sp, 0x74
/* 097D5C 7F06322C 000F7180 */  sll   $t6, $t7, 6
/* 097D60 7F063230 0FC16032 */  jal   matrix_4x4_multiply
/* 097D64 7F063234 01D03021 */   addu  $a2, $t6, $s0
/* 097D68 7F063238 8E420008 */  lw    $v0, 8($s2)
/* 097D6C 7F06323C 8C44001C */  lw    $a0, 0x1c($v0)
.L7F063240:
/* 097D70 7F063240 50800010 */  beql  $a0, $zero, .L7F063284
/* 097D74 7F063244 864C000C */   lh    $t4, 0xc($s2)
/* 097D78 7F063248 8C890004 */  lw    $t1, 4($a0)
/* 097D7C 7F06324C 00002825 */  move  $a1, $zero
/* 097D80 7F063250 0FC1B15C */  jal   modelFindNodeMtxIndex
/* 097D84 7F063254 AFA90054 */   sw    $t1, 0x54($sp)
/* 097D88 7F063258 AFA20050 */  sw    $v0, 0x50($sp)
/* 097D8C 7F06325C 8FA40054 */  lw    $a0, 0x54($sp)
/* 097D90 7F063260 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 097D94 7F063264 27A50074 */   addiu $a1, $sp, 0x74
/* 097D98 7F063268 8FAA0050 */  lw    $t2, 0x50($sp)
/* 097D9C 7F06326C 8FA401A0 */  lw    $a0, 0x1a0($sp)
/* 097DA0 7F063270 27A50074 */  addiu $a1, $sp, 0x74
/* 097DA4 7F063274 000A5980 */  sll   $t3, $t2, 6
/* 097DA8 7F063278 0FC16032 */  jal   matrix_4x4_multiply
/* 097DAC 7F06327C 01703021 */   addu  $a2, $t3, $s0
/* 097DB0 7F063280 864C000C */  lh    $t4, 0xc($s2)
.L7F063284:
/* 097DB4 7F063284 00008025 */  move  $s0, $zero
/* 097DB8 7F063288 29810013 */  slti  $at, $t4, 0x13
/* 097DBC 7F06328C 14200019 */  bnez  $at, .L7F0632F4
/* 097DC0 7F063290 00000000 */   nop
/* 097DC4 7F063294 8E4D0008 */  lw    $t5, 8($s2)
.L7F063298:
/* 097DC8 7F063298 01B0C821 */  addu  $t9, $t5, $s0
/* 097DCC 7F06329C 8F250048 */  lw    $a1, 0x48($t9)
/* 097DD0 7F0632A0 50A00007 */  beql  $a1, $zero, .L7F0632C0
/* 097DD4 7F0632A4 8E480008 */   lw    $t0, 8($s2)
/* 097DD8 7F0632A8 0FC1B1E7 */  jal   modelGetNodeRwData
/* 097DDC 7F0632AC 02602025 */   move  $a0, $s3
/* 097DE0 7F0632B0 10400002 */  beqz  $v0, .L7F0632BC
/* 097DE4 7F0632B4 24180001 */   li    $t8, 1
/* 097DE8 7F0632B8 AC580000 */  sw    $t8, ($v0)
.L7F0632BC:
/* 097DEC 7F0632BC 8E480008 */  lw    $t0, 8($s2)
.L7F0632C0:
/* 097DF0 7F0632C0 01107821 */  addu  $t7, $t0, $s0
/* 097DF4 7F0632C4 8DE5005C */  lw    $a1, 0x5c($t7)
/* 097DF8 7F0632C8 50A00007 */  beql  $a1, $zero, .L7F0632E8
/* 097DFC 7F0632CC 26100004 */   addiu $s0, $s0, 4
/* 097E00 7F0632D0 0FC1B1E7 */  jal   modelGetNodeRwData
/* 097E04 7F0632D4 02602025 */   move  $a0, $s3
/* 097E08 7F0632D8 10400002 */  beqz  $v0, .L7F0632E4
/* 097E0C 7F0632DC 240E0001 */   li    $t6, 1
/* 097E10 7F0632E0 AC4E0000 */  sw    $t6, ($v0)
.L7F0632E4:
/* 097E14 7F0632E4 26100004 */  addiu $s0, $s0, 4
.L7F0632E8:
/* 097E18 7F0632E8 24010014 */  li    $at, 20
/* 097E1C 7F0632EC 5601FFEA */  bnel  $s0, $at, .L7F063298
/* 097E20 7F0632F0 8E4D0008 */   lw    $t5, 8($s2)
.L7F0632F4:
/* 097E24 7F0632F4 0FC1BBF1 */  jal   modelUpdateNodeRelations
/* 097E28 7F0632F8 02602025 */   move  $a0, $s3
/* 097E2C 7F0632FC 8FA2019C */  lw    $v0, 0x19c($sp)
/* 097E30 7F063300 24010013 */  li    $at, 19
/* 097E34 7F063304 3C09BC00 */  lui   $t1, (0xBC000002 >> 16) # lui $t1, 0xbc00
/* 097E38 7F063308 1041000C */  beq   $v0, $at, .L7F06333C
/* 097E3C 7F06330C 35290002 */   ori   $t1, (0xBC000002 & 0xFFFF) # ori $t1, $t1, 2
/* 097E40 7F063310 24010012 */  li    $at, 18
/* 097E44 7F063314 10410009 */  beq   $v0, $at, .L7F06333C
/* 097E48 7F063318 24010002 */   li    $at, 2
/* 097E4C 7F06331C 10410007 */  beq   $v0, $at, .L7F06333C
/* 097E50 7F063320 24010003 */   li    $at, 3
/* 097E54 7F063324 10410005 */  beq   $v0, $at, .L7F06333C
/* 097E58 7F063328 24010014 */   li    $at, 20
/* 097E5C 7F06332C 10410003 */  beq   $v0, $at, .L7F06333C
/* 097E60 7F063330 24010015 */   li    $at, 21
/* 097E64 7F063334 54410028 */  bnel  $v0, $at, .L7F0633D8
/* 097E68 7F063338 864A000C */   lh    $t2, 0xc($s2)
.L7F06333C:
/* 097E6C 7F06333C 8FA20198 */  lw    $v0, 0x198($sp)
/* 097E70 7F063340 3C0A8000 */  lui   $t2, (0x80000040 >> 16) # lui $t2, 0x8000
/* 097E74 7F063344 354A0040 */  ori   $t2, (0x80000040 & 0xFFFF) # ori $t2, $t2, 0x40
/* 097E78 7F063348 24430008 */  addiu $v1, $v0, 8
/* 097E7C 7F06334C 3C0B0386 */  lui   $t3, (0x03860010 >> 16) # lui $t3, 0x386
/* 097E80 7F063350 3C0C8003 */  lui   $t4, %hi(g_WeaponEnvmapLight + 0x8)
/* 097E84 7F063354 AC4A0004 */  sw    $t2, 4($v0)
/* 097E88 7F063358 AC490000 */  sw    $t1, ($v0)
/* 097E8C 7F06335C 258C2448 */  addiu $t4, %lo(g_WeaponEnvmapLight + 0x8) # addiu $t4, $t4, 0x2448
/* 097E90 7F063360 356B0010 */  ori   $t3, (0x03860010 & 0xFFFF) # ori $t3, $t3, 0x10
/* 097E94 7F063364 24640008 */  addiu $a0, $v1, 8
/* 097E98 7F063368 AC6B0000 */  sw    $t3, ($v1)
/* 097E9C 7F06336C AC6C0004 */  sw    $t4, 4($v1)
/* 097EA0 7F063370 3C0D0388 */  lui   $t5, (0x03880010 >> 16) # lui $t5, 0x388
/* 097EA4 7F063374 3C198003 */  lui   $t9, %hi(g_WeaponEnvmapLight)
/* 097EA8 7F063378 27392440 */  addiu $t9, %lo(g_WeaponEnvmapLight) # addiu $t9, $t9, 0x2440
/* 097EAC 7F06337C 35AD0010 */  ori   $t5, (0x03880010 & 0xFFFF) # ori $t5, $t5, 0x10
/* 097EB0 7F063380 24900008 */  addiu $s0, $a0, 8
/* 097EB4 7F063384 3C180384 */  lui   $t8, (0x03840010 >> 16) # lui $t8, 0x384
/* 097EB8 7F063388 AC8D0000 */  sw    $t5, ($a0)
/* 097EBC 7F06338C AC990004 */  sw    $t9, 4($a0)
/* 097EC0 7F063390 37180010 */  ori   $t8, (0x03840010 & 0xFFFF) # ori $t8, $t8, 0x10
/* 097EC4 7F063394 AE180000 */  sw    $t8, ($s0)
/* 097EC8 7F063398 26050008 */  addiu $a1, $s0, 8
/* 097ECC 7F06339C 0FC1E11D */  jal   sub_GAME_7F078474
/* 097ED0 7F0633A0 AFA50198 */   sw    $a1, 0x198($sp)
/* 097ED4 7F0633A4 AE020004 */  sw    $v0, 4($s0)
/* 097ED8 7F0633A8 8FA80198 */  lw    $t0, 0x198($sp)
/* 097EDC 7F0633AC 3C0E0382 */  lui   $t6, (0x03820010 >> 16) # lui $t6, 0x382
/* 097EE0 7F0633B0 35CE0010 */  ori   $t6, (0x03820010 & 0xFFFF) # ori $t6, $t6, 0x10
/* 097EE4 7F0633B4 250F0008 */  addiu $t7, $t0, 8
/* 097EE8 7F0633B8 AFAF0198 */  sw    $t7, 0x198($sp)
/* 097EEC 7F0633BC AD0E0000 */  sw    $t6, ($t0)
/* 097EF0 7F0633C0 0FC1E11D */  jal   sub_GAME_7F078474
/* 097EF4 7F0633C4 AFA80034 */   sw    $t0, 0x34($sp)
/* 097EF8 7F0633C8 8FA30034 */  lw    $v1, 0x34($sp)
/* 097EFC 7F0633CC 24490010 */  addiu $t1, $v0, 0x10
/* 097F00 7F0633D0 AC690004 */  sw    $t1, 4($v1)
/* 097F04 7F0633D4 864A000C */  lh    $t2, 0xc($s2)
.L7F0633D8:
/* 097F08 7F0633D8 29410011 */  slti  $at, $t2, 0x11
/* 097F0C 7F0633DC 5420000C */  bnezl $at, .L7F063410
/* 097F10 7F0633E0 8FA201A4 */   lw    $v0, 0x1a4($sp)
/* 097F14 7F0633E4 8E420008 */  lw    $v0, 8($s2)
/* 097F18 7F0633E8 02602025 */  move  $a0, $s3
/* 097F1C 7F0633EC 8C4B0040 */  lw    $t3, 0x40($v0)
/* 097F20 7F0633F0 51600007 */  beql  $t3, $zero, .L7F063410
/* 097F24 7F0633F4 8FA201A4 */   lw    $v0, 0x1a4($sp)
/* 097F28 7F0633F8 0FC1B1E7 */  jal   modelGetNodeRwData
/* 097F2C 7F0633FC 8C450044 */   lw    $a1, 0x44($v0)
/* 097F30 7F063400 50400003 */  beql  $v0, $zero, .L7F063410
/* 097F34 7F063404 8FA201A4 */   lw    $v0, 0x1a4($sp)
/* 097F38 7F063408 AC400000 */  sw    $zero, ($v0)
/* 097F3C 7F06340C 8FA201A4 */  lw    $v0, 0x1a4($sp)
.L7F063410:
/* 097F40 7F063410 8FAC0198 */  lw    $t4, 0x198($sp)
/* 097F44 7F063414 27A40158 */  addiu $a0, $sp, 0x158
/* 097F48 7F063418 284100FF */  slti  $at, $v0, 0xff
/* 097F4C 7F06341C 14200006 */  bnez  $at, .L7F063438
/* 097F50 7F063420 AFAC0164 */   sw    $t4, 0x164($sp)
/* 097F54 7F063424 8FB901A8 */  lw    $t9, 0x1a8($sp)
/* 097F58 7F063428 240D0004 */  li    $t5, 4
/* 097F5C 7F06342C AFAD0188 */  sw    $t5, 0x188($sp)
/* 097F60 7F063430 10000006 */  b     .L7F06344C
/* 097F64 7F063434 AFB9018C */   sw    $t9, 0x18c($sp)
.L7F063438:
/* 097F68 7F063438 8FA801A8 */  lw    $t0, 0x1a8($sp)
/* 097F6C 7F06343C 24180005 */  li    $t8, 5
/* 097F70 7F063440 AFB80188 */  sw    $t8, 0x188($sp)
/* 097F74 7F063444 AFA2018C */  sw    $v0, 0x18c($sp)
/* 097F78 7F063448 AFA80190 */  sw    $t0, 0x190($sp)
.L7F06344C:
/* 097F7C 7F06344C AFA0015C */  sw    $zero, 0x15c($sp)
/* 097F80 7F063450 0FC1D1A1 */  jal   subdraw
/* 097F84 7F063454 02602825 */   move  $a1, $s3
/* 097F88 7F063458 8FAF0164 */  lw    $t7, 0x164($sp)
/* 097F8C 7F06345C 0FC16319 */  jal   matrix_4x4_7F058C64
/* 097F90 7F063460 AFAF0198 */   sw    $t7, 0x198($sp)
/* 097F94 7F063464 864E000E */  lh    $t6, 0xe($s2)
/* 097F98 7F063468 00008025 */  move  $s0, $zero
/* 097F9C 7F06346C 19C00010 */  blez  $t6, .L7F0634B0
/* 097FA0 7F063470 00000000 */   nop
/* 097FA4 7F063474 8FA90144 */  lw    $t1, 0x144($sp)
.L7F063478:
/* 097FA8 7F063478 27A50074 */  addiu $a1, $sp, 0x74
/* 097FAC 7F06347C 0FC16008 */  jal   matrix_4x4_copy
/* 097FB0 7F063480 01302021 */   addu  $a0, $t1, $s0
/* 097FB4 7F063484 8FAB0144 */  lw    $t3, 0x144($sp)
/* 097FB8 7F063488 00115180 */  sll   $t2, $s1, 6
/* 097FBC 7F06348C 27A40074 */  addiu $a0, $sp, 0x74
/* 097FC0 7F063490 0FC16327 */  jal   matrix_4x4_f32_to_s32
/* 097FC4 7F063494 014B2821 */   addu  $a1, $t2, $t3
/* 097FC8 7F063498 864C000E */  lh    $t4, 0xe($s2)
/* 097FCC 7F06349C 26310001 */  addiu $s1, $s1, 1
/* 097FD0 7F0634A0 26100040 */  addiu $s0, $s0, 0x40
/* 097FD4 7F0634A4 022C082A */  slt   $at, $s1, $t4
/* 097FD8 7F0634A8 5420FFF3 */  bnezl $at, .L7F063478
/* 097FDC 7F0634AC 8FA90144 */   lw    $t1, 0x144($sp)
.L7F0634B0:
/* 097FE0 7F0634B0 0FC16322 */  jal   matrix_4x4_7F058C88
/* 097FE4 7F0634B4 00000000 */   nop
.L7F0634B8:
/* 097FE8 7F0634B8 8FA20198 */  lw    $v0, 0x198($sp)
.L7F0634BC:
/* 097FEC 7F0634BC 8FBF0024 */  lw    $ra, 0x24($sp)
/* 097FF0 7F0634C0 8FB00014 */  lw    $s0, 0x14($sp)
/* 097FF4 7F0634C4 8FB10018 */  lw    $s1, 0x18($sp)
/* 097FF8 7F0634C8 8FB2001C */  lw    $s2, 0x1c($sp)
/* 097FFC 7F0634CC 8FB30020 */  lw    $s3, 0x20($sp)
/* 098000 7F0634D0 03E00008 */  jr    $ra
/* 098004 7F0634D4 27BD0198 */   addiu $sp, $sp, 0x198
)
#endif

void sub_GAME_7F0634D8(s32 arg0, s32 arg1, s32 arg2, s32 arg3)
{
    set_enviro_fog_for_items_in_solo_watch_menu(arg0, arg1, arg2, arg3, -256);
}

// Unreferenced
void sub_GAME_7F0634FC(s32 arg0, s32 arg1, s32 arg2) {
    sub_GAME_7F0634D8(arg0, arg1, arg2, 0xFF);
}

void sub_GAME_7F06351C(struct coord3d* arg0, Mtxf* arg1, Mtxf* arg2, Mtxf* arg3, struct coord3d* arg4, Mtxf* arg5, Mtxf* arg6)
{
    Mtxf sp20;

    matrix_4x4_set_identity_and_position(arg0, arg6);
    matrix_4x4_multiply_in_place(arg1, arg6);
    matrix_4x4_multiply_in_place(arg2, arg6);
    matrix_4x4_multiply_in_place(arg3, arg6);
    matrix_4x4_set_identity_and_position(arg4, &sp20);
    matrix_4x4_multiply_in_place(&sp20, arg6);
    matrix_4x4_multiply_in_place(arg5, arg6);
}



#ifdef NONMATCHING
void sub_GAME_7F06359C(void) {

}
#else
GLOBAL_ASM(
.late_rodata
glabel D_80053ED4
.word 0x40c90fdb /*6.2831855*/
glabel D_80053ED8
.word 0x3f19999a /*0.60000002*/
glabel D_80053EDC
.word 0x40c90fdb /*6.2831855*/
glabel D_80053EE0
.word 0x3f19999a /*0.60000002*/
glabel D_80053EE4
.word 0xbf860a92 /*-1.0471976*/
glabel D_80053EE8
.word 0x40c90fdb /*6.2831855*/
glabel D_80053EEC
.word 0x3f19999a /*0.60000002*/
glabel D_80053EF0
.word 0x40c90fdb /*6.2831855*/
glabel D_80053EF4
.word 0x3f19999a /*0.60000002*/
glabel D_80053EF8
.word 0xbe32b8c3 /*-0.17453294*/
glabel D_80053EFC
.word 0x3f860a92 /*1.0471976*/
glabel D_80053F00
.word 0x3e32b8c3 /*0.17453294*/
glabel D_80053F04
.word 0x3f860a92 /*1.0471976*/
glabel D_80053F08
.word 0xbe32b8c3 /*-0.17453294*/
glabel D_80053F0C
.word 0x3e32b8c3 /*0.17453294*/
glabel D_80053F10
.word 0x3e32b8c3 /*0.17453294*/
glabel D_80053F14
.word 0xbe32b8c3 /*-0.17453294*/
glabel D_80053F18
.word 0xbf65c8fa /*-0.89759791*/
glabel D_80053F1C
.word 0xbe32b8c3 /*-0.17453294*/
glabel D_80053F20
.word 0x40490fdb /*3.1415927*/
.text
glabel sub_GAME_7F06359C
/* 0980CC 7F06359C 27BDFAC8 */  addiu $sp, $sp, -0x538
/* 0980D0 7F0635A0 3C0F8003 */  lui   $t7, %hi(D_80035D04+0x3C)
/* 0980D4 7F0635A4 AFB30054 */  sw    $s3, 0x54($sp)
/* 0980D8 7F0635A8 AFB20050 */  sw    $s2, 0x50($sp)
/* 0980DC 7F0635AC 25EF5D40 */  addiu $t7, %lo(D_80035D04+0x3C) # addiu $t7, $t7, 0x5d40
/* 0980E0 7F0635B0 00C09025 */  move  $s2, $a2
/* 0980E4 7F0635B4 00E09825 */  move  $s3, $a3
/* 0980E8 7F0635B8 AFBF006C */  sw    $ra, 0x6c($sp)
/* 0980EC 7F0635BC AFBE0068 */  sw    $fp, 0x68($sp)
/* 0980F0 7F0635C0 AFB70064 */  sw    $s7, 0x64($sp)
/* 0980F4 7F0635C4 AFB60060 */  sw    $s6, 0x60($sp)
/* 0980F8 7F0635C8 AFB5005C */  sw    $s5, 0x5c($sp)
/* 0980FC 7F0635CC AFB40058 */  sw    $s4, 0x58($sp)
/* 098100 7F0635D0 AFB1004C */  sw    $s1, 0x4c($sp)
/* 098104 7F0635D4 AFB00048 */  sw    $s0, 0x48($sp)
/* 098108 7F0635D8 F7B80040 */  sdc1  $f24, 0x40($sp)
/* 09810C 7F0635DC F7B60038 */  sdc1  $f22, 0x38($sp)
/* 098110 7F0635E0 F7B40030 */  sdc1  $f20, 0x30($sp)
/* 098114 7F0635E4 AFA40538 */  sw    $a0, 0x538($sp)
/* 098118 7F0635E8 AFA5053C */  sw    $a1, 0x53c($sp)
/* 09811C 7F0635EC 25E8003C */  addiu $t0, $t7, 0x3c
/* 098120 7F0635F0 27AE04F8 */  addiu $t6, $sp, 0x4f8
.L7F0635F4:
/* 098124 7F0635F4 8DE10000 */  lw    $at, ($t7)
/* 098128 7F0635F8 25EF000C */  addiu $t7, $t7, 0xc
/* 09812C 7F0635FC 25CE000C */  addiu $t6, $t6, 0xc
/* 098130 7F063600 ADC1FFF4 */  sw    $at, -0xc($t6)
/* 098134 7F063604 8DE1FFF8 */  lw    $at, -8($t7)
/* 098138 7F063608 ADC1FFF8 */  sw    $at, -8($t6)
/* 09813C 7F06360C 8DE1FFFC */  lw    $at, -4($t7)
/* 098140 7F063610 15E8FFF8 */  bne   $t7, $t0, .L7F0635F4
/* 098144 7F063614 ADC1FFFC */   sw    $at, -4($t6)
/* 098148 7F063618 8DE10000 */  lw    $at, ($t7)
/* 09814C 7F06361C 00002025 */  move  $a0, $zero
/* 098150 7F063620 24050055 */  li    $a1, 85
/* 098154 7F063624 0FC176A3 */  jal   sub_GAME_7F05DA8C
/* 098158 7F063628 ADC10000 */   sw    $at, ($t6)
/* 09815C 7F06362C 0FC173AF */  jal   Gun_hand_without_item
/* 098160 7F063630 00002025 */   move  $a0, $zero
/* 098164 7F063634 10400005 */  beqz  $v0, .L7F06364C
/* 098168 7F063638 00000000 */   nop
/* 09816C 7F06363C 0FC173C0 */  jal   get_itemtype_in_hand
/* 098170 7F063640 00002025 */   move  $a0, $zero
/* 098174 7F063644 14400003 */  bnez  $v0, .L7F063654
/* 098178 7F063648 00000000 */   nop
.L7F06364C:
/* 09817C 7F06364C 10000336 */  b     .L7F064328
/* 098180 7F063650 8FA20538 */   lw    $v0, 0x538($sp)
.L7F063654:
/* 098184 7F063654 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 098188 7F063658 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09818C 7F06365C 8444081E */  lh    $a0, 0x81e($v0)
/* 098190 7F063660 24420810 */  addiu $v0, $v0, 0x810
/* 098194 7F063664 AFA203D8 */  sw    $v0, 0x3d8($sp)
/* 098198 7F063668 00044980 */  sll   $t1, $a0, 6
/* 09819C 7F06366C 0FC2F5C5 */  jal   dynAllocate
/* 0981A0 7F063670 01202025 */   move  $a0, $t1
/* 0981A4 7F063674 0040B825 */  move  $s7, $v0
/* 0981A8 7F063678 0FC1D73D */  jal   modelCalculateRwDataLen
/* 0981AC 7F06367C 8FA403D8 */   lw    $a0, 0x3d8($sp)
/* 0981B0 7F063680 8FA503D8 */  lw    $a1, 0x3d8($sp)
/* 0981B4 7F063684 27A404D8 */  addiu $a0, $sp, 0x4d8
/* 0981B8 7F063688 0FC1D7DA */  jal   modelInit
/* 0981BC 7F06368C 27A60460 */   addiu $a2, $sp, 0x460
/* 0981C0 7F063690 AFB704E4 */  sw    $s7, 0x4e4($sp)
/* 0981C4 7F063694 8FA4053C */  lw    $a0, 0x53c($sp)
/* 0981C8 7F063698 0FC16008 */  jal   matrix_4x4_copy
/* 0981CC 7F06369C 02E02825 */   move  $a1, $s7
/* 0981D0 7F0636A0 240A0004 */  li    $t2, 4
/* 0981D4 7F0636A4 AFAA0070 */  sw    $t2, 0x70($sp)
/* 0981D8 7F0636A8 24110001 */  li    $s1, 1
/* 0981DC 7F0636AC 8FB4054C */  lw    $s4, 0x54c($sp)
.L7F0636B0:
/* 0981E0 7F0636B0 8FAB03D8 */  lw    $t3, 0x3d8($sp)
/* 0981E4 7F0636B4 8FAD0070 */  lw    $t5, 0x70($sp)
/* 0981E8 7F0636B8 24010002 */  li    $at, 2
/* 0981EC 7F0636BC 8D6C0008 */  lw    $t4, 8($t3)
/* 0981F0 7F0636C0 27A5041C */  addiu $a1, $sp, 0x41c
/* 0981F4 7F0636C4 018DC821 */  addu  $t9, $t4, $t5
/* 0981F8 7F0636C8 8F380000 */  lw    $t8, ($t9)
/* 0981FC 7F0636CC 1621002B */  bne   $s1, $at, .L7F06377C
/* 098200 7F0636D0 8F100004 */   lw    $s0, 4($t8)
/* 098204 7F0636D4 0C00303B */  jal   joyGetStickX
/* 098208 7F0636D8 82840000 */   lb    $a0, ($s4)
/* 09820C 7F0636DC 44822000 */  mtc1  $v0, $f4
/* 098210 7F0636E0 3C018005 */  lui   $at, %hi(D_80053ED4)
/* 098214 7F0636E4 C42A3ED4 */  lwc1  $f10, %lo(D_80053ED4)($at)
/* 098218 7F0636E8 468021A0 */  cvt.s.w $f6, $f4
/* 09821C 7F0636EC 3C018005 */  lui   $at, %hi(D_80053ED8)
/* 098220 7F0636F0 C4323ED8 */  lwc1  $f18, %lo(D_80053ED8)($at)
/* 098224 7F0636F4 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 098228 7F0636F8 27A5041C */  addiu $a1, $sp, 0x41c
/* 09822C 7F0636FC 46003207 */  neg.s $f8, $f6
/* 098230 7F063700 44813000 */  mtc1  $at, $f6
/* 098234 7F063704 460A4402 */  mul.s $f16, $f8, $f10
/* 098238 7F063708 00000000 */  nop
/* 09823C 7F06370C 46128102 */  mul.s $f4, $f16, $f18
/* 098240 7F063710 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 098244 7F063714 46062303 */   div.s $f12, $f4, $f6
/* 098248 7F063718 0C00307F */  jal   joyGetStickY
/* 09824C 7F06371C 82840000 */   lb    $a0, ($s4)
/* 098250 7F063720 44824000 */  mtc1  $v0, $f8
/* 098254 7F063724 3C018005 */  lui   $at, %hi(D_80053EDC)
/* 098258 7F063728 C4323EDC */  lwc1  $f18, %lo(D_80053EDC)($at)
/* 09825C 7F06372C 468042A0 */  cvt.s.w $f10, $f8
/* 098260 7F063730 3C018005 */  lui   $at, %hi(D_80053EE0)
/* 098264 7F063734 C4263EE0 */  lwc1  $f6, %lo(D_80053EE0)($at)
/* 098268 7F063738 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 09826C 7F06373C 27A503DC */  addiu $a1, $sp, 0x3dc
/* 098270 7F063740 46005407 */  neg.s $f16, $f10
/* 098274 7F063744 44815000 */  mtc1  $at, $f10
/* 098278 7F063748 46128102 */  mul.s $f4, $f16, $f18
/* 09827C 7F06374C 00000000 */  nop
/* 098280 7F063750 46062202 */  mul.s $f8, $f4, $f6
/* 098284 7F063754 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098288 7F063758 460A4303 */   div.s $f12, $f8, $f10
/* 09828C 7F06375C 27A403DC */  addiu $a0, $sp, 0x3dc
/* 098290 7F063760 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 098294 7F063764 27A5041C */   addiu $a1, $sp, 0x41c
/* 098298 7F063768 02002025 */  move  $a0, $s0
/* 09829C 7F06376C 0FC16266 */  jal   matrix_4x4_set_position
/* 0982A0 7F063770 27A5041C */   addiu $a1, $sp, 0x41c
/* 0982A4 7F063774 10000004 */  b     .L7F063788
/* 0982A8 7F063778 00114180 */   sll   $t0, $s1, 6
.L7F06377C:
/* 0982AC 7F06377C 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 0982B0 7F063780 02002025 */   move  $a0, $s0
/* 0982B4 7F063784 00114180 */  sll   $t0, $s1, 6
.L7F063788:
/* 0982B8 7F063788 01173021 */  addu  $a2, $t0, $s7
/* 0982BC 7F06378C 8FA4053C */  lw    $a0, 0x53c($sp)
/* 0982C0 7F063790 0FC16032 */  jal   matrix_4x4_multiply
/* 0982C4 7F063794 27A5041C */   addiu $a1, $sp, 0x41c
/* 0982C8 7F063798 8FAF0070 */  lw    $t7, 0x70($sp)
/* 0982CC 7F06379C 26310001 */  addiu $s1, $s1, 1
/* 0982D0 7F0637A0 2A21000D */  slti  $at, $s1, 0xd
/* 0982D4 7F0637A4 25EE0004 */  addiu $t6, $t7, 4
/* 0982D8 7F0637A8 1420FFC1 */  bnez  $at, .L7F0636B0
/* 0982DC 7F0637AC AFAE0070 */   sw    $t6, 0x70($sp)
/* 0982E0 7F0637B0 0FC1BBF1 */  jal   modelUpdateNodeRelations
/* 0982E4 7F0637B4 27A404D8 */   addiu $a0, $sp, 0x4d8
/* 0982E8 7F0637B8 8FA90538 */  lw    $t1, 0x538($sp)
/* 0982EC 7F0637BC 2A4100FF */  slti  $at, $s2, 0xff
/* 0982F0 7F0637C0 14200004 */  bnez  $at, .L7F0637D4
/* 0982F4 7F0637C4 AFA90504 */   sw    $t1, 0x504($sp)
/* 0982F8 7F0637C8 240A0001 */  li    $t2, 1
/* 0982FC 7F0637CC 10000006 */  b     .L7F0637E8
/* 098300 7F0637D0 AFAA0528 */   sw    $t2, 0x528($sp)
.L7F0637D4:
/* 098304 7F0637D4 240B0005 */  li    $t3, 5
/* 098308 7F0637D8 240CFF00 */  li    $t4, -256
/* 09830C 7F0637DC AFAB0528 */  sw    $t3, 0x528($sp)
/* 098310 7F0637E0 AFB2052C */  sw    $s2, 0x52c($sp)
/* 098314 7F0637E4 AFAC0530 */  sw    $t4, 0x530($sp)
.L7F0637E8:
/* 098318 7F0637E8 240D0001 */  li    $t5, 1
/* 09831C 7F0637EC AFAD04FC */  sw    $t5, 0x4fc($sp)
/* 098320 7F0637F0 27A404F8 */  addiu $a0, $sp, 0x4f8
/* 098324 7F0637F4 0FC1D1A1 */  jal   subdraw
/* 098328 7F0637F8 27A504D8 */   addiu $a1, $sp, 0x4d8
/* 09832C 7F0637FC 8FB90504 */  lw    $t9, 0x504($sp)
/* 098330 7F063800 0FC16319 */  jal   matrix_4x4_7F058C64
/* 098334 7F063804 AFB90538 */   sw    $t9, 0x538($sp)
/* 098338 7F063808 8FB803D8 */  lw    $t8, 0x3d8($sp)
/* 09833C 7F06380C 00008825 */  move  $s1, $zero
/* 098340 7F063810 00008025 */  move  $s0, $zero
/* 098344 7F063814 8708000E */  lh    $t0, 0xe($t8)
/* 098348 7F063818 19000011 */  blez  $t0, .L7F063860
/* 09834C 7F06381C 00000000 */   nop
/* 098350 7F063820 8FAF04E4 */  lw    $t7, 0x4e4($sp)
.L7F063824:
/* 098354 7F063824 27A5041C */  addiu $a1, $sp, 0x41c
/* 098358 7F063828 0FC16008 */  jal   matrix_4x4_copy
/* 09835C 7F06382C 01F02021 */   addu  $a0, $t7, $s0
/* 098360 7F063830 8FA904E4 */  lw    $t1, 0x4e4($sp)
/* 098364 7F063834 00117180 */  sll   $t6, $s1, 6
/* 098368 7F063838 27A4041C */  addiu $a0, $sp, 0x41c
/* 09836C 7F06383C 0FC16327 */  jal   matrix_4x4_f32_to_s32
/* 098370 7F063840 01C92821 */   addu  $a1, $t6, $t1
/* 098374 7F063844 8FAA03D8 */  lw    $t2, 0x3d8($sp)
/* 098378 7F063848 26310001 */  addiu $s1, $s1, 1
/* 09837C 7F06384C 26100040 */  addiu $s0, $s0, 0x40
/* 098380 7F063850 854B000E */  lh    $t3, 0xe($t2)
/* 098384 7F063854 022B082A */  slt   $at, $s1, $t3
/* 098388 7F063858 5420FFF2 */  bnezl $at, .L7F063824
/* 09838C 7F06385C 8FAF04E4 */   lw    $t7, 0x4e4($sp)
.L7F063860:
/* 098390 7F063860 0FC16322 */  jal   matrix_4x4_7F058C88
/* 098394 7F063864 00000000 */   nop
/* 098398 7F063868 126002AE */  beqz  $s3, .L7F064324
/* 09839C 7F06386C 8FAC03D8 */   lw    $t4, 0x3d8($sp)
/* 0983A0 7F063870 8D8D0008 */  lw    $t5, 8($t4)
/* 0983A4 7F063874 00008825 */  move  $s1, $zero
/* 0983A8 7F063878 8DA50034 */  lw    $a1, 0x34($t5)
/* 0983AC 7F06387C 50A00005 */  beql  $a1, $zero, .L7F063894
/* 0983B0 7F063880 8FB903D8 */   lw    $t9, 0x3d8($sp)
/* 0983B4 7F063884 0FC1B1E7 */  jal   modelGetNodeRwData
/* 0983B8 7F063888 27A404D8 */   addiu $a0, $sp, 0x4d8
/* 0983BC 7F06388C AC400000 */  sw    $zero, ($v0)
/* 0983C0 7F063890 8FB903D8 */  lw    $t9, 0x3d8($sp)
.L7F063894:
/* 0983C4 7F063894 8724000E */  lh    $a0, 0xe($t9)
/* 0983C8 7F063898 0004C180 */  sll   $t8, $a0, 6
/* 0983CC 7F06389C 0FC2F5C5 */  jal   dynAllocate
/* 0983D0 7F0638A0 03002025 */   move  $a0, $t8
/* 0983D4 7F0638A4 3C018005 */  lui   $at, %hi(D_80053EE4)
/* 0983D8 7F0638A8 C4363EE4 */  lwc1  $f22, %lo(D_80053EE4)($at)
/* 0983DC 7F0638AC 3C01C120 */  li    $at, 0xC1200000 # -10.000000
/* 0983E0 7F0638B0 4481A000 */  mtc1  $at, $f20
/* 0983E4 7F0638B4 4480C000 */  mtc1  $zero, $f24
/* 0983E8 7F0638B8 0040B825 */  move  $s7, $v0
/* 0983EC 7F0638BC AFA204E4 */  sw    $v0, 0x4e4($sp)
/* 0983F0 7F0638C0 8FBE0548 */  lw    $fp, 0x548($sp)
/* 0983F4 7F0638C4 27B601CC */  addiu $s6, $sp, 0x1cc
/* 0983F8 7F0638C8 27B5020C */  addiu $s5, $sp, 0x20c
/* 0983FC 7F0638CC 27B3024C */  addiu $s3, $sp, 0x24c
/* 098400 7F0638D0 27B2028C */  addiu $s2, $sp, 0x28c
/* 098404 7F0638D4 27B0038C */  addiu $s0, $sp, 0x38c
/* 098408 7F0638D8 3C01C0A0 */  li    $at, 0xC0A00000 # -5.000000
.L7F0638DC:
/* 09840C 7F0638DC 44810000 */  mtc1  $at, $f0
/* 098410 7F0638E0 3C01C328 */  li    $at, 0xC3280000 # -168.000000
/* 098414 7F0638E4 44811000 */  mtc1  $at, $f2
/* 098418 7F0638E8 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
/* 09841C 7F0638EC 44818000 */  mtc1  $at, $f16
/* 098420 7F0638F0 44050000 */  mfc1  $a1, $f0
/* 098424 7F0638F4 44071000 */  mfc1  $a3, $f2
/* 098428 7F0638F8 02A02025 */  move  $a0, $s5
/* 09842C 7F0638FC 3C0644FA */  lui   $a2, 0x44fa
/* 098430 7F063900 E7B80014 */  swc1  $f24, 0x14($sp)
/* 098434 7F063904 E7B8001C */  swc1  $f24, 0x1c($sp)
/* 098438 7F063908 E7B80020 */  swc1  $f24, 0x20($sp)
/* 09843C 7F06390C E7A00010 */  swc1  $f0, 0x10($sp)
/* 098440 7F063910 E7A20018 */  swc1  $f2, 0x18($sp)
/* 098444 7F063914 0FC165A5 */  jal   matrix_4x4_set_lookat_target
/* 098448 7F063918 E7B00024 */   swc1  $f16, 0x24($sp)
/* 09844C 7F06391C 3C01C0A0 */  li    $at, 0xC0A00000 # -5.000000
/* 098450 7F063920 44810000 */  mtc1  $at, $f0
/* 098454 7F063924 3C01C328 */  li    $at, 0xC3280000 # -168.000000
/* 098458 7F063928 44811000 */  mtc1  $at, $f2
/* 09845C 7F06392C 3C01BF80 */  li    $at, 0xBF800000 # -1.000000
/* 098460 7F063930 44819000 */  mtc1  $at, $f18
/* 098464 7F063934 44050000 */  mfc1  $a1, $f0
/* 098468 7F063938 44071000 */  mfc1  $a3, $f2
/* 09846C 7F06393C 02002025 */  move  $a0, $s0
/* 098470 7F063940 3C0644FA */  lui   $a2, 0x44fa
/* 098474 7F063944 E7B80014 */  swc1  $f24, 0x14($sp)
/* 098478 7F063948 E7B8001C */  swc1  $f24, 0x1c($sp)
/* 09847C 7F06394C E7B80020 */  swc1  $f24, 0x20($sp)
/* 098480 7F063950 E7A00010 */  swc1  $f0, 0x10($sp)
/* 098484 7F063954 E7A20018 */  swc1  $f2, 0x18($sp)
/* 098488 7F063958 0FC165A5 */  jal   matrix_4x4_set_lookat_target
/* 09848C 7F06395C E7B20024 */   swc1  $f18, 0x24($sp)
/* 098490 7F063960 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 098494 7F063964 02602025 */   move  $a0, $s3
/* 098498 7F063968 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 09849C 7F06396C 02C02025 */   move  $a0, $s6
/* 0984A0 7F063970 02C02025 */  move  $a0, $s6
/* 0984A4 7F063974 0FC16008 */  jal   matrix_4x4_copy
/* 0984A8 7F063978 27A502CC */   addiu $a1, $sp, 0x2cc
/* 0984AC 7F06397C 24010002 */  li    $at, 2
/* 0984B0 7F063980 5621003D */  bnel  $s1, $at, .L7F063A78
/* 0984B4 7F063984 2401000B */   li    $at, 11
/* 0984B8 7F063988 8FC10058 */  lw    $at, 0x58($fp)
/* 0984BC 7F06398C 27A401C0 */  addiu $a0, $sp, 0x1c0
/* 0984C0 7F063990 27A5034C */  addiu $a1, $sp, 0x34c
/* 0984C4 7F063994 AC810000 */  sw    $at, ($a0)
/* 0984C8 7F063998 8FCF005C */  lw    $t7, 0x5c($fp)
/* 0984CC 7F06399C AC8F0004 */  sw    $t7, 4($a0)
/* 0984D0 7F0639A0 8FC10060 */  lw    $at, 0x60($fp)
/* 0984D4 7F0639A4 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 0984D8 7F0639A8 AC810008 */   sw    $at, 8($a0)
/* 0984DC 7F0639AC 0C00303B */  jal   joyGetStickX
/* 0984E0 7F0639B0 82840000 */   lb    $a0, ($s4)
/* 0984E4 7F0639B4 44822000 */  mtc1  $v0, $f4
/* 0984E8 7F0639B8 3C018005 */  lui   $at, %hi(D_80053EE8)
/* 0984EC 7F0639BC C42A3EE8 */  lwc1  $f10, %lo(D_80053EE8)($at)
/* 0984F0 7F0639C0 468021A0 */  cvt.s.w $f6, $f4
/* 0984F4 7F0639C4 3C018005 */  lui   $at, %hi(D_80053EEC)
/* 0984F8 7F0639C8 C4323EEC */  lwc1  $f18, %lo(D_80053EEC)($at)
/* 0984FC 7F0639CC 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 098500 7F0639D0 27A5041C */  addiu $a1, $sp, 0x41c
/* 098504 7F0639D4 46003207 */  neg.s $f8, $f6
/* 098508 7F0639D8 44813000 */  mtc1  $at, $f6
/* 09850C 7F0639DC 460A4402 */  mul.s $f16, $f8, $f10
/* 098510 7F0639E0 00000000 */  nop
/* 098514 7F0639E4 46128102 */  mul.s $f4, $f16, $f18
/* 098518 7F0639E8 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 09851C 7F0639EC 46062303 */   div.s $f12, $f4, $f6
/* 098520 7F0639F0 0C00307F */  jal   joyGetStickY
/* 098524 7F0639F4 82840000 */   lb    $a0, ($s4)
/* 098528 7F0639F8 44824000 */  mtc1  $v0, $f8
/* 09852C 7F0639FC 3C018005 */  lui   $at, %hi(D_80053EF0)
/* 098530 7F063A00 C4323EF0 */  lwc1  $f18, %lo(D_80053EF0)($at)
/* 098534 7F063A04 468042A0 */  cvt.s.w $f10, $f8
/* 098538 7F063A08 3C018005 */  lui   $at, %hi(D_80053EF4)
/* 09853C 7F063A0C C4263EF4 */  lwc1  $f6, %lo(D_80053EF4)($at)
/* 098540 7F063A10 3C0143B4 */  li    $at, 0x43B40000 # 360.000000
/* 098544 7F063A14 27A503DC */  addiu $a1, $sp, 0x3dc
/* 098548 7F063A18 46005407 */  neg.s $f16, $f10
/* 09854C 7F063A1C 44815000 */  mtc1  $at, $f10
/* 098550 7F063A20 46128102 */  mul.s $f4, $f16, $f18
/* 098554 7F063A24 00000000 */  nop
/* 098558 7F063A28 46062202 */  mul.s $f8, $f4, $f6
/* 09855C 7F063A2C 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098560 7F063A30 460A4303 */   div.s $f12, $f8, $f10
/* 098564 7F063A34 27A403DC */  addiu $a0, $sp, 0x3dc
/* 098568 7F063A38 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 09856C 7F063A3C 27A5041C */   addiu $a1, $sp, 0x41c
/* 098570 7F063A40 27A4034C */  addiu $a0, $sp, 0x34c
/* 098574 7F063A44 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 098578 7F063A48 27A5041C */   addiu $a1, $sp, 0x41c
/* 09857C 7F063A4C 02A02025 */  move  $a0, $s5
/* 098580 7F063A50 27A5041C */  addiu $a1, $sp, 0x41c
/* 098584 7F063A54 0FC16032 */  jal   matrix_4x4_multiply
/* 098588 7F063A58 27A6030C */   addiu $a2, $sp, 0x30c
/* 09858C 7F063A5C 00117180 */  sll   $t6, $s1, 6
/* 098590 7F063A60 01D72821 */  addu  $a1, $t6, $s7
/* 098594 7F063A64 0FC16008 */  jal   matrix_4x4_copy
/* 098598 7F063A68 27A4030C */   addiu $a0, $sp, 0x30c
/* 09859C 7F063A6C 10000208 */  b     .L7F064290
/* 0985A0 7F063A70 26310001 */   addiu $s1, $s1, 1
/* 0985A4 7F063A74 2401000B */  li    $at, 11
.L7F063A78:
/* 0985A8 7F063A78 1621002B */  bne   $s1, $at, .L7F063B28
/* 0985AC 7F063A7C 3C0A8003 */   lui   $t2, %hi(D_80035D44+0x3C)
/* 0985B0 7F063A80 254A5D80 */  addiu $t2, %lo(D_80035D44+0x3C) # addiu $t2, $t2, 0x5d80
/* 0985B4 7F063A84 8D410000 */  lw    $at, ($t2)
/* 0985B8 7F063A88 27A901A8 */  addiu $t1, $sp, 0x1a8
/* 0985BC 7F063A8C 8D4C0004 */  lw    $t4, 4($t2)
/* 0985C0 7F063A90 AD210000 */  sw    $at, ($t1)
/* 0985C4 7F063A94 8D410008 */  lw    $at, 8($t2)
/* 0985C8 7F063A98 AD2C0004 */  sw    $t4, 4($t1)
/* 0985CC 7F063A9C 27AD01B4 */  addiu $t5, $sp, 0x1b4
/* 0985D0 7F063AA0 AD210008 */  sw    $at, 8($t1)
/* 0985D4 7F063AA4 8FC100C4 */  lw    $at, 0xc4($fp)
/* 0985D8 7F063AA8 24050010 */  li    $a1, 16
/* 0985DC 7F063AAC ADA10000 */  sw    $at, ($t5)
/* 0985E0 7F063AB0 8FD800C8 */  lw    $t8, 0xc8($fp)
/* 0985E4 7F063AB4 ADB80004 */  sw    $t8, 4($t5)
/* 0985E8 7F063AB8 8FC100CC */  lw    $at, 0xcc($fp)
/* 0985EC 7F063ABC ADA10008 */  sw    $at, 8($t5)
/* 0985F0 7F063AC0 0C0030C3 */  jal   joyGetButtons
/* 0985F4 7F063AC4 82840000 */   lb    $a0, ($s4)
/* 0985F8 7F063AC8 10400004 */  beqz  $v0, .L7F063ADC
/* 0985FC 7F063ACC 3C018005 */   lui   $at, %hi(D_80053EF8)
/* 098600 7F063AD0 C42C3EF8 */  lwc1  $f12, %lo(D_80053EF8)($at)
/* 098604 7F063AD4 0FC1617F */  jal   matrix_4x4_set_rotation_around_y
/* 098608 7F063AD8 02602825 */   move  $a1, $s3
.L7F063ADC:
/* 09860C 7F063ADC 3C018005 */  lui   $at, %hi(D_80053EFC)
/* 098610 7F063AE0 C42C3EFC */  lwc1  $f12, %lo(D_80053EFC)($at)
/* 098614 7F063AE4 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098618 7F063AE8 02402825 */   move  $a1, $s2
/* 09861C 7F063AEC 27A801B4 */  addiu $t0, $sp, 0x1b4
/* 098620 7F063AF0 AFA80010 */  sw    $t0, 0x10($sp)
/* 098624 7F063AF4 27A401A8 */  addiu $a0, $sp, 0x1a8
/* 098628 7F063AF8 02602825 */  move  $a1, $s3
/* 09862C 7F063AFC 02403025 */  move  $a2, $s2
/* 098630 7F063B00 02C03825 */  move  $a3, $s6
/* 098634 7F063B04 AFB50014 */  sw    $s5, 0x14($sp)
/* 098638 7F063B08 0FC18D47 */  jal   sub_GAME_7F06351C
/* 09863C 7F063B0C AFB00018 */   sw    $s0, 0x18($sp)
/* 098640 7F063B10 00117980 */  sll   $t7, $s1, 6
/* 098644 7F063B14 01F72821 */  addu  $a1, $t7, $s7
/* 098648 7F063B18 0FC16008 */  jal   matrix_4x4_copy
/* 09864C 7F063B1C 02002025 */   move  $a0, $s0
/* 098650 7F063B20 100001DB */  b     .L7F064290
/* 098654 7F063B24 26310001 */   addiu $s1, $s1, 1
.L7F063B28:
/* 098658 7F063B28 24010004 */  li    $at, 4
/* 09865C 7F063B2C 16210029 */  bne   $s1, $at, .L7F063BD4
/* 098660 7F063B30 3C0B8003 */   lui   $t3, %hi(D_80035D44+0x48)
/* 098664 7F063B34 256B5D8C */  addiu $t3, %lo(D_80035D44+0x48) # addiu $t3, $t3, 0x5d8c
/* 098668 7F063B38 8D610000 */  lw    $at, ($t3)
/* 09866C 7F063B3C 27AE0190 */  addiu $t6, $sp, 0x190
/* 098670 7F063B40 8D6A0004 */  lw    $t2, 4($t3)
/* 098674 7F063B44 ADC10000 */  sw    $at, ($t6)
/* 098678 7F063B48 8D610008 */  lw    $at, 8($t3)
/* 09867C 7F063B4C ADCA0004 */  sw    $t2, 4($t6)
/* 098680 7F063B50 27AC019C */  addiu $t4, $sp, 0x19c
/* 098684 7F063B54 ADC10008 */  sw    $at, 8($t6)
/* 098688 7F063B58 8FC10070 */  lw    $at, 0x70($fp)
/* 09868C 7F063B5C 24050008 */  li    $a1, 8
/* 098690 7F063B60 AD810000 */  sw    $at, ($t4)
/* 098694 7F063B64 8FCD0074 */  lw    $t5, 0x74($fp)
/* 098698 7F063B68 AD8D0004 */  sw    $t5, 4($t4)
/* 09869C 7F063B6C 8FC10078 */  lw    $at, 0x78($fp)
/* 0986A0 7F063B70 AD810008 */  sw    $at, 8($t4)
/* 0986A4 7F063B74 0C0030C3 */  jal   joyGetButtons
/* 0986A8 7F063B78 82840000 */   lb    $a0, ($s4)
/* 0986AC 7F063B7C 10400004 */  beqz  $v0, .L7F063B90
/* 0986B0 7F063B80 4600B306 */   mov.s $f12, $f22
/* 0986B4 7F063B84 C7B00194 */  lwc1  $f16, 0x194($sp)
/* 0986B8 7F063B88 46148480 */  add.s $f18, $f16, $f20
/* 0986BC 7F063B8C E7B20194 */  swc1  $f18, 0x194($sp)
.L7F063B90:
/* 0986C0 7F063B90 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 0986C4 7F063B94 02402825 */   move  $a1, $s2
/* 0986C8 7F063B98 27B8019C */  addiu $t8, $sp, 0x19c
/* 0986CC 7F063B9C AFB80010 */  sw    $t8, 0x10($sp)
/* 0986D0 7F063BA0 27A40190 */  addiu $a0, $sp, 0x190
/* 0986D4 7F063BA4 02602825 */  move  $a1, $s3
/* 0986D8 7F063BA8 02403025 */  move  $a2, $s2
/* 0986DC 7F063BAC 02C03825 */  move  $a3, $s6
/* 0986E0 7F063BB0 AFB50014 */  sw    $s5, 0x14($sp)
/* 0986E4 7F063BB4 0FC18D47 */  jal   sub_GAME_7F06351C
/* 0986E8 7F063BB8 AFB00018 */   sw    $s0, 0x18($sp)
/* 0986EC 7F063BBC 00114180 */  sll   $t0, $s1, 6
/* 0986F0 7F063BC0 01172821 */  addu  $a1, $t0, $s7
/* 0986F4 7F063BC4 0FC16008 */  jal   matrix_4x4_copy
/* 0986F8 7F063BC8 02002025 */   move  $a0, $s0
/* 0986FC 7F063BCC 100001B0 */  b     .L7F064290
/* 098700 7F063BD0 26310001 */   addiu $s1, $s1, 1
.L7F063BD4:
/* 098704 7F063BD4 24010005 */  li    $at, 5
/* 098708 7F063BD8 16210029 */  bne   $s1, $at, .L7F063C80
/* 09870C 7F063BDC 3C098003 */   lui   $t1, %hi(D_80035D44+0x54)
/* 098710 7F063BE0 25295D98 */  addiu $t1, %lo(D_80035D44+0x54) # addiu $t1, $t1, 0x5d98
/* 098714 7F063BE4 8D210000 */  lw    $at, ($t1)
/* 098718 7F063BE8 27AF0178 */  addiu $t7, $sp, 0x178
/* 09871C 7F063BEC 8D2B0004 */  lw    $t3, 4($t1)
/* 098720 7F063BF0 ADE10000 */  sw    $at, ($t7)
/* 098724 7F063BF4 8D210008 */  lw    $at, 8($t1)
/* 098728 7F063BF8 ADEB0004 */  sw    $t3, 4($t7)
/* 09872C 7F063BFC 27AA0184 */  addiu $t2, $sp, 0x184
/* 098730 7F063C00 ADE10008 */  sw    $at, 8($t7)
/* 098734 7F063C04 8FC1007C */  lw    $at, 0x7c($fp)
/* 098738 7F063C08 24050004 */  li    $a1, 4
/* 09873C 7F063C0C AD410000 */  sw    $at, ($t2)
/* 098740 7F063C10 8FCC0080 */  lw    $t4, 0x80($fp)
/* 098744 7F063C14 AD4C0004 */  sw    $t4, 4($t2)
/* 098748 7F063C18 8FC10084 */  lw    $at, 0x84($fp)
/* 09874C 7F063C1C AD410008 */  sw    $at, 8($t2)
/* 098750 7F063C20 0C0030C3 */  jal   joyGetButtons
/* 098754 7F063C24 82840000 */   lb    $a0, ($s4)
/* 098758 7F063C28 10400004 */  beqz  $v0, .L7F063C3C
/* 09875C 7F063C2C 4600B306 */   mov.s $f12, $f22
/* 098760 7F063C30 C7A4017C */  lwc1  $f4, 0x17c($sp)
/* 098764 7F063C34 46142180 */  add.s $f6, $f4, $f20
/* 098768 7F063C38 E7A6017C */  swc1  $f6, 0x17c($sp)
.L7F063C3C:
/* 09876C 7F063C3C 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098770 7F063C40 02402825 */   move  $a1, $s2
/* 098774 7F063C44 27AD0184 */  addiu $t5, $sp, 0x184
/* 098778 7F063C48 AFAD0010 */  sw    $t5, 0x10($sp)
/* 09877C 7F063C4C 27A40178 */  addiu $a0, $sp, 0x178
/* 098780 7F063C50 02602825 */  move  $a1, $s3
/* 098784 7F063C54 02403025 */  move  $a2, $s2
/* 098788 7F063C58 02C03825 */  move  $a3, $s6
/* 09878C 7F063C5C AFB50014 */  sw    $s5, 0x14($sp)
/* 098790 7F063C60 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098794 7F063C64 AFB00018 */   sw    $s0, 0x18($sp)
/* 098798 7F063C68 0011C180 */  sll   $t8, $s1, 6
/* 09879C 7F063C6C 03172821 */  addu  $a1, $t8, $s7
/* 0987A0 7F063C70 0FC16008 */  jal   matrix_4x4_copy
/* 0987A4 7F063C74 02002025 */   move  $a0, $s0
/* 0987A8 7F063C78 10000185 */  b     .L7F064290
/* 0987AC 7F063C7C 26310001 */   addiu $s1, $s1, 1
.L7F063C80:
/* 0987B0 7F063C80 24010006 */  li    $at, 6
/* 0987B4 7F063C84 16210029 */  bne   $s1, $at, .L7F063D2C
/* 0987B8 7F063C88 3C0E8003 */   lui   $t6, %hi(D_80035D44+0x60)
/* 0987BC 7F063C8C 25CE5DA4 */  addiu $t6, %lo(D_80035D44+0x60) # addiu $t6, $t6, 0x5da4
/* 0987C0 7F063C90 8DC10000 */  lw    $at, ($t6)
/* 0987C4 7F063C94 27A80160 */  addiu $t0, $sp, 0x160
/* 0987C8 7F063C98 8DC90004 */  lw    $t1, 4($t6)
/* 0987CC 7F063C9C AD010000 */  sw    $at, ($t0)
/* 0987D0 7F063CA0 8DC10008 */  lw    $at, 8($t6)
/* 0987D4 7F063CA4 AD090004 */  sw    $t1, 4($t0)
/* 0987D8 7F063CA8 27AB016C */  addiu $t3, $sp, 0x16c
/* 0987DC 7F063CAC AD010008 */  sw    $at, 8($t0)
/* 0987E0 7F063CB0 8FC10088 */  lw    $at, 0x88($fp)
/* 0987E4 7F063CB4 24050002 */  li    $a1, 2
/* 0987E8 7F063CB8 AD610000 */  sw    $at, ($t3)
/* 0987EC 7F063CBC 8FCA008C */  lw    $t2, 0x8c($fp)
/* 0987F0 7F063CC0 AD6A0004 */  sw    $t2, 4($t3)
/* 0987F4 7F063CC4 8FC10090 */  lw    $at, 0x90($fp)
/* 0987F8 7F063CC8 AD610008 */  sw    $at, 8($t3)
/* 0987FC 7F063CCC 0C0030C3 */  jal   joyGetButtons
/* 098800 7F063CD0 82840000 */   lb    $a0, ($s4)
/* 098804 7F063CD4 10400004 */  beqz  $v0, .L7F063CE8
/* 098808 7F063CD8 4600B306 */   mov.s $f12, $f22
/* 09880C 7F063CDC C7A80164 */  lwc1  $f8, 0x164($sp)
/* 098810 7F063CE0 46144280 */  add.s $f10, $f8, $f20
/* 098814 7F063CE4 E7AA0164 */  swc1  $f10, 0x164($sp)
.L7F063CE8:
/* 098818 7F063CE8 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 09881C 7F063CEC 02402825 */   move  $a1, $s2
/* 098820 7F063CF0 27AC016C */  addiu $t4, $sp, 0x16c
/* 098824 7F063CF4 AFAC0010 */  sw    $t4, 0x10($sp)
/* 098828 7F063CF8 27A40160 */  addiu $a0, $sp, 0x160
/* 09882C 7F063CFC 02602825 */  move  $a1, $s3
/* 098830 7F063D00 02403025 */  move  $a2, $s2
/* 098834 7F063D04 02C03825 */  move  $a3, $s6
/* 098838 7F063D08 AFB50014 */  sw    $s5, 0x14($sp)
/* 09883C 7F063D0C 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098840 7F063D10 AFB00018 */   sw    $s0, 0x18($sp)
/* 098844 7F063D14 00116980 */  sll   $t5, $s1, 6
/* 098848 7F063D18 01B72821 */  addu  $a1, $t5, $s7
/* 09884C 7F063D1C 0FC16008 */  jal   matrix_4x4_copy
/* 098850 7F063D20 02002025 */   move  $a0, $s0
/* 098854 7F063D24 1000015A */  b     .L7F064290
/* 098858 7F063D28 26310001 */   addiu $s1, $s1, 1
.L7F063D2C:
/* 09885C 7F063D2C 24010007 */  li    $at, 7
/* 098860 7F063D30 16210029 */  bne   $s1, $at, .L7F063DD8
/* 098864 7F063D34 3C0F8003 */   lui   $t7, %hi(D_80035D44+0x6C)
/* 098868 7F063D38 25EF5DB0 */  addiu $t7, %lo(D_80035D44+0x6C) # addiu $t7, $t7, 0x5db0
/* 09886C 7F063D3C 8DE10000 */  lw    $at, ($t7)
/* 098870 7F063D40 27B80148 */  addiu $t8, $sp, 0x148
/* 098874 7F063D44 8DEE0004 */  lw    $t6, 4($t7)
/* 098878 7F063D48 AF010000 */  sw    $at, ($t8)
/* 09887C 7F063D4C 8DE10008 */  lw    $at, 8($t7)
/* 098880 7F063D50 AF0E0004 */  sw    $t6, 4($t8)
/* 098884 7F063D54 27A90154 */  addiu $t1, $sp, 0x154
/* 098888 7F063D58 AF010008 */  sw    $at, 8($t8)
/* 09888C 7F063D5C 8FC10094 */  lw    $at, 0x94($fp)
/* 098890 7F063D60 24050001 */  li    $a1, 1
/* 098894 7F063D64 AD210000 */  sw    $at, ($t1)
/* 098898 7F063D68 8FCB0098 */  lw    $t3, 0x98($fp)
/* 09889C 7F063D6C AD2B0004 */  sw    $t3, 4($t1)
/* 0988A0 7F063D70 8FC1009C */  lw    $at, 0x9c($fp)
/* 0988A4 7F063D74 AD210008 */  sw    $at, 8($t1)
/* 0988A8 7F063D78 0C0030C3 */  jal   joyGetButtons
/* 0988AC 7F063D7C 82840000 */   lb    $a0, ($s4)
/* 0988B0 7F063D80 10400004 */  beqz  $v0, .L7F063D94
/* 0988B4 7F063D84 4600B306 */   mov.s $f12, $f22
/* 0988B8 7F063D88 C7B0014C */  lwc1  $f16, 0x14c($sp)
/* 0988BC 7F063D8C 46148480 */  add.s $f18, $f16, $f20
/* 0988C0 7F063D90 E7B2014C */  swc1  $f18, 0x14c($sp)
.L7F063D94:
/* 0988C4 7F063D94 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 0988C8 7F063D98 02402825 */   move  $a1, $s2
/* 0988CC 7F063D9C 27AA0154 */  addiu $t2, $sp, 0x154
/* 0988D0 7F063DA0 AFAA0010 */  sw    $t2, 0x10($sp)
/* 0988D4 7F063DA4 27A40148 */  addiu $a0, $sp, 0x148
/* 0988D8 7F063DA8 02602825 */  move  $a1, $s3
/* 0988DC 7F063DAC 02403025 */  move  $a2, $s2
/* 0988E0 7F063DB0 02C03825 */  move  $a3, $s6
/* 0988E4 7F063DB4 AFB50014 */  sw    $s5, 0x14($sp)
/* 0988E8 7F063DB8 0FC18D47 */  jal   sub_GAME_7F06351C
/* 0988EC 7F063DBC AFB00018 */   sw    $s0, 0x18($sp)
/* 0988F0 7F063DC0 00116180 */  sll   $t4, $s1, 6
/* 0988F4 7F063DC4 01972821 */  addu  $a1, $t4, $s7
/* 0988F8 7F063DC8 0FC16008 */  jal   matrix_4x4_copy
/* 0988FC 7F063DCC 02002025 */   move  $a0, $s0
/* 098900 7F063DD0 1000012F */  b     .L7F064290
/* 098904 7F063DD4 26310001 */   addiu $s1, $s1, 1
.L7F063DD8:
/* 098908 7F063DD8 24010009 */  li    $at, 9
/* 09890C 7F063DDC 16210029 */  bne   $s1, $at, .L7F063E84
/* 098910 7F063DE0 3C088003 */   lui   $t0, %hi(D_80035D44+0x78)
/* 098914 7F063DE4 25085DBC */  addiu $t0, %lo(D_80035D44+0x78) # addiu $t0, $t0, 0x5dbc
/* 098918 7F063DE8 8D010000 */  lw    $at, ($t0)
/* 09891C 7F063DEC 27AD0130 */  addiu $t5, $sp, 0x130
/* 098920 7F063DF0 8D0F0004 */  lw    $t7, 4($t0)
/* 098924 7F063DF4 ADA10000 */  sw    $at, ($t5)
/* 098928 7F063DF8 8D010008 */  lw    $at, 8($t0)
/* 09892C 7F063DFC ADAF0004 */  sw    $t7, 4($t5)
/* 098930 7F063E00 27AE013C */  addiu $t6, $sp, 0x13c
/* 098934 7F063E04 ADA10008 */  sw    $at, 8($t5)
/* 098938 7F063E08 8FC100AC */  lw    $at, 0xac($fp)
/* 09893C 7F063E0C 24054000 */  li    $a1, 16384
/* 098940 7F063E10 ADC10000 */  sw    $at, ($t6)
/* 098944 7F063E14 8FC900B0 */  lw    $t1, 0xb0($fp)
/* 098948 7F063E18 ADC90004 */  sw    $t1, 4($t6)
/* 09894C 7F063E1C 8FC100B4 */  lw    $at, 0xb4($fp)
/* 098950 7F063E20 ADC10008 */  sw    $at, 8($t6)
/* 098954 7F063E24 0C0030C3 */  jal   joyGetButtons
/* 098958 7F063E28 82840000 */   lb    $a0, ($s4)
/* 09895C 7F063E2C 10400004 */  beqz  $v0, .L7F063E40
/* 098960 7F063E30 4600B306 */   mov.s $f12, $f22
/* 098964 7F063E34 C7A40134 */  lwc1  $f4, 0x134($sp)
/* 098968 7F063E38 46142180 */  add.s $f6, $f4, $f20
/* 09896C 7F063E3C E7A60134 */  swc1  $f6, 0x134($sp)
.L7F063E40:
/* 098970 7F063E40 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098974 7F063E44 02402825 */   move  $a1, $s2
/* 098978 7F063E48 27AB013C */  addiu $t3, $sp, 0x13c
/* 09897C 7F063E4C AFAB0010 */  sw    $t3, 0x10($sp)
/* 098980 7F063E50 27A40130 */  addiu $a0, $sp, 0x130
/* 098984 7F063E54 02602825 */  move  $a1, $s3
/* 098988 7F063E58 02403025 */  move  $a2, $s2
/* 09898C 7F063E5C 02C03825 */  move  $a3, $s6
/* 098990 7F063E60 AFB50014 */  sw    $s5, 0x14($sp)
/* 098994 7F063E64 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098998 7F063E68 AFB00018 */   sw    $s0, 0x18($sp)
/* 09899C 7F063E6C 00115180 */  sll   $t2, $s1, 6
/* 0989A0 7F063E70 01572821 */  addu  $a1, $t2, $s7
/* 0989A4 7F063E74 0FC16008 */  jal   matrix_4x4_copy
/* 0989A8 7F063E78 02002025 */   move  $a0, $s0
/* 0989AC 7F063E7C 10000104 */  b     .L7F064290
/* 0989B0 7F063E80 26310001 */   addiu $s1, $s1, 1
.L7F063E84:
/* 0989B4 7F063E84 24010008 */  li    $at, 8
/* 0989B8 7F063E88 16210029 */  bne   $s1, $at, .L7F063F30
/* 0989BC 7F063E8C 3C188003 */   lui   $t8, %hi(D_80035D44+0x84)
/* 0989C0 7F063E90 27185DC8 */  addiu $t8, %lo(D_80035D44+0x84) # addiu $t8, $t8, 0x5dc8
/* 0989C4 7F063E94 8F010000 */  lw    $at, ($t8)
/* 0989C8 7F063E98 27AC0118 */  addiu $t4, $sp, 0x118
/* 0989CC 7F063E9C 8F080004 */  lw    $t0, 4($t8)
/* 0989D0 7F063EA0 AD810000 */  sw    $at, ($t4)
/* 0989D4 7F063EA4 8F010008 */  lw    $at, 8($t8)
/* 0989D8 7F063EA8 AD880004 */  sw    $t0, 4($t4)
/* 0989DC 7F063EAC 27AF0124 */  addiu $t7, $sp, 0x124
/* 0989E0 7F063EB0 AD810008 */  sw    $at, 8($t4)
/* 0989E4 7F063EB4 8FC100A0 */  lw    $at, 0xa0($fp)
/* 0989E8 7F063EB8 34058000 */  li    $a1, 32768
/* 0989EC 7F063EBC ADE10000 */  sw    $at, ($t7)
/* 0989F0 7F063EC0 8FCE00A4 */  lw    $t6, 0xa4($fp)
/* 0989F4 7F063EC4 ADEE0004 */  sw    $t6, 4($t7)
/* 0989F8 7F063EC8 8FC100A8 */  lw    $at, 0xa8($fp)
/* 0989FC 7F063ECC ADE10008 */  sw    $at, 8($t7)
/* 098A00 7F063ED0 0C0030C3 */  jal   joyGetButtons
/* 098A04 7F063ED4 82840000 */   lb    $a0, ($s4)
/* 098A08 7F063ED8 10400004 */  beqz  $v0, .L7F063EEC
/* 098A0C 7F063EDC 4600B306 */   mov.s $f12, $f22
/* 098A10 7F063EE0 C7A8011C */  lwc1  $f8, 0x11c($sp)
/* 098A14 7F063EE4 46144280 */  add.s $f10, $f8, $f20
/* 098A18 7F063EE8 E7AA011C */  swc1  $f10, 0x11c($sp)
.L7F063EEC:
/* 098A1C 7F063EEC 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098A20 7F063EF0 02402825 */   move  $a1, $s2
/* 098A24 7F063EF4 27A90124 */  addiu $t1, $sp, 0x124
/* 098A28 7F063EF8 AFA90010 */  sw    $t1, 0x10($sp)
/* 098A2C 7F063EFC 27A40118 */  addiu $a0, $sp, 0x118
/* 098A30 7F063F00 02602825 */  move  $a1, $s3
/* 098A34 7F063F04 02403025 */  move  $a2, $s2
/* 098A38 7F063F08 02C03825 */  move  $a3, $s6
/* 098A3C 7F063F0C AFB50014 */  sw    $s5, 0x14($sp)
/* 098A40 7F063F10 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098A44 7F063F14 AFB00018 */   sw    $s0, 0x18($sp)
/* 098A48 7F063F18 00115980 */  sll   $t3, $s1, 6
/* 098A4C 7F063F1C 01772821 */  addu  $a1, $t3, $s7
/* 098A50 7F063F20 0FC16008 */  jal   matrix_4x4_copy
/* 098A54 7F063F24 02002025 */   move  $a0, $s0
/* 098A58 7F063F28 100000D9 */  b     .L7F064290
/* 098A5C 7F063F2C 26310001 */   addiu $s1, $s1, 1
.L7F063F30:
/* 098A60 7F063F30 2401000A */  li    $at, 10
/* 098A64 7F063F34 1621002B */  bne   $s1, $at, .L7F063FE4
/* 098A68 7F063F38 3C0D8003 */   lui   $t5, %hi(D_80035D44+0x90)
/* 098A6C 7F063F3C 25AD5DD4 */  addiu $t5, %lo(D_80035D44+0x90) # addiu $t5, $t5, 0x5dd4
/* 098A70 7F063F40 8DA10000 */  lw    $at, ($t5)
/* 098A74 7F063F44 27AA0100 */  addiu $t2, $sp, 0x100
/* 098A78 7F063F48 8DB80004 */  lw    $t8, 4($t5)
/* 098A7C 7F063F4C AD410000 */  sw    $at, ($t2)
/* 098A80 7F063F50 8DA10008 */  lw    $at, 8($t5)
/* 098A84 7F063F54 AD580004 */  sw    $t8, 4($t2)
/* 098A88 7F063F58 27A8010C */  addiu $t0, $sp, 0x10c
/* 098A8C 7F063F5C AD410008 */  sw    $at, 8($t2)
/* 098A90 7F063F60 8FC100B8 */  lw    $at, 0xb8($fp)
/* 098A94 7F063F64 24050020 */  li    $a1, 32
/* 098A98 7F063F68 AD010000 */  sw    $at, ($t0)
/* 098A9C 7F063F6C 8FCF00BC */  lw    $t7, 0xbc($fp)
/* 098AA0 7F063F70 AD0F0004 */  sw    $t7, 4($t0)
/* 098AA4 7F063F74 8FC100C0 */  lw    $at, 0xc0($fp)
/* 098AA8 7F063F78 AD010008 */  sw    $at, 8($t0)
/* 098AAC 7F063F7C 0C0030C3 */  jal   joyGetButtons
/* 098AB0 7F063F80 82840000 */   lb    $a0, ($s4)
/* 098AB4 7F063F84 10400004 */  beqz  $v0, .L7F063F98
/* 098AB8 7F063F88 3C018005 */   lui   $at, %hi(D_80053F00)
/* 098ABC 7F063F8C C42C3F00 */  lwc1  $f12, %lo(D_80053F00)($at)
/* 098AC0 7F063F90 0FC1617F */  jal   matrix_4x4_set_rotation_around_y
/* 098AC4 7F063F94 02602825 */   move  $a1, $s3
.L7F063F98:
/* 098AC8 7F063F98 3C018005 */  lui   $at, %hi(D_80053F04)
/* 098ACC 7F063F9C C42C3F04 */  lwc1  $f12, %lo(D_80053F04)($at)
/* 098AD0 7F063FA0 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098AD4 7F063FA4 02402825 */   move  $a1, $s2
/* 098AD8 7F063FA8 27AE010C */  addiu $t6, $sp, 0x10c
/* 098ADC 7F063FAC AFAE0010 */  sw    $t6, 0x10($sp)
/* 098AE0 7F063FB0 27A40100 */  addiu $a0, $sp, 0x100
/* 098AE4 7F063FB4 02602825 */  move  $a1, $s3
/* 098AE8 7F063FB8 02403025 */  move  $a2, $s2
/* 098AEC 7F063FBC 02C03825 */  move  $a3, $s6
/* 098AF0 7F063FC0 AFB50014 */  sw    $s5, 0x14($sp)
/* 098AF4 7F063FC4 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098AF8 7F063FC8 AFB00018 */   sw    $s0, 0x18($sp)
/* 098AFC 7F063FCC 00114980 */  sll   $t1, $s1, 6
/* 098B00 7F063FD0 01372821 */  addu  $a1, $t1, $s7
/* 098B04 7F063FD4 0FC16008 */  jal   matrix_4x4_copy
/* 098B08 7F063FD8 02002025 */   move  $a0, $s0
/* 098B0C 7F063FDC 100000AC */  b     .L7F064290
/* 098B10 7F063FE0 26310001 */   addiu $s1, $s1, 1
.L7F063FE4:
/* 098B14 7F063FE4 24010003 */  li    $at, 3
/* 098B18 7F063FE8 1621004C */  bne   $s1, $at, .L7F06411C
/* 098B1C 7F063FEC 3C0C8003 */   lui   $t4, %hi(D_80035D44+0x9C)
/* 098B20 7F063FF0 258C5DE0 */  addiu $t4, %lo(D_80035D44+0x9C) # addiu $t4, $t4, 0x5de0
/* 098B24 7F063FF4 8D810000 */  lw    $at, ($t4)
/* 098B28 7F063FF8 27AB00A8 */  addiu $t3, $sp, 0xa8
/* 098B2C 7F063FFC 8D8D0004 */  lw    $t5, 4($t4)
/* 098B30 7F064000 AD610000 */  sw    $at, ($t3)
/* 098B34 7F064004 8D810008 */  lw    $at, 8($t4)
/* 098B38 7F064008 AD6D0004 */  sw    $t5, 4($t3)
/* 098B3C 7F06400C 27B800F4 */  addiu $t8, $sp, 0xf4
/* 098B40 7F064010 AD610008 */  sw    $at, 8($t3)
/* 098B44 7F064014 8FC10064 */  lw    $at, 0x64($fp)
/* 098B48 7F064018 27A400B4 */  addiu $a0, $sp, 0xb4
/* 098B4C 7F06401C AF010000 */  sw    $at, ($t8)
/* 098B50 7F064020 8FC80068 */  lw    $t0, 0x68($fp)
/* 098B54 7F064024 AF080004 */  sw    $t0, 4($t8)
/* 098B58 7F064028 8FC1006C */  lw    $at, 0x6c($fp)
/* 098B5C 7F06402C 0FC15FF4 */  jal   matrix_4x4_set_identity
/* 098B60 7F064030 AF010008 */   sw    $at, 8($t8)
/* 098B64 7F064034 82840000 */  lb    $a0, ($s4)
/* 098B68 7F064038 0C0030C3 */  jal   joyGetButtons
/* 098B6C 7F06403C 24050800 */   li    $a1, 2048
/* 098B70 7F064040 10400007 */  beqz  $v0, .L7F064060
/* 098B74 7F064044 24050400 */   li    $a1, 1024
/* 098B78 7F064048 3C018005 */  lui   $at, %hi(D_80053F08)
/* 098B7C 7F06404C C42C3F08 */  lwc1  $f12, %lo(D_80053F08)($at)
/* 098B80 7F064050 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098B84 7F064054 02602825 */   move  $a1, $s3
/* 098B88 7F064058 10000009 */  b     .L7F064080
/* 098B8C 7F06405C 82840000 */   lb    $a0, ($s4)
.L7F064060:
/* 098B90 7F064060 0C0030C3 */  jal   joyGetButtons
/* 098B94 7F064064 82840000 */   lb    $a0, ($s4)
/* 098B98 7F064068 10400004 */  beqz  $v0, .L7F06407C
/* 098B9C 7F06406C 3C018005 */   lui   $at, %hi(D_80053F0C)
/* 098BA0 7F064070 C42C3F0C */  lwc1  $f12, %lo(D_80053F0C)($at)
/* 098BA4 7F064074 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098BA8 7F064078 02602825 */   move  $a1, $s3
.L7F06407C:
/* 098BAC 7F06407C 82840000 */  lb    $a0, ($s4)
.L7F064080:
/* 098BB0 7F064080 0C0030C3 */  jal   joyGetButtons
/* 098BB4 7F064084 24050200 */   li    $a1, 512
/* 098BB8 7F064088 10400007 */  beqz  $v0, .L7F0640A8
/* 098BBC 7F06408C 24050100 */   li    $a1, 256
/* 098BC0 7F064090 3C018005 */  lui   $at, %hi(D_80053F10)
/* 098BC4 7F064094 C42C3F10 */  lwc1  $f12, %lo(D_80053F10)($at)
/* 098BC8 7F064098 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 098BCC 7F06409C 27A500B4 */   addiu $a1, $sp, 0xb4
/* 098BD0 7F0640A0 10000009 */  b     .L7F0640C8
/* 098BD4 7F0640A4 27A400B4 */   addiu $a0, $sp, 0xb4
.L7F0640A8:
/* 098BD8 7F0640A8 0C0030C3 */  jal   joyGetButtons
/* 098BDC 7F0640AC 82840000 */   lb    $a0, ($s4)
/* 098BE0 7F0640B0 10400004 */  beqz  $v0, .L7F0640C4
/* 098BE4 7F0640B4 3C018005 */   lui   $at, %hi(D_80053F14)
/* 098BE8 7F0640B8 C42C3F14 */  lwc1  $f12, %lo(D_80053F14)($at)
/* 098BEC 7F0640BC 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 098BF0 7F0640C0 27A500B4 */   addiu $a1, $sp, 0xb4
.L7F0640C4:
/* 098BF4 7F0640C4 27A400B4 */  addiu $a0, $sp, 0xb4
.L7F0640C8:
/* 098BF8 7F0640C8 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 098BFC 7F0640CC 02602825 */   move  $a1, $s3
/* 098C00 7F0640D0 3C018005 */  lui   $at, %hi(D_80053F18)
/* 098C04 7F0640D4 C42C3F18 */  lwc1  $f12, %lo(D_80053F18)($at)
/* 098C08 7F0640D8 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098C0C 7F0640DC 02402825 */   move  $a1, $s2
/* 098C10 7F0640E0 27AF00F4 */  addiu $t7, $sp, 0xf4
/* 098C14 7F0640E4 AFAF0010 */  sw    $t7, 0x10($sp)
/* 098C18 7F0640E8 27A400A8 */  addiu $a0, $sp, 0xa8
/* 098C1C 7F0640EC 02602825 */  move  $a1, $s3
/* 098C20 7F0640F0 02403025 */  move  $a2, $s2
/* 098C24 7F0640F4 02C03825 */  move  $a3, $s6
/* 098C28 7F0640F8 AFB50014 */  sw    $s5, 0x14($sp)
/* 098C2C 7F0640FC 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098C30 7F064100 AFB00018 */   sw    $s0, 0x18($sp)
/* 098C34 7F064104 00117180 */  sll   $t6, $s1, 6
/* 098C38 7F064108 01D72821 */  addu  $a1, $t6, $s7
/* 098C3C 7F06410C 0FC16008 */  jal   matrix_4x4_copy
/* 098C40 7F064110 02002025 */   move  $a0, $s0
/* 098C44 7F064114 1000005E */  b     .L7F064290
/* 098C48 7F064118 26310001 */   addiu $s1, $s1, 1
.L7F06411C:
/* 098C4C 7F06411C 24010001 */  li    $at, 1
/* 098C50 7F064120 16210029 */  bne   $s1, $at, .L7F0641C8
/* 098C54 7F064124 3C0A8003 */   lui   $t2, %hi(D_80035D44+0xA8)
/* 098C58 7F064128 254A5DEC */  addiu $t2, %lo(D_80035D44+0xA8) # addiu $t2, $t2, 0x5dec
/* 098C5C 7F06412C 8D410000 */  lw    $at, ($t2)
/* 098C60 7F064130 27A90090 */  addiu $t1, $sp, 0x90
/* 098C64 7F064134 8D4C0004 */  lw    $t4, 4($t2)
/* 098C68 7F064138 AD210000 */  sw    $at, ($t1)
/* 098C6C 7F06413C 8D410008 */  lw    $at, 8($t2)
/* 098C70 7F064140 AD2C0004 */  sw    $t4, 4($t1)
/* 098C74 7F064144 27AD009C */  addiu $t5, $sp, 0x9c
/* 098C78 7F064148 AD210008 */  sw    $at, 8($t1)
/* 098C7C 7F06414C 8FC1004C */  lw    $at, 0x4c($fp)
/* 098C80 7F064150 24051000 */  li    $a1, 4096
/* 098C84 7F064154 ADA10000 */  sw    $at, ($t5)
/* 098C88 7F064158 8FD80050 */  lw    $t8, 0x50($fp)
/* 098C8C 7F06415C ADB80004 */  sw    $t8, 4($t5)
/* 098C90 7F064160 8FC10054 */  lw    $at, 0x54($fp)
/* 098C94 7F064164 ADA10008 */  sw    $at, 8($t5)
/* 098C98 7F064168 0C0030C3 */  jal   joyGetButtons
/* 098C9C 7F06416C 82840000 */   lb    $a0, ($s4)
/* 098CA0 7F064170 10400004 */  beqz  $v0, .L7F064184
/* 098CA4 7F064174 4600B306 */   mov.s $f12, $f22
/* 098CA8 7F064178 C7B00094 */  lwc1  $f16, 0x94($sp)
/* 098CAC 7F06417C 46148480 */  add.s $f18, $f16, $f20
/* 098CB0 7F064180 E7B20094 */  swc1  $f18, 0x94($sp)
.L7F064184:
/* 098CB4 7F064184 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098CB8 7F064188 02402825 */   move  $a1, $s2
/* 098CBC 7F06418C 27A8009C */  addiu $t0, $sp, 0x9c
/* 098CC0 7F064190 AFA80010 */  sw    $t0, 0x10($sp)
/* 098CC4 7F064194 27A40090 */  addiu $a0, $sp, 0x90
/* 098CC8 7F064198 02602825 */  move  $a1, $s3
/* 098CCC 7F06419C 02403025 */  move  $a2, $s2
/* 098CD0 7F0641A0 02C03825 */  move  $a3, $s6
/* 098CD4 7F0641A4 AFB50014 */  sw    $s5, 0x14($sp)
/* 098CD8 7F0641A8 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098CDC 7F0641AC AFB00018 */   sw    $s0, 0x18($sp)
/* 098CE0 7F0641B0 00117980 */  sll   $t7, $s1, 6
/* 098CE4 7F0641B4 01F72821 */  addu  $a1, $t7, $s7
/* 098CE8 7F0641B8 0FC16008 */  jal   matrix_4x4_copy
/* 098CEC 7F0641BC 02002025 */   move  $a0, $s0
/* 098CF0 7F0641C0 10000033 */  b     .L7F064290
/* 098CF4 7F0641C4 26310001 */   addiu $s1, $s1, 1
.L7F0641C8:
/* 098CF8 7F0641C8 2401000C */  li    $at, 12
/* 098CFC 7F0641CC 1621002C */  bne   $s1, $at, .L7F064280
/* 098D00 7F0641D0 8FA4053C */   lw    $a0, 0x53c($sp)
/* 098D04 7F0641D4 3C0B8003 */  lui   $t3, %hi(D_80035D44+0xB4)
/* 098D08 7F0641D8 256B5DF8 */  addiu $t3, %lo(D_80035D44+0xB4) # addiu $t3, $t3, 0x5df8
/* 098D0C 7F0641DC 8D610000 */  lw    $at, ($t3)
/* 098D10 7F0641E0 27AE0078 */  addiu $t6, $sp, 0x78
/* 098D14 7F0641E4 8D6A0004 */  lw    $t2, 4($t3)
/* 098D18 7F0641E8 ADC10000 */  sw    $at, ($t6)
/* 098D1C 7F0641EC 8D610008 */  lw    $at, 8($t3)
/* 098D20 7F0641F0 ADCA0004 */  sw    $t2, 4($t6)
/* 098D24 7F0641F4 27AC0084 */  addiu $t4, $sp, 0x84
/* 098D28 7F0641F8 ADC10008 */  sw    $at, 8($t6)
/* 098D2C 7F0641FC 8FC100D0 */  lw    $at, 0xd0($fp)
/* 098D30 7F064200 24052000 */  li    $a1, 8192
/* 098D34 7F064204 AD810000 */  sw    $at, ($t4)
/* 098D38 7F064208 8FCD00D4 */  lw    $t5, 0xd4($fp)
/* 098D3C 7F06420C AD8D0004 */  sw    $t5, 4($t4)
/* 098D40 7F064210 8FC100D8 */  lw    $at, 0xd8($fp)
/* 098D44 7F064214 AD810008 */  sw    $at, 8($t4)
/* 098D48 7F064218 0C0030C3 */  jal   joyGetButtons
/* 098D4C 7F06421C 82840000 */   lb    $a0, ($s4)
/* 098D50 7F064220 10400004 */  beqz  $v0, .L7F064234
/* 098D54 7F064224 3C018005 */   lui   $at, %hi(D_80053F1C)
/* 098D58 7F064228 C42C3F1C */  lwc1  $f12, %lo(D_80053F1C)($at)
/* 098D5C 7F06422C 0FC1615C */  jal   matrix_4x4_set_rotation_around_x
/* 098D60 7F064230 02602825 */   move  $a1, $s3
.L7F064234:
/* 098D64 7F064234 3C018005 */  lui   $at, %hi(D_80053F20)
/* 098D68 7F064238 C42C3F20 */  lwc1  $f12, %lo(D_80053F20)($at)
/* 098D6C 7F06423C 0FC161A2 */  jal   matrix_4x4_set_rotation_around_z
/* 098D70 7F064240 02402825 */   move  $a1, $s2
/* 098D74 7F064244 27B80084 */  addiu $t8, $sp, 0x84
/* 098D78 7F064248 AFB80010 */  sw    $t8, 0x10($sp)
/* 098D7C 7F06424C 27A40078 */  addiu $a0, $sp, 0x78
/* 098D80 7F064250 02602825 */  move  $a1, $s3
/* 098D84 7F064254 02403025 */  move  $a2, $s2
/* 098D88 7F064258 02C03825 */  move  $a3, $s6
/* 098D8C 7F06425C AFB50014 */  sw    $s5, 0x14($sp)
/* 098D90 7F064260 0FC18D47 */  jal   sub_GAME_7F06351C
/* 098D94 7F064264 AFB00018 */   sw    $s0, 0x18($sp)
/* 098D98 7F064268 00114180 */  sll   $t0, $s1, 6
/* 098D9C 7F06426C 01172821 */  addu  $a1, $t0, $s7
/* 098DA0 7F064270 0FC16008 */  jal   matrix_4x4_copy
/* 098DA4 7F064274 02002025 */   move  $a0, $s0
/* 098DA8 7F064278 10000005 */  b     .L7F064290
/* 098DAC 7F06427C 26310001 */   addiu $s1, $s1, 1
.L7F064280:
/* 098DB0 7F064280 00117980 */  sll   $t7, $s1, 6
/* 098DB4 7F064284 0FC16008 */  jal   matrix_4x4_copy
/* 098DB8 7F064288 01F72821 */   addu  $a1, $t7, $s7
/* 098DBC 7F06428C 26310001 */  addiu $s1, $s1, 1
.L7F064290:
/* 098DC0 7F064290 2A21000D */  slti  $at, $s1, 0xd
/* 098DC4 7F064294 5420FD91 */  bnezl $at, .L7F0638DC
/* 098DC8 7F064298 3C01C0A0 */   lui   $at, 0xc0a0
/* 098DCC 7F06429C 0FC1BBF1 */  jal   modelUpdateNodeRelations
/* 098DD0 7F0642A0 27A404D8 */   addiu $a0, $sp, 0x4d8
/* 098DD4 7F0642A4 8FA90538 */  lw    $t1, 0x538($sp)
/* 098DD8 7F0642A8 27A404F8 */  addiu $a0, $sp, 0x4f8
/* 098DDC 7F0642AC 27A504D8 */  addiu $a1, $sp, 0x4d8
/* 098DE0 7F0642B0 0FC1D1A1 */  jal   subdraw
/* 098DE4 7F0642B4 AFA90504 */   sw    $t1, 0x504($sp)
/* 098DE8 7F0642B8 8FAE0504 */  lw    $t6, 0x504($sp)
/* 098DEC 7F0642BC 0FC16319 */  jal   matrix_4x4_7F058C64
/* 098DF0 7F0642C0 AFAE0538 */   sw    $t6, 0x538($sp)
/* 098DF4 7F0642C4 8FAB03D8 */  lw    $t3, 0x3d8($sp)
/* 098DF8 7F0642C8 00008825 */  move  $s1, $zero
/* 098DFC 7F0642CC 00008025 */  move  $s0, $zero
/* 098E00 7F0642D0 856A000E */  lh    $t2, 0xe($t3)
/* 098E04 7F0642D4 19400011 */  blez  $t2, .L7F06431C
/* 098E08 7F0642D8 00000000 */   nop
/* 098E0C 7F0642DC 8FB904E4 */  lw    $t9, 0x4e4($sp)
.L7F0642E0:
/* 098E10 7F0642E0 27A5041C */  addiu $a1, $sp, 0x41c
/* 098E14 7F0642E4 0FC16008 */  jal   matrix_4x4_copy
/* 098E18 7F0642E8 03302021 */   addu  $a0, $t9, $s0
/* 098E1C 7F0642EC 8FAD04E4 */  lw    $t5, 0x4e4($sp)
/* 098E20 7F0642F0 00116180 */  sll   $t4, $s1, 6
/* 098E24 7F0642F4 27A4041C */  addiu $a0, $sp, 0x41c
/* 098E28 7F0642F8 0FC16327 */  jal   matrix_4x4_f32_to_s32
/* 098E2C 7F0642FC 018D2821 */   addu  $a1, $t4, $t5
/* 098E30 7F064300 8FB803D8 */  lw    $t8, 0x3d8($sp)
/* 098E34 7F064304 26310001 */  addiu $s1, $s1, 1
/* 098E38 7F064308 26100040 */  addiu $s0, $s0, 0x40
/* 098E3C 7F06430C 8708000E */  lh    $t0, 0xe($t8)
/* 098E40 7F064310 0228082A */  slt   $at, $s1, $t0
/* 098E44 7F064314 5420FFF2 */  bnezl $at, .L7F0642E0
/* 098E48 7F064318 8FB904E4 */   lw    $t9, 0x4e4($sp)
.L7F06431C:
/* 098E4C 7F06431C 0FC16322 */  jal   matrix_4x4_7F058C88
/* 098E50 7F064320 00000000 */   nop
.L7F064324:
/* 098E54 7F064324 8FA20538 */  lw    $v0, 0x538($sp)
.L7F064328:
/* 098E58 7F064328 8FBF006C */  lw    $ra, 0x6c($sp)
/* 098E5C 7F06432C D7B40030 */  ldc1  $f20, 0x30($sp)
/* 098E60 7F064330 D7B60038 */  ldc1  $f22, 0x38($sp)
/* 098E64 7F064334 D7B80040 */  ldc1  $f24, 0x40($sp)
/* 098E68 7F064338 8FB00048 */  lw    $s0, 0x48($sp)
/* 098E6C 7F06433C 8FB1004C */  lw    $s1, 0x4c($sp)
/* 098E70 7F064340 8FB20050 */  lw    $s2, 0x50($sp)
/* 098E74 7F064344 8FB30054 */  lw    $s3, 0x54($sp)
/* 098E78 7F064348 8FB40058 */  lw    $s4, 0x58($sp)
/* 098E7C 7F06434C 8FB5005C */  lw    $s5, 0x5c($sp)
/* 098E80 7F064350 8FB60060 */  lw    $s6, 0x60($sp)
/* 098E84 7F064354 8FB70064 */  lw    $s7, 0x64($sp)
/* 098E88 7F064358 8FBE0068 */  lw    $fp, 0x68($sp)
/* 098E8C 7F06435C 03E00008 */  jr    $ra
/* 098E90 7F064360 27BD0538 */   addiu $sp, $sp, 0x538
)
#endif





#ifdef NONMATCHING
void sub_GAME_7F064364(void) {

}
#else
GLOBAL_ASM(
.text
glabel sub_GAME_7F064364
/* 098E94 7F064364 27BDFFE0 */  addiu $sp, $sp, -0x20
/* 098E98 7F064368 AFA7002C */  sw    $a3, 0x2c($sp)
/* 098E9C 7F06436C 8FAE002C */  lw    $t6, 0x2c($sp)
/* 098EA0 7F064370 8FAF0030 */  lw    $t7, 0x30($sp)
/* 098EA4 7F064374 00C03825 */  move  $a3, $a2
/* 098EA8 7F064378 AFBF001C */  sw    $ra, 0x1c($sp)
/* 098EAC 7F06437C AFA60028 */  sw    $a2, 0x28($sp)
/* 098EB0 7F064380 240600FF */  li    $a2, 255
/* 098EB4 7F064384 AFAE0010 */  sw    $t6, 0x10($sp)
/* 098EB8 7F064388 0FC18D67 */  jal   sub_GAME_7F06359C
/* 098EBC 7F06438C AFAF0014 */   sw    $t7, 0x14($sp)
/* 098EC0 7F064390 8FBF001C */  lw    $ra, 0x1c($sp)
/* 098EC4 7F064394 27BD0020 */  addiu $sp, $sp, 0x20
/* 098EC8 7F064398 03E00008 */  jr    $ra
/* 098ECC 7F06439C 00000000 */   nop
)
#endif



ALSoundState* sub_GAME_7F0643A0(void)
{
    s32 i;
    for (i = 0; i < 4; i++) {
        if (dword_CODE_bss_80075DB8[i] == 0) {
            return &dword_CODE_bss_80075DB8[i];
        }
    }
    return NULL;
}


void recall_joy2_hits_edit_detail_edit_flag(enum ITEM_IDS item, PropRecord* prop, s32 texture_index)
{
    s32 sp6C;
    u32 rnd1;
    u32 rnd2;
    ALSoundState* sound_state;
    struct RicochetSoundsSmall ricochet_sounds_small_copy;
    struct PunchSounds punch_sounds_copy;
    struct BulletFleshSounds bullet_flesh_sounds_copy;
    u32 sfx_index;

    sp6C = sub_GAME_7F0539E4(&prop->pos);

    rnd1 = randomGetNext();
    rnd2 = randomGetNext();

    D_800483C4 = texture_index;

    if (get_debug_joy2hitsedit_flag() == 0)
    {
        get_debug_joy2detailedit_flag();
    }

    if ((item == ITEM_REMOTEMINE)
        || (item == ITEM_PROXIMITYMINE)
        || (item == ITEM_TIMEDMINE)
        || (item == ITEM_BOMBCASE)
        || (item == ITEM_BUG)
        || (item == ITEM_MICROCAMERA)
        || (item == ITEM_PLASTIQUE)
        || (item == ITEM_WATCHLASER)
        || (item == ITEM_WATCHMAGNETATTRACT))
    {
        return;
    }

#ifdef BUGFIX_R1
    if (g_ClockTimer <= 0) { return; }
#endif

    sound_state = sub_GAME_7F0643A0();
    if (sound_state != NULL)
    {
        if ((prop->type != 3) && (prop->type != 6))
        {
            if (item == ITEM_LASER)
            {
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, RICO_LASER1_SFX, sound_state);
            }
            else
            {
                ricochet_sounds_small_copy = ricochet_sounds_small;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, ricochet_sounds_small_copy.arr[rnd1 % 20], sound_state);
            }

            if (sound_state->link.next != NULL)
            {
                sndCreatePostEvent((ALSoundState* ) sound_state->link.next, 8, sp6C);
            }
        }
        else
        {
            if (item == ITEM_KNIFE)
            {
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, HIT_BULLET_SNOW_SFX, sound_state);
            }
            else if (item == ITEM_FIST)
            {
                punch_sounds_copy = punch_sounds;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, punch_sounds_copy.arr[rnd1 % 3], sound_state);
            }
            else
            {
                bullet_flesh_sounds_copy = bullet_flesh_sounds;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, bullet_flesh_sounds_copy.arr[rnd1 % 2], sound_state);
            }

            if (sound_state->link.next != NULL) {
                sndCreatePostEvent((ALSoundState* ) sound_state->link.next, 8, sp6C);
            }
        }
    }

    sound_state = sub_GAME_7F0643A0();
    if ((sound_state != NULL) && (texture_index >= 0))
    {
        if (D_8004E86C[g_Textures[texture_index].hitSound] != NULL)
        {
            if (D_8004E86C[g_Textures[texture_index].hitSound]->sfx_len > 0)
            {
                sfx_index = rnd2 % D_8004E86C[g_Textures[texture_index].hitSound]->sfx_len;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, D_8004E86C[g_Textures[texture_index].hitSound]->sfx[sfx_index], sound_state);
            }

            if (sound_state->link.next != NULL)
            {
                chrobjSndCreatePostEventDefault((ALSoundState* ) sound_state->link.next, &prop->pos);
            }
        }
    }
#ifdef DEBUG
    osSyncPrintf("Shot prop: hittype %d\n", g_Textures[texture_index].hitSound);
#endif
#ifdef ENABLE_LOG
    osSyncPrintf("Shot prop:  %S\n", HIT_TYPE_ToString[g_Textures[texture_index].hitSound]);
#endif
}


void sub_GAME_7F064720(coord3d* pos)
{
    ALSoundState* sound;
    ALLink* link;

#ifdef BUGFIX_R1
    if (g_ClockTimer <= 0) { return; }
#endif

    sound = sub_GAME_7F0643A0();

    if (sound != NULL)
    {
        sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, HIT_BULLET_GLASS_SFX, sound);

        link = sound->link.next;
        if (link != NULL)
        {
            chrobjSndCreatePostEventDefault((ALSoundState* ) link, pos);
        }
    }
}


void recall_joy2_hits_edit_flag(enum ITEM_IDS item, coord3d* arg1, s32 texture_index)
{
    ALSoundState *sound_state;
    u32 rnd1;
    u32 rnd2;
    struct LaserRichochetSounds laser_copied;
    struct RicochetSoundsLarge rico_copied;
    u32 sfx_index;
    struct image_sound *img_sound;

    rnd1 = randomGetNext();
    rnd2 = randomGetNext();

    D_800483C4 = texture_index;
    get_debug_joy2hitsedit_flag();

#ifdef BUGFIX_R1
    if (g_ClockTimer <= 0) { return; }
#endif

    sound_state = sub_GAME_7F0643A0();
    if (sound_state != NULL)
    {
        if (item != ITEM_WATCHLASER)
        {
            if (item == ITEM_LASER)
            {
                laser_copied = laser_ricochet_sounds;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, laser_copied.arr[rnd1 % 2], sound_state);
            }
            else
            {
                rico_copied = ricochet_sounds_large;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, rico_copied.arr[rnd1 % 36], sound_state);
            }
        }

        if (sound_state->link.next != NULL)
        {
            chrobjSndCreatePostEventDefault((ALSoundState* ) sound_state->link.next, arg1);
        }
    }

    sound_state = sub_GAME_7F0643A0();
    if ((sound_state != NULL) && (texture_index >= 0))
    {
        img_sound = D_8004E86C[g_Textures[texture_index].hitSound];
        if (img_sound->sfx_len > 0)
        {
            if (img_sound != NULL)
            {
                sfx_index = rnd2 % img_sound->sfx_len;
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, img_sound->sfx[sfx_index], sound_state);
            }

            if (sound_state->link.next != NULL)
            {
                chrobjSndCreatePostEventDefault((ALSoundState* ) sound_state->link.next, arg1);
            }
        }
    }
}


void sub_GAME_7F064934(ITEM_IDS item)
{
    struct EarWhistleSounds copied;

#ifdef BUGFIX_R1
    if (g_ClockTimer <= 0) { return; }
#endif
    if ((item != ITEM_LASER) && (item != ITEM_WATCHLASER))
    {
        copied = ear_whistle_sounds;
        sndPlaySfx((struct ALBankAlt_s*) g_musicSfxBufferPtr, copied.arr[randomGetNext() % 5], 0);
    }
}


f32 sub_GAME_7F0649AC(s32 param_1)
{
  f32 fVar1;

  fVar1 = -60.0f;
  if (param_1 == 0x19) {
    fVar1 -= 20.0f;
  }
  return fVar1;
}



void sub_GAME_7F0649D8(enum GUNHAND hand)
{
    struct hand* hand_ptr;
    enum ITEM_IDS item_id;
    s32 ammo_in_magazine;
    s32 ammo_in_hands;
    WeaponStats* item_stats;
    s32 magsize;
    s32 ammo_total;

    hand_ptr = &g_CurrentPlayer->hands[hand];
    item_id = getCurrentPlayerWeaponId(hand);
    ammo_in_magazine = hand_ptr->weapon_ammo_in_magazine;
    ammo_in_hands = get_ammo_in_hands_weapon(hand);
    item_stats = get_ptr_item_statistics(item_id);
    ammo_total = ammo_in_hands + ammo_in_magazine;

    hand_ptr->weapon_ammo_in_magazine = (ammo_total >= item_stats->MagSize)
        ? item_stats->MagSize
        : ammo_total;

    g_CurrentPlayer->ammoheldarr[item_stats->AmmoType] = (bondwalkItemCheckBitflags(item_id, WEAPONSTATBITFLAG_AMMO_CLIP_LIMIT) != 0)
        ? 0
        : (g_CurrentPlayer->ammoheldarr[item_stats->AmmoType] - hand_ptr->weapon_ammo_in_magazine) + ammo_in_magazine;

    if (item_id == ITEM_ROCKETLAUNCH)
    {
        currentPlayerCreateRocket(hand);
        return;
    }

    if ((item_id == ITEM_SHOTGUN) || (item_id == ITEM_AUTOSHOT))
    {
        ammo_in_hands = get_ammo_in_hands_weapon(hand);
        if (ammo_in_hands >= 5)
        {
            hand_ptr->field_8A4 = 5;
            return;
        }
        hand_ptr->field_8A4 = ammo_in_hands;
    }
}

#if defined(VERSION_US) || defined(VERSION_JP)
    #define F_7F05C6FC_ARG1(x) ((f32)(x))
    #define WHEN_1_CASE_GRENADELAUNCH_FLD890 6
    #define WHEN_1_CASE_GRENADE_FLD890 0xf0
    #define WHEN_D_FLD890 0x14
    #define WHEN_5_SP188_INIT 0x10
    #define WHEN_5_SP188_MULTI 0xc
    #define WHEN_5_FLD8B0_SP 0x11
    #define WHEN_5_FLD8B0_MULTI 0xd
    #define WHEN_8_SP178_INIT 0x17
    #define WHEN_8_SP178_MULTI 0xc
    #define WHEN_A_FLD890 0x10
    #define WHEN_A_FLD8B0 0x11
    #define WHEN_C_FLD890 0x17
    #define WHEN_E_FLD890 0x10
    #define WHEN_10_FLD890 0x17
    #define WHEN_11_FLD890_1 0x10
    #define WHEN_11_FLD890_2 0x18
    #define WHEN_1E_FLD890 0x1e
#endif
#if defined(VERSION_EU)
    #define F_7F05C6FC_ARG1(x) ((f32)(x)) * 60.0f / 50.0f
    #define WHEN_1_CASE_GRENADELAUNCH_FLD890 5
    #define WHEN_1_CASE_GRENADE_FLD890 0xc8
    #define WHEN_D_FLD890 0x10
    #define WHEN_5_SP188_INIT 0xd
    #define WHEN_5_SP188_MULTI 0xa
    #define WHEN_5_FLD8B0_SP 0xe
    #define WHEN_5_FLD8B0_MULTI 0xa
    #define WHEN_8_SP178_INIT 0x13
    #define WHEN_8_SP178_MULTI 0xa
    #define WHEN_A_FLD890 0xd
    #define WHEN_A_FLD8B0 0xe
    #define WHEN_C_FLD890 0x13
    #define WHEN_E_FLD890 0xd
    #define WHEN_10_FLD890 0x13
    #define WHEN_11_FLD890_1 0xd
    #define WHEN_11_FLD890_2 0x14
    #define WHEN_1E_FLD890 0x19
#endif

// 0x7f064b28 NTSC
void handle_weapon_id_values_possibly_1st_person_animation(enum GUNHAND arg0, s32 arg1)
{
#if defined(VERSION_US)
    s32 stack1;
    s32 stack2;
    s32 sp1C4;
    s32 stack3;
    struct hand *sp1BC;
    s32 stack4;
    s32 sp1B4;
    struct sfx2 sp1B0;
    s32 stack5;
    struct WeaponStats *weapon_stats;
    s32 sp1A4;
    s32 sp1A0;
    f32 sp19C;
    f32 sp198;
    s32 stack7;
    f32 sp190;
    f32 sp18C;
    s32 sp188;
    f32 sp184;
    s32 stack8;
    s32 stack9;
    s32 sp178;
    f32 sp174;
    s32 stack10;
    s32 stack11;
    Mtxf sp12C;
    f32 sp128;
    s32 stack12;
    Mtxf spE4;
    f32 tempf;
    struct hand *temp_s0;
    Mtxf sp9C;
    f32 sp98;
    f32 sp94;
    enum ITEM_IDS temp_v0_3;
    f32 sp8C;
    f32 sp88;
    enum ITEM_IDS var_s1;
    struct sfx3 sp7C;
    struct PropRecord *temp_v0_8;
    Weapon1PTransformKeyframe *sp74;
    f32 temp_f0_2;
    u32 var_a0_2;
    f32 temp_v1_9;
    struct hand *temp_v1_5;
    f32 un_f32_num = 0.0f;
    f32 un_f32_div_1 = 16.0f;
    f32 un_f32_div_2 = 23.0f;
    s32 stack14;
    s32 stack15;
#endif
#if defined(VERSION_JP)
    s32 stack1;
    s32 stack2;
    s32 sp1C4;
    s32 stack3;
    struct hand *sp1BC;
    s32 stack4;
    s32 sp1B4;
    struct sfx2 sp1B0;
    s32 stack5;
    struct WeaponStats *weapon_stats;
    s32 sp1A4;
    s32 sp1A0;
    f32 sp19C;
    s32 stat_2;
    s32 stat_3;
    s32 stat_4;
    f32 sp198;
    s32 stack14;
    f32 sp190;
    f32 sp18C;
    s32 sp188;
    f32 sp184;
    s32 stack7;
    s32 stack8;
    s32 sp178;
    f32 sp174;
    s32 stack9;
    s32 stack10;
    Mtxf sp12C;
    f32 sp128;
    s32 stack11;
    Mtxf spE4;
    s32 stack12;
    f32 tempf;
    Mtxf sp9C;
    f32 sp98;
    f32 sp94;
    struct hand *temp_s0;
    f32 sp8C;
    f32 sp88;
    enum ITEM_IDS temp_v0_3;
    struct sfx3 sp7C;
    enum ITEM_IDS var_s1;
    Weapon1PTransformKeyframe *sp74;
    struct PropRecord *temp_v0_8;
    f32 temp_f0_2;
    u32 var_a0_2;
    f32 temp_v1_9;
    struct hand *temp_v1_5;
    f32 un_f32_num = 0.0f;
    f32 un_f32_div_1 = 16.0f;
    f32 un_f32_div_2 = 23.0f;
    s32 stack15;
#endif
#if defined(VERSION_EU)
    s32 stack1;
    s32 stack2;
    s32 sp1C4;
    s32 stack3;
    struct hand *sp1BC;
    s32 stack4;
    s32 sp1B4;
    struct sfx2 sp1B0;
    s32 stack5;
    struct WeaponStats *weapon_stats;
    s32 sp1A4;
    s32 sp1A0;
    f32 sp19C;
    s32 stat_2;
    s32 stat_3;
    s32 stat_4;
    f32 sp198;
    s32 stack14;
    f32 sp190;
    f32 sp18C;
    s32 sp188;
    f32 sp184;
    s32 stack7;
    s32 stack8;
    s32 sp178;
    f32 sp174;
    s32 stack9;
    s32 stack10;
    Mtxf sp12C;
    f32 sp128;
    s32 stack11;
    Mtxf spE4;
    s32 stack12;
    f32 tempf;
    Mtxf sp9C;
    f32 sp98;
    f32 sp94;
    struct hand *temp_s0;
    f32 sp8C;
    f32 sp88;
    enum ITEM_IDS temp_v0_3;
    struct sfx3 sp7C;
    enum ITEM_IDS var_s1;
    Weapon1PTransformKeyframe *sp74;
    struct PropRecord *temp_v0_8;
    f32 temp_f0_2;
    u32 var_a0_2;
    f32 temp_v1_9;
    struct hand *temp_v1_5;
    f32 un_f32_num = 0.0f;
    f32 un_f32_div_1 = 13.0f;
    f32 un_f32_div_2 = 19.0f;
    s32 stack15;
#endif

    temp_s0 = &g_CurrentPlayer->hands[arg0];
    var_s1 = get_item_in_hand_or_watch_menu(arg0);
    sp1C4 = get_ammo_type_for_weapon(var_s1);

    temp_s0->field_884 = temp_s0->weapon_hold_time;
    temp_s0->weapon_hold_time = arg1;

    if (arg1 == 0)
    {
        temp_s0->field_888 = 1;
    }

    temp_s0->weapon_firing_status = 0;
    temp_s0->field_87D = 0;

    if (g_ClockTimer > 0)
    {
        temp_s0->field_890 += g_ClockTimer;
        temp_s0->field_88C += 1;
    }

    temp_s0->field_92C = 0;

    if (temp_s0->when_detonating_mines_is_0 == 0)
    {
#if defined(VERSION_JP) || defined(VERSION_EU)
        if ((var_s1 == ITEM_LASER) && (temp_s0->field_888 != 0))
        {
            temp_s0->field_8A0 = 0;
        }
#endif
        if (
            (temp_s0->weapon_hold_time != 0)
            && (var_s1 != ITEM_UNARMED)
            && (((bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_CLICKY) != 0)) || (temp_s0->weapon_ammo_in_magazine > 0))
#if defined(VERSION_JP) || defined(VERSION_EU)
            && ((var_s1 != ITEM_LASER) || (temp_s0->field_8A0 < 0xC8))
#endif
        )
        {
            temp_s0->when_detonating_mines_is_0 = 1;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            temp_s0->field_888 = 0;
        }
        else
        {
            if (temp_s0->weapon_current_animation != 0)
            {
                temp_s0->when_detonating_mines_is_0 = temp_s0->weapon_current_animation;
                temp_s0->field_890 = 0;
                temp_s0->field_88C = 0;
            }
        }

        temp_s0->weapon_current_animation = 0;

        if ((temp_s0->when_detonating_mines_is_0 == 0)
            && (temp_s0->weapon_ammo_in_magazine == 0)
            && (sp1C4 != 0))
        {
            if ((lvlGetControlsLockedFlag() == 0) && (g_CurrentPlayer->mpmenuon == 0))
            {
                /**
                 * D_80032458 is always 0 so this branch can never execute.
                 */
                if ((D_80032458 != 0) && (sp1C4 == 1) && (g_CurrentPlayer->ammoheldarr[sp1C4] <= 0))
                {
                    g_CurrentPlayer->ammoheldarr[sp1C4] = 1;
                }

                if (get_ammo_in_hands_weapon(arg0) > 0)
                {
                    temp_s0->when_detonating_mines_is_0 = 9;
                    temp_s0->field_890 = 0;
                    temp_s0->field_88C = 0;
                }
                else
                {
                    if (g_CurrentPlayer->trigger_released != 0)
                    {
                        temp_v0_3 = get_item_in_hand_or_watch_menu(1 - arg0);

                        sp1BC = (g_CurrentPlayer->hands - arg0) + 1;

                        if ((sp1BC->when_detonating_mines_is_0 == 0)
                            && (sp1BC->weapon_current_animation == 0)
                            && (
                                (temp_v0_3 == ITEM_UNARMED)
                                || ((sp1BC->weapon_ammo_in_magazine == 0)
                                    && ((get_ammo_type_for_weapon(temp_v0_3) != 0))
                                    && ((get_ammo_in_hands_weapon(1 - arg0) <= 0)))))
                        {
                            autoadvance_on_deplete_all_ammo();

                            temp_s0->field_88C = 0;
                            temp_s0->field_890 = 0;
                            temp_s0->when_detonating_mines_is_0 = temp_s0->weapon_current_animation;
                            temp_s0->weapon_current_animation = 0;

                            sp1BC->field_88C = 0;
                            sp1BC->field_890 = 0;
                            sp1BC->when_detonating_mines_is_0 = sp1BC->weapon_current_animation;
                            sp1BC->weapon_current_animation = 0;
                        }
                    }
                }
            }
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 1)
    {
        switch (var_s1)
        {
        case ITEM_RUGER:
        case ITEM_GRENADELAUNCH:
            if (temp_s0->field_890 >= WHEN_1_CASE_GRENADELAUNCH_FLD890)
            {
                temp_s0->when_detonating_mines_is_0 = 2;
                temp_s0->field_890 = 0;
                temp_s0->field_88C = 0;
            }
            break;
        case ITEM_CAMERA:
            if (temp_s0->field_88C == 0)
            {
                currentPlayerSetFadeColour(0, 0, 0, 1.0f);
            }
            else if (temp_s0->field_890 > 0)
            {
                currentPlayerAdjustFade(8.0f, 0, 0, 0, 0.0f);
                temp_s0->when_detonating_mines_is_0 = 2;
                temp_s0->field_890 = 0;
                temp_s0->field_88C = 0;
            }
            break;
        case ITEM_WPPK:
        case ITEM_WPPKSIL:
        case ITEM_TT33:
        case ITEM_SKORPION:
        case ITEM_AK47:
        case ITEM_UZI:
        case ITEM_MP5K:
        case ITEM_MP5KSIL:
        case ITEM_SPECTRE:
        case ITEM_M16:
        case ITEM_FNP90:
        case ITEM_SHOTGUN:
        case ITEM_AUTOSHOT:
        case ITEM_SNIPERRIFLE:
        case ITEM_GOLDENGUN:
        case ITEM_SILVERWPPK:
        case ITEM_GOLDWPPK:
        case ITEM_LASER:
        case ITEM_WATCHLASER:
        case ITEM_ROCKETLAUNCH:
        case ITEM_TRIGGER:
        case ITEM_TANKSHELLS:
        case ITEM_FLAREPISTOL:
        case ITEM_PITONGUN:
        case ITEM_WATCHMAGNETATTRACT:
            temp_s0->when_detonating_mines_is_0 = 2;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        case ITEM_TIMEDMINE:
        case ITEM_PROXIMITYMINE:
        case ITEM_REMOTEMINE:
        case ITEM_BOMBCASE:
        case ITEM_PLASTIQUE:
        case ITEM_BUG:
        case ITEM_MICROCAMERA:
        case ITEM_GOLDENEYEKEY:
            temp_s0->when_detonating_mines_is_0 = 0x1C;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        case ITEM_KNIFE:
            if (!(randomGetNext() & 1))
            {
                temp_s0->when_detonating_mines_is_0 = 0x11;
            }
            else
            {
                temp_s0->when_detonating_mines_is_0 = 0x14;
            }
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        case ITEM_GRENADE:
            if ((temp_s0->field_888 != 0) || (temp_s0->field_890 >= WHEN_1_CASE_GRENADE_FLD890))
            {
                g_CurrentPlayer->last_z_trigger_timer = temp_s0->field_890;
                temp_s0->when_detonating_mines_is_0 = 0x1A;
                temp_s0->field_88C = 0;
                temp_s0->field_890 = 0;
            }
            break;
        case ITEM_FIST:
            if (!(randomGetNext() & 1))
            {
                temp_s0->when_detonating_mines_is_0 = 0x1E;
            }
            else
            {
                temp_s0->when_detonating_mines_is_0 = 0x20;
            }
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        case ITEM_THROWKNIFE:
            temp_s0->when_detonating_mines_is_0 = 0x17;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        case ITEM_TASER:
            tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
            if (sub_GAME_7F05C6FC(taserFireKeyFrames, tempf, &temp_s0->field_8EC, arg0) != 0)
            {
                temp_s0->field_92C = 1;
            }
            else
            {
                temp_s0->when_detonating_mines_is_0 = 2;
                temp_s0->field_890 = 0;
                temp_s0->field_88C = 0;
            }
            break;
        case ITEM_BUNGEE:
        case ITEM_DOORDECODER:
        case ITEM_BOMBDEFUSER:
        case ITEM_LOCKEXPLODER:
        case ITEM_DOOREXPLODER:
        case ITEM_WEAPONCASE:
        case ITEM_SAFECRACKERCASE:
        case ITEM_KEYANALYSERCASE:
        case ITEM_BUGDETECTOR:
        case ITEM_EXPLOSIVEFLOPPY:
        case ITEM_POLARIZEDGLASSES:
        case ITEM_DARKGLASSES:
        case ITEM_CREDITCARD:
        case ITEM_GASKEYRING:
        case ITEM_DATATHIEF:
        case ITEM_WATCHIDENTIFIER:
        case ITEM_WATCHCOMMUNICATOR:
        case ITEM_WATCHGEIGERCOUNTER:
        case ITEM_WATCHMAGNETREPEL:
        case ITEM_DATTAPE:
        case ITEM_KEYCARD:
        case ITEM_KEYYALE:
        case ITEM_KEYBOLT:
            temp_s0->when_detonating_mines_is_0 = 0x24;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        case ITEM_SUIT_LF_HAND:
        case ITEM_JOYPAD:
        case ITEM_ROCKETROUND:
        case ITEM_GRENADEROUND:
        case ITEM_TOKEN:
        default:
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
            break;
        }

        temp_s0->volley = 0;
    }

    if (temp_s0->when_detonating_mines_is_0 == 2)
    {
        if ((get_ammo_type_for_weapon(var_s1) == 0) || (temp_s0->weapon_ammo_in_magazine > 0))
        {
            switch (var_s1)
            {
            case ITEM_CAMERA:
            case ITEM_WATCHMAGNETATTRACT:
                if (temp_s0->field_88C == 0)
                {
                    temp_s0->weapon_firing_status = (lvlGetControlsLockedFlag() == 0) && (g_CurrentPlayer->mpmenuon == 0);
                }
                else
                {
                    temp_s0->when_detonating_mines_is_0 = 3;
                    temp_s0->field_890 = 0;
                    temp_s0->field_88C = 0;
                }
                break;
            case ITEM_WPPK:
            case ITEM_WPPKSIL:
            case ITEM_TT33:
            case ITEM_SHOTGUN:
            case ITEM_AUTOSHOT:
            case ITEM_SNIPERRIFLE:
            case ITEM_RUGER:
            case ITEM_GOLDENGUN:
            case ITEM_SILVERWPPK:
            case ITEM_GOLDWPPK:
            case ITEM_LASER:
            case ITEM_WATCHLASER:
            case ITEM_GRENADELAUNCH:
            case ITEM_ROCKETLAUNCH:
            case ITEM_TRIGGER:
            case ITEM_TANKSHELLS:
            case ITEM_FLAREPISTOL:
            case ITEM_PITONGUN:
                if (temp_s0->field_88C == 0)
                {
                    if ((getPlayerCount() == 1) || ((checkGamePaused() == 0) && (g_CurrentPlayer->mpmenuon == 0)))
                    {
                        temp_s0->field_87D = 1;
                    }

                    temp_s0->weapon_firing_status = (lvlGetControlsLockedFlag() == 0) && (g_CurrentPlayer->mpmenuon == 0);

                    sub_GAME_7F05E808(arg0);
                }
                else
                {
                    temp_s0->when_detonating_mines_is_0 = 3;
                    temp_s0->field_890 = 0;
                    temp_s0->field_88C = 0;
                }
                break;
            case ITEM_SKORPION:
            case ITEM_AK47:
            case ITEM_UZI:
            case ITEM_MP5K:
            case ITEM_MP5KSIL:
            case ITEM_SPECTRE:
            case ITEM_M16:
            case ITEM_FNP90:
                if ((temp_s0->field_88C == 0)
                    || (temp_s0->weapon_hold_time != 0)
                    || ((bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_BURST_FIRE) != 0)
                        && (get_BONDdata_is_aiming() == 0)
                        && (((s32) temp_s0->volley % 3) != 0)))
                {
                    if (((s32) temp_s0->field_88C % bondwalkItemGetAutomaticFiringRate(var_s1)) == 0)
                    {
                        if ((getPlayerCount() == 1) || ((checkGamePaused() == 0) && (g_CurrentPlayer->mpmenuon == 0)))
                        {
                            temp_s0->field_87D = 1;
                        }

                        temp_s0->weapon_firing_status = (lvlGetControlsLockedFlag() == 0)
                            && (g_CurrentPlayer->mpmenuon == 0);
                    }
                }
                else
                {
                    temp_s0->when_detonating_mines_is_0 = 3;
                    temp_s0->field_890 = 0;
                    temp_s0->field_88C = 0;
                }
                break;
            case ITEM_KNIFE:
                if ((temp_s0->field_88C == 0) || (temp_s0->weapon_hold_time != 0))
                {
                    temp_s0->weapon_firing_status = 0;
                    temp_s0->field_87D = temp_s0->weapon_firing_status;
                }
                else
                {
                    temp_s0->when_detonating_mines_is_0 = 3;
                    temp_s0->field_890 = 0;
                    temp_s0->field_88C = 0;
                }
                break;
            case ITEM_TASER:
                if ((temp_s0->field_88C == 0) || (temp_s0->weapon_hold_time != 0))
                {
                    sub_GAME_7F05C6FC(taserRaiseKeyframes, 0.0f, &temp_s0->field_8EC, arg0);

                    temp_s0->weapon_firing_status = 0;
                    temp_s0->field_92C = 1;
                    temp_s0->field_87D = temp_s0->weapon_firing_status;

                    if (temp_s0->field_88C == 0)
                    {
                        temp_s0->weapon_firing_status = (lvlGetControlsLockedFlag() == 0)
                            && (g_CurrentPlayer->mpmenuon == 0);
                    }
                }
                else
                {
                    temp_s0->when_detonating_mines_is_0 = 3;
                    temp_s0->field_890 = 0;
                    temp_s0->field_88C = 0;
                }
                break;
            }

            if (temp_s0->weapon_firing_status != 0)
            {
                if (var_s1 != ITEM_CAMERA)
                {
                    joyRumblePakStart(get_cur_playernum(), 0.1f);

                    if (cur_player_get_control_type() >= 4)
                    {
                        joyRumblePakStart(get_cur_playernum() + getPlayerCount(), 0.1f);
                    }
                }

                temp_s0->weapon_ammo_in_magazine -= 1;
                temp_s0->volley += 1;
            }

            if (temp_s0->when_detonating_mines_is_0 == 2)
            {
                sp1B4 = 0;

                if (bondwalkItemGetSoundTriggerRate(var_s1) > 0)
                {
                    if ((g_CurrentPlayer->hands[1 - arg0].field_A50 != g_GlobalTimer)
                        && (temp_s0->field_A4C < g_GlobalTimer))
                    {
                        temp_s0->field_A4C = bondwalkItemGetSoundTriggerRate(var_s1) + g_GlobalTimer;
                        sp1B4 = 1;
                    }
                }
                else if (temp_s0->weapon_firing_status != 0)
                {
                    sp1B4 = 1;
                }

                if ((getPlayerCount() == 1) || ((checkGamePaused() == 0) && (g_CurrentPlayer->mpmenuon == 0)))
                {
                    if (sp1B4 != 0)
                    {
                        if ((temp_s0->audioHandle != NULL) && (sndGetPlayingState(temp_s0->audioHandle) != 0))
                        {
                            sndDeactivate(temp_s0->audioHandle);
                        }

                        if (((struct ALSoundState *)temp_s0->field_A48 != 0)
                            && (sndGetPlayingState((struct ALSoundState *) temp_s0->field_A48) != 0))
                        {
                            sndDeactivate((struct ALSoundState *) temp_s0->field_A48);
                        }

                        if (bondwalkItemGetSound(var_s1) != 0)
                        {
                            if (temp_s0->audioHandle == NULL)
                            {
                                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, bondwalkItemGetSound(var_s1), (struct ALSoundState *) &temp_s0->audioHandle);
                            }
                            else if ((struct ALSoundState *)temp_s0->field_A48 == 0)
                            {
                                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, bondwalkItemGetSound(var_s1), (struct ALSoundState *) &temp_s0->field_A48);
                            }

                            temp_s0->field_A50 = g_GlobalTimer;
                        }
                    }

                    if (var_s1 == ITEM_WATCHLASER)
                    {
                        sp1B0 = D_80035E90;
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, sp1B0.half[randomGetNext() & 1], NULL);
                    }
                }
            }
        }
        else if (temp_s0->field_88C > 0)
        {
            temp_s0->when_detonating_mines_is_0 = 3;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0xD;
            temp_s0->field_890 = 0;
            temp_s0->field_88C = 0;

            if ((getPlayerCount() == 1)
#if defined(VERSION_JP) || defined(VERSION_EU)
                || ((checkGamePaused() == 0) && (g_CurrentPlayer->mpmenuon == 0))
#else
                || (checkGamePaused() == 0)
#endif
               )
            {
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, EMPTY_GUN_FIRE_SFX, NULL);
            }
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 3)
    {
        if (var_s1 == ITEM_TASER)
        {
            tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
            if (sub_GAME_7F05C6FC(taserRaiseKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
            {
                temp_s0->field_92C = 1;
            }
            else
            {
                temp_s0->when_detonating_mines_is_0 = 0;
                temp_s0->field_890 = 0.0f;
                temp_s0->field_88C = 0;
            }
        }
        else
        {
            weapon_stats = get_ptr_item_statistics(var_s1);

#if defined(VERSION_US)
            sp1A4 = weapon_stats->b44[0];
            sp1A0 = weapon_stats->b44[1];
#endif
#if defined(VERSION_JP)
            sp1A4 = weapon_stats->b44[0];
            sp1A0 = weapon_stats->b44[1];
            stat_2 = weapon_stats->b44[2];
            stat_3 = weapon_stats->b44[3];
            stat_4 = weapon_stats->SingleFiringRate;
#endif
#if defined(VERSION_EU)
            sp1A4 = ((s32)weapon_stats->b44[0] * 50) / 60;
            sp1A0 = ((s32)weapon_stats->b44[1] * 50) / 60;
            stat_2 = ((s32)weapon_stats->b44[2] * 50) / 60;
            stat_3 = ((s32)weapon_stats->b44[3] * 50) / 60;
            stat_4 = weapon_stats->SingleFiringRate * 50 / 60;
#endif

            if ((
                    (temp_s0->field_888 != 0)
                    && (temp_s0->field_890 >= (sp1A4 + sp1A0))
                )
                ||
                (
                    ((weapon_stats->SingleFiringRate >= 0))
                    && (temp_s0->field_888 == 0)
#if defined(VERSION_US)
                    && (temp_s0->field_890 >= (sp1A4 + sp1A0 + weapon_stats->SingleFiringRate))
#endif
#if defined(VERSION_JP) ||  defined(VERSION_EU)
                    && (temp_s0->field_890 >= (sp1A4 + sp1A0 + stat_4))
#endif
                )
               )
            {
                temp_s0->when_detonating_mines_is_0 = 0;
                temp_s0->field_890 = 0.0f;
                temp_s0->field_88C = 0;
            }
            else if (
                (temp_s0->field_888 != 0)
                && (temp_s0->weapon_hold_time != 0)

#if defined(VERSION_US)
                && (temp_s0->field_890 >= weapon_stats->b44[2])
#endif
#if defined(VERSION_JP) ||  defined(VERSION_EU)
                && (temp_s0->field_890 >= stat_2)
#endif

                && (weapon_stats->b44[3] >= 0)

#if defined(VERSION_US)
                // HACK: registers are swapped
                // addu a1, v1, a0
                && (temp_s0->field_890 + weapon_stats->b44[3] < (0,sp1A4) + sp1A0)
                && (temp_s0->field_890 + weapon_stats->b44[3] >= (s32)weapon_stats->b44[2])
#endif
#if defined(VERSION_JP) ||  defined(VERSION_EU)
                && (temp_s0->field_890 + stat_3 < sp1A4 + sp1A0)
                && (temp_s0->field_890 + stat_3 >= (s32)stat_2)
#endif
            )
            {
                temp_s0->when_detonating_mines_is_0 = 4;
                temp_s0->field_890 = 0;
                temp_s0->field_88C = 0;
#if defined(VERSION_US)
                temp_s0->field_8A8 = weapon_stats->b44[3];
#endif
#if defined(VERSION_JP) ||  defined(VERSION_EU)
                temp_s0->field_8A8 = stat_3;
#endif
            }
            else if (temp_s0->field_890 < sp1A4 + sp1A0)
            {
                sp198 = weapon_stats->RecoilBack;
                sp19C = weapon_stats->RecoilUp;

                if (temp_s0->field_890 == 0)
                {
                    temp_s0->field_8C8 = temp_s0->field_8E8;
                    temp_s0->field_8BC = temp_s0->field_8DC;
                    temp_s0->field_8C0 = temp_s0->field_8E0;
                    temp_s0->field_8C4 = temp_s0->field_8E4;
                }

                if (temp_s0->field_890 < sp1A4)
                {
                    temp_s0->field_8D8 = M_TAU_F - ((sp19C * M_TAU_F) / 360.0f);

                    temp_s0->field_8CC = ((gunSetHorizontalOffset(arg0) - temp_s0->field_A38) * sp198) / 1000.0f;
                    temp_s0->field_8D0 = 0;
                    temp_s0->field_8D4 = ((weapon_stats->PosZ - temp_s0->field_A40) * sp198) / 1000.0f;

                    sp190 = sinf(((f32) temp_s0->field_890 * M_PI_2F) / (f32) sp1A4);
                }
                else
                {
                    temp_s0->field_8D8 = M_TAU_F - ((sp19C * M_TAU_F) / 360.0f);

                    temp_s0->field_8CC = ((gunSetHorizontalOffset(arg0) - temp_s0->field_A38) * sp198) / 1000.0f;
                    temp_s0->field_8D0 = 0;
                    temp_s0->field_8D4 = ((weapon_stats->PosZ - temp_s0->field_A40) * sp198) / 1000.0f;

                    sp190 = (cosf(((f32) (temp_s0->field_890 - sp1A4) * M_PI_F) / (f32) sp1A0) * 0.5f) + 0.5f;
                }

                temp_f0_2 = sub_GAME_7F06D0CC(temp_s0->field_8C8, temp_s0->field_8D8, sp190);

                temp_s0->field_8E8 = temp_f0_2;
                temp_s0->field_92C = 1;
                temp_s0->field_8DC = ((temp_s0->field_8CC - temp_s0->field_8BC) * sp190) + temp_s0->field_8BC;
                temp_s0->field_8E0 = ((temp_s0->field_8D0 - temp_s0->field_8C0) * sp190) + temp_s0->field_8C0;
                temp_s0->field_8E4 = ((temp_s0->field_8D4 - temp_s0->field_8C4) * sp190) + temp_s0->field_8C4;

                matrix_4x4_set_rotation_around_x(temp_f0_2, &temp_s0->field_8EC);
                matrix_4x4_set_position((struct coord3d *)&temp_s0->field_8DC, &temp_s0->field_8EC);
            }
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 4)
    {
        if (temp_s0->field_890 == 0)
        {
            temp_s0->field_8C8 = temp_s0->field_8E8;
            temp_s0->field_8BC = temp_s0->field_8DC;
            temp_s0->field_8C0 = temp_s0->field_8E0;
            temp_s0->field_8C4 = temp_s0->field_8E4;
            temp_s0->field_8D8 = 0.0f;
            temp_s0->field_8CC = 0.0f;
            temp_s0->field_8D0 = 0.0f;
            temp_s0->field_8D4 = 0.0f;
        }

        if (temp_s0->field_890 < temp_s0->field_8A8)
        {
            sp18C = (cosf(((f32) (temp_s0->field_8A8 - temp_s0->field_890) * M_PI_2F) / (f32) temp_s0->field_8A8) * 0.5f) + 0.5f;

            temp_f0_2 = sub_GAME_7F06D0CC(temp_s0->field_8C8, temp_s0->field_8D8, sp18C);

            temp_s0->field_8E8 = temp_f0_2;
            temp_s0->field_92C = 1;
            temp_s0->field_8DC = ((temp_s0->field_8CC - temp_s0->field_8BC) * sp18C) + temp_s0->field_8BC;
            temp_s0->field_8E0 = ((temp_s0->field_8D0 - temp_s0->field_8C0) * sp18C) + temp_s0->field_8C0;
            temp_s0->field_8E4 = ((temp_s0->field_8D4 - temp_s0->field_8C4) * sp18C) + temp_s0->field_8C4;

            matrix_4x4_set_rotation_around_x(temp_f0_2, &temp_s0->field_8EC);
            matrix_4x4_set_position((struct coord3d *)&temp_s0->field_8DC, &temp_s0->field_8EC);
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0xD)
    {
        if (temp_s0->field_88C == 0)
        {
            sub_GAME_7F05E808(arg0);
        }

        if ((temp_s0->field_888 != 0) || ((temp_s0->field_888 == 0) && (temp_s0->field_890 >= WHEN_D_FLD890)))
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    sp188 = WHEN_5_SP188_INIT;
    if (temp_s0->when_detonating_mines_is_0 == 5)
    {
        if (getPlayerCount() >= 2)
        {
            sp188 = WHEN_5_SP188_MULTI;
        }

        if (temp_s0->field_88C == 0)
        {
            if (getPlayerCount() == 1)
            {
                temp_s0->field_8B0 = WHEN_5_FLD8B0_SP;
            }
            else
            {
                temp_s0->field_8B0 = WHEN_5_FLD8B0_MULTI;
            }
        }

        if (temp_s0->field_890 >= sp188)
        {
            g_CurrentPlayer->ammoheldarr[get_ammo_type_for_weapon(var_s1)] += temp_s0->weapon_ammo_in_magazine;
            temp_s0->weapon_ammo_in_magazine = 0;

            if (getPlayerCount() >= 2)
            {
                sub_GAME_7F09B368(arg0);
            }

            sub_GAME_7F05FB00(arg0);

            temp_s0->when_detonating_mines_is_0 = 6;

            if (bondinvItemAvailable(ITEM_SNIPERRIFLE) != 0)
            {
                g_CurrentPlayer->cur_item_weapon_getname = ITEM_SNIPERRIFLE;
            }
            else
            {
                g_CurrentPlayer->cur_item_weapon_getname = ITEM_FIST;
            }
        }
        else
        {
            sp184 = ((f32) temp_s0->field_890 * M_LN2F) / (f32) sp188;
            temp_s0->field_92C = 1;

            matrix_4x4_set_rotation_around_x(sp184, &temp_s0->field_8EC);

            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = (1.0f - cosf(sp184)) * -60.0f;
            temp_s0->field_8EC.m[3][2] = sinf(sp184) * 15.0f;
        }
    }

    if ((temp_s0->when_detonating_mines_is_0 == 6) || (temp_s0->when_detonating_mines_is_0 == 7))
    {
        if ((temp_s0->weapon_animation_trigger == 0) || (temp_s0->field_890 >= temp_s0->field_8B0))
        {
            if (temp_s0->when_detonating_mines_is_0 == 6)
            {
                temp_v1_5 = (g_CurrentPlayer->hands - arg0) + 1;

                if ((temp_v1_5->when_detonating_mines_is_0 != 6) && (temp_v1_5->when_detonating_mines_is_0 != 5))
                {
                    if (
                        (temp_v1_5->weapon_current_animation != 5)
                        && (temp_v1_5->when_detonating_mines_is_0 != 0xE)
                        && (temp_v1_5->when_detonating_mines_is_0 != 0xF)
                        && (temp_v1_5->when_detonating_mines_is_0 != 0x10)
                        && (temp_v1_5->weapon_current_animation != 0xE))
                    {
                        if (arg0 == GUNRIGHT)
                        {
                            if (bondinvItemAvailableForHand(temp_s0->weapon_next_weapon, getCurrentPlayerWeaponId(GUNLEFT)) == 0)
                            {
                                currentPlayerEquipWeaponWrapper(GUNLEFT, 0);
                            }
                        }
                        else if (bondinvItemAvailableForHand(getCurrentPlayerWeaponId(GUNRIGHT), temp_s0->weapon_next_weapon) == 0)
                        {
                            temp_s0->weapon_next_weapon = ITEM_UNARMED;
                        }
                    }
                }
                currentPlayerUnEquipWeaponWrapper(arg0, temp_s0->weapon_next_weapon);
                var_s1 = get_item_in_hand_or_watch_menu(arg0);
                temp_s0->when_detonating_mines_is_0 = 7;
            }
            else if (Gun_hand_without_item(arg0) != 0)
            {
                temp_s0->when_detonating_mines_is_0 = 8;
                temp_s0->field_890 = 0.0f;
                temp_s0->field_88C = 0;
            }
        }

        if ((temp_s0->when_detonating_mines_is_0 == 6) || (temp_s0->when_detonating_mines_is_0 == 7))
        {
            temp_s0->field_92C = 1;
            matrix_4x4_set_rotation_around_x(M_LN2F, &temp_s0->field_8EC);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = (1.0f - cosf(M_LN2F)) * -60.0f;
            temp_s0->field_8EC.m[3][2] = sinf(M_LN2F) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 8)
    {
        sp178 = WHEN_8_SP178_INIT;

        if (getPlayerCount() >= 2)
        {
            sp178 = WHEN_8_SP178_MULTI;
        }

        if (temp_s0->field_88C == 0)
        {
            if (getPlayerCount() >= 2)
            {
                sub_GAME_7F09B398(arg0);
            }

            sub_GAME_7F0649D8(arg0);

            g_CurrentPlayer->trigger_released = 0;

            if ((g_ClockTimer > 0)
                && (g_CurrentPlayer->unknown != 1)
                && (Gun_hand_without_item(arg0) != 0)
                && (g_PlayerInvincible == 0)
#if defined(VERSION_JP) || defined(VERSION_EU)
                && (g_CurrentPlayer->bonddead == 0)
#endif
               )
            {
                switch (var_s1)
                {
                    case ITEM_LASER:
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, PICKUP_LASER_SFX, NULL);
                        break;

                    case ITEM_KNIFE:
                    case ITEM_THROWKNIFE:
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, PICKUP_KNIFE_SFX, NULL);
                        break;

                    case ITEM_TIMEDMINE:
                    case ITEM_PROXIMITYMINE:
                    case ITEM_REMOTEMINE:
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, PICKUP_MINE_SFX, NULL);
                        break;

                    case ITEM_UNARMED:
                    case ITEM_FIST:
                    case ITEM_WATCHLASER:
                    case ITEM_GRENADE:
                    case ITEM_TRIGGER:
                    case ITEM_TASER:
                    case ITEM_TANKSHELLS:
                    case ITEM_BOMBCASE:
                    case ITEM_PLASTIQUE:
                    case ITEM_CAMERA:
                    case ITEM_BUG:
                    case ITEM_MICROCAMERA:
                    case ITEM_WATCHMAGNETATTRACT:
                    case ITEM_GOLDENEYEKEY:
                    case ITEM_TOKEN:
                        break;

                    default:
                        sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, PICKUP_GUN_SFX, NULL);
                        break;
                }
            }
        }

        if ((temp_s0->field_890 >= sp178)
            || (get_ptr_weapon_model_header_line(var_s1) == NULL)
            || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_SHOW_FIRST_PERSON) == 0)
            || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_HIDE_FIRST_PERSON_HAND) != 0))
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            sp174 = ((f32) (sp178 - temp_s0->field_890) * M_LN2F) / (f32) sp178;
            temp_s0->field_92C = 1;
            matrix_4x4_set_rotation_around_x(sp174, &temp_s0->field_8EC);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = (1.0f - cosf(sp174)) * -60.0f;
            temp_s0->field_8EC.m[3][2] = sinf(sp174) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 9)
    {
        if (((temp_s0->weapon_ammo_in_magazine < get_ptr_item_statistics(var_s1)->MagSize)
             || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_AMMO_CLIP_LIMIT) != 0))
            && ((get_ammo_in_hands_weapon(arg0) > 0)))
        {
            temp_s0->when_detonating_mines_is_0 = 0xA;
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0xA)
    {
        if ((temp_s0->field_890 >= WHEN_A_FLD890) || (temp_s0->field_87F == 0))
        {
            temp_s0->when_detonating_mines_is_0 = 0xB;
            temp_s0->field_8B0 = WHEN_A_FLD8B0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            sp128 = ((f32) temp_s0->field_890 * M_LN2F) / un_f32_div_1;
            temp_s0->field_92C = 1;

            if (arg0 == GUNRIGHT)
            {
                matrix_4x4_set_rotation_around_z((un_f32_num / un_f32_div_1), &temp_s0->field_8EC);
            }
            else
            {
                matrix_4x4_set_rotation_around_z(-(un_f32_num / un_f32_div_1), &temp_s0->field_8EC);
            }

            matrix_4x4_set_rotation_around_x(sp128, &sp12C);
            matrix_4x4_multiply_in_place(&sp12C, &temp_s0->field_8EC);
            sinf((un_f32_num / un_f32_div_1));
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = sub_GAME_7F0649AC(var_s1) * (1.0f - cosf(sp128));
            temp_s0->field_8EC.m[3][2] = sinf(sp128) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0xB)
    {
        if ((temp_s0->field_88C == 0)
#if defined(VERSION_JP) || defined(VERSION_EU)
            && (g_ClockTimer > 0)
#endif
            && (g_CurrentPlayer->unknown != 1)
            && (Gun_hand_without_item(arg0) != 0)
            && (g_PlayerInvincible == 0)
#if defined(VERSION_JP) || defined(VERSION_EU)
            && (g_CurrentPlayer->bonddead == 0)
#endif
           )
        {
            switch (var_s1)
            {
            case ITEM_UNARMED:
            case ITEM_FIST:
            case ITEM_KNIFE:
            case ITEM_THROWKNIFE:
            case ITEM_LASER:
            case ITEM_WATCHLASER:
            case ITEM_GRENADE:
            case ITEM_TIMEDMINE:
            case ITEM_PROXIMITYMINE:
            case ITEM_REMOTEMINE:
            case ITEM_TRIGGER:
            case ITEM_TASER:
            case ITEM_TANKSHELLS:
            case ITEM_BOMBCASE:
            case ITEM_PLASTIQUE:
            case ITEM_CAMERA:
            case ITEM_BUG:
            case ITEM_MICROCAMERA:
            case ITEM_WATCHMAGNETATTRACT:
            case ITEM_GOLDENEYEKEY:
            case ITEM_TOKEN:
                break;
            default:
            case ITEM_WPPK:
            case ITEM_WPPKSIL:
            case ITEM_TT33:
            case ITEM_SKORPION:
            case ITEM_AK47:
            case ITEM_UZI:
            case ITEM_MP5K:
            case ITEM_MP5KSIL:
            case ITEM_SPECTRE:
            case ITEM_M16:
            case ITEM_FNP90:
            case ITEM_SHOTGUN:
            case ITEM_AUTOSHOT:
            case ITEM_SNIPERRIFLE:
            case ITEM_RUGER:
            case ITEM_GOLDENGUN:
            case ITEM_SILVERWPPK:
            case ITEM_GOLDWPPK:
            case ITEM_GRENADELAUNCH:
            case ITEM_ROCKETLAUNCH:
            case ITEM_FLAREPISTOL:
            case ITEM_PITONGUN:
            case ITEM_BUNGEE:
            case ITEM_DOORDECODER:
            case ITEM_BOMBDEFUSER:
            case ITEM_LOCKEXPLODER:
            case ITEM_DOOREXPLODER:
            case ITEM_BRIEFCASE:
            case ITEM_WEAPONCASE:
            case ITEM_SAFECRACKERCASE:
            case ITEM_KEYANALYSERCASE:
            case ITEM_BUGDETECTOR:
            case ITEM_EXPLOSIVEFLOPPY:
            case ITEM_POLARIZEDGLASSES:
            case ITEM_DARKGLASSES:
            case ITEM_CREDITCARD:
            case ITEM_GASKEYRING:
            case ITEM_DATATHIEF:
            case ITEM_WATCHIDENTIFIER:
            case ITEM_WATCHCOMMUNICATOR:
            case ITEM_WATCHGEIGERCOUNTER:
            case ITEM_WATCHMAGNETREPEL:
                sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, GUN_RIFLECOCK_SFX, NULL);
                break;
            }
        }

        if ((temp_s0->field_890 >= temp_s0->field_8B0) && !(((temp_s0->field_88C < 2))))
        {
            temp_s0->when_detonating_mines_is_0 = 0xC;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            temp_s0->field_92C = 1;

            if (arg0 == GUNRIGHT)
            {
                matrix_4x4_set_rotation_around_z(un_f32_num, &temp_s0->field_8EC);
            }
            else
            {
                matrix_4x4_set_rotation_around_z(-un_f32_num, &temp_s0->field_8EC);
            }

            matrix_4x4_set_rotation_around_x(M_LN2F, &spE4);
            matrix_4x4_multiply_in_place(&spE4, &temp_s0->field_8EC);
            sinf(un_f32_num);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = sub_GAME_7F0649AC(var_s1) * (1.0f - cosf(M_LN2F));
            temp_s0->field_8EC.m[3][2] = sinf(M_LN2F) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0xC)
    {
        if (temp_s0->field_88C == 0)
        {
            sub_GAME_7F0649D8(arg0);
            g_CurrentPlayer->trigger_released = 0;
        }

        if ((temp_s0->field_890 >= WHEN_C_FLD890)
            || (get_ptr_weapon_model_header_line(var_s1) == NULL)
            || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_SHOW_FIRST_PERSON) == 0)
            || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_HIDE_FIRST_PERSON_HAND) != 0))
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            sp98 = ((f32) (WHEN_C_FLD890 - temp_s0->field_890) * M_LN2F) / un_f32_div_2;
            temp_s0->field_92C = 1;

            if (arg0 == GUNRIGHT)
            {
                matrix_4x4_set_rotation_around_z((un_f32_num / un_f32_div_2), &temp_s0->field_8EC);
            }
            else
            {
                matrix_4x4_set_rotation_around_z(-(un_f32_num / un_f32_div_2), &temp_s0->field_8EC);
            }

            matrix_4x4_set_rotation_around_x(sp98, &sp9C);
            matrix_4x4_multiply_in_place(&sp9C, &temp_s0->field_8EC);
            sinf(un_f32_num / un_f32_div_2);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = sub_GAME_7F0649AC(var_s1) * (1.0f - cosf(sp98));
            temp_s0->field_8EC.m[3][2] = sinf(sp98) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0xE)
    {
        if ((temp_s0->field_890 >= WHEN_E_FLD890) || (temp_s0->field_87F == 0))
        {
            temp_s0->when_detonating_mines_is_0 = 0xF;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            sp94 = ((f32) temp_s0->field_890 * M_LN2F) / un_f32_div_1;
            temp_s0->field_92C = 1;

            matrix_4x4_set_rotation_around_x(sp94, &temp_s0->field_8EC);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = (1.0f - cosf(sp94)) * -60.0f;
            temp_s0->field_8EC.m[3][2] = sinf(sp94) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0xF)
    {
        if ((temp_s0->field_88C == 0) || (Gun_hand_without_item(arg0) == 0))
        {
            sub_GAME_7F05DA8C(arg0, temp_s0->weapon_next_weapon);
            var_s1 = get_item_in_hand_or_watch_menu(arg0);
        }

        if (Gun_hand_without_item(arg0) != 0)
        {
            temp_s0->when_detonating_mines_is_0 = 0x10;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            temp_s0->field_92C = 1;
            matrix_4x4_set_rotation_around_x(M_LN2F, &temp_s0->field_8EC);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = (1.0f - cosf(M_LN2F)) * -60.0f;
            temp_s0->field_8EC.m[3][2] = sinf(M_LN2F) * 15.0f;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x10)
    {
        if ((temp_s0->field_88C == 0) && (var_s1 < 0x21))
        {
            if (getPlayerCount() >= 2)
            {
                sub_GAME_7F09B398(arg0);
            }
            sub_GAME_7F0649D8(arg0);
            g_CurrentPlayer->trigger_released = 0;
        }

        if ((temp_s0->field_890 >= WHEN_10_FLD890)
            || (get_ptr_weapon_model_header_line(var_s1) == NULL)
            || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_SHOW_FIRST_PERSON) == 0)
            || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_HIDE_FIRST_PERSON_HAND) != 0))
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
        else
        {
            sp8C = ((f32) (WHEN_10_FLD890 - temp_s0->field_890) * M_LN2F) / un_f32_div_2;
            temp_s0->field_92C = 1;
            matrix_4x4_set_rotation_around_x(sp8C, &temp_s0->field_8EC);
            temp_s0->field_8EC.m[3][0] = 0.0f;
            temp_s0->field_8EC.m[3][1] = (1.0f - cosf(sp8C)) * -60.0f;
            temp_s0->field_8EC.m[3][2] = sinf(sp8C) * 15.0f;
        }
    }

    if ((temp_s0->when_detonating_mines_is_0 == 0x11)
        || (temp_s0->when_detonating_mines_is_0 == 0x12)
        || (temp_s0->when_detonating_mines_is_0 == 0x13)
        || (temp_s0->when_detonating_mines_is_0 == 0x14)
        || (temp_s0->when_detonating_mines_is_0 == 0x15)
        || (temp_s0->when_detonating_mines_is_0 == 0x16))
    {
        sp88 = F_7F05C6FC_ARG1(temp_s0->field_890);

        if (((temp_s0->when_detonating_mines_is_0 == 0x11)
                || (temp_s0->when_detonating_mines_is_0 == 0x14))
                && (temp_s0->field_890 >= WHEN_11_FLD890_1))
        {
            sp7C = D_80035E94;
            sndPlaySfx((struct ALBankAlt_s *) g_musicSfxBufferPtr, sp7C.half[randomGetNext() % 3U], NULL);


            if (temp_s0->when_detonating_mines_is_0 == 0x11)
            {
                temp_s0->when_detonating_mines_is_0 = 0x12;
                temp_s0->when_detonating_mines_is_0 = 0x12;
            }
            else
            {
                temp_s0->when_detonating_mines_is_0 = 0x15;
                temp_s0->when_detonating_mines_is_0 = 0x15;
            }
        }

        if ((temp_s0->when_detonating_mines_is_0 != 0x13)
            && (temp_s0->when_detonating_mines_is_0 != 0x16)
            && (temp_s0->field_890 >= WHEN_11_FLD890_2))
        {
            temp_s0->weapon_firing_status = 1;
            if ((temp_s0->when_detonating_mines_is_0 == 0x11) || (temp_s0->when_detonating_mines_is_0 == 0x12))
            {
                temp_s0->when_detonating_mines_is_0 = 0x13;
                temp_s0->when_detonating_mines_is_0 = 0x13;
            }
            else
            {
                temp_s0->when_detonating_mines_is_0 = 0x16;
                temp_s0->when_detonating_mines_is_0 = 0x16;
            }
        }

        if ((temp_s0->when_detonating_mines_is_0 == 0x11)
            || (temp_s0->when_detonating_mines_is_0 == 0x12)
            || (temp_s0->when_detonating_mines_is_0 == 0x13))
        {
            var_a0_2 = D_80034CA4;
        }
        else
        {
            var_a0_2 = D_80034E0C;
        }

        if (sub_GAME_7F05C6FC(var_a0_2, sp88, &temp_s0->field_8EC, arg0) != 0)
        {
            temp_s0->field_92C = 1;
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if ((temp_s0->when_detonating_mines_is_0 == 0x1E)
        || (temp_s0->when_detonating_mines_is_0 == 0x1F)
        || (temp_s0->when_detonating_mines_is_0 == 0x20)
        || (temp_s0->when_detonating_mines_is_0 == 0x21))
    {
        temp_v1_9 = F_7F05C6FC_ARG1(temp_s0->field_890);

        if ((temp_s0->when_detonating_mines_is_0 == 0x1E) || (temp_s0->when_detonating_mines_is_0 == 0x1F))
        {
            if (g_CurrentPlayer->cur_item_weapon_getname == 0x11) // Sniper Rifle
            {
                sp74 = sniperMeleeKeyframes1;
            }
            else
            {
                sp74 = fistMeleeKeyframes1;
            }

            if ((temp_s0->when_detonating_mines_is_0 != 0x1F) && (temp_s0->field_890 >= WHEN_1E_FLD890))
            {
                temp_s0->weapon_firing_status = 1;
                temp_s0->when_detonating_mines_is_0 = 0x1F;
            }
        }
        else if ((temp_s0->when_detonating_mines_is_0 == 0x20) || (temp_s0->when_detonating_mines_is_0 == 0x21))
        {
            if (g_CurrentPlayer->cur_item_weapon_getname == 0x11) // Sniper Rifle
            {
                sp74 = sniperMeleeKeyframes2;
            }
            else
            {
                sp74 = fistMeleeKeyframes2;
            }

            if ((temp_s0->when_detonating_mines_is_0 != 0x21) && (temp_s0->field_890 >= WHEN_1E_FLD890))
            {
                temp_s0->weapon_firing_status = 1;
                temp_s0->when_detonating_mines_is_0 = 0x21;
            }
        }

        if (sub_GAME_7F05C6FC(sp74, temp_v1_9, &temp_s0->field_8EC, arg0) != 0)
        {
            temp_s0->field_92C = 1;
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x1A)
    {
        if (temp_s0->weapon_ammo_in_magazine > 0)
        {
            tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
            if (sub_GAME_7F05C6FC(grenadeThrowKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
            {
                temp_s0->field_92C = 1;
            }
            else
            {
                temp_s0->field_87E = 0;
                temp_s0->weapon_firing_status = 1;
                temp_s0->weapon_ammo_in_magazine -= 1;
                temp_s0->when_detonating_mines_is_0 = 0x1B;
                temp_s0->field_890 = 0.0f;
                temp_s0->field_88C = 0;
            }
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x1B)
    {
        tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
        if (sub_GAME_7F05C6FC(timedMineThrowKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
        {
            temp_s0->field_92C = 1;
        }
        else
        {
            temp_s0->field_87E = 1;
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x17)
    {
        if (temp_s0->weapon_ammo_in_magazine > 0)
        {
            if (temp_s0->field_888 != 0)
            {
                temp_s0->when_detonating_mines_is_0 = 0x18;
            }
            else
            {
                tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
                if (sub_GAME_7F05C6FC(throwKnifeDrawBackKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
                {
                    temp_s0->field_92C = 1;
                }
                else if (sub_GAME_7F05C6FC(throwKnifeReleaseKeyframes, 0.0f, &temp_s0->field_8EC, arg0) != 0)
                {
                    temp_s0->field_92C = 1;
                }
                else
                {
                    temp_s0->when_detonating_mines_is_0 = 0x18;
                }
            }
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x18)
    {
        if (temp_s0->weapon_ammo_in_magazine > 0)
        {
            tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
            if (sub_GAME_7F05C6FC(throwKnifeDrawBackKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
            {
                temp_s0->field_92C = 1;
            }
            else
            {
                temp_s0->field_87E = 0;
                temp_s0->weapon_firing_status = 1;
                temp_s0->weapon_ammo_in_magazine -= 1;
                temp_s0->when_detonating_mines_is_0 = 0x19;
                temp_s0->field_890 = 0.0f;
                temp_s0->field_88C = 0;
            }
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x19)
    {
        tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
        if (sub_GAME_7F05C6FC(throwKnifeReleaseKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
        {
            temp_s0->field_92C = 1;
        }
        else
        {
            temp_s0->field_87E = 1;
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x1C)
    {
        if ((temp_s0->weapon_ammo_in_magazine > 0) || (bondwalkItemCheckBitflags(var_s1, WEAPONSTATBITFLAG_CLICKY) != 0))
        {
            tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
            if (sub_GAME_7F05C6FC(proxMineThrowKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
            {
                temp_s0->field_92C = 1;
            }
            else
            {
                temp_s0->field_87E = 0;
                temp_s0->weapon_firing_status = 1;
                temp_s0->weapon_ammo_in_magazine -= 1;
                temp_s0->when_detonating_mines_is_0 = 0x1D;
                temp_s0->field_890 = 0.0f;
                temp_s0->field_88C = 0;
            }
        }
        else
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x1D)
    {
        tempf = F_7F05C6FC_ARG1(temp_s0->field_890);
        if (sub_GAME_7F05C6FC(remoteMineThrowKeyframes, tempf, &temp_s0->field_8EC, arg0) != 0)
        {
            temp_s0->field_92C = 1;
        }
        else
        {
            temp_s0->field_87E = 1;
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }

    if (temp_s0->when_detonating_mines_is_0 == 0x24)
    {
        if (var_s1 == ITEM_KEYANALYSERCASE)
        {
            if (temp_s0->field_88C == 0)
            {
                analyzeGEKey();
            }
        }
        else if (var_s1 == ITEM_WEAPONCASE)
        {
            if (temp_s0->field_88C == 0)
            {
                give_weapon_case_items();
            }
        }
        else if ((var_s1 == ITEM_BOMBDEFUSER)
            || (var_s1 == ITEM_DATATHIEF)
            || (var_s1 == ITEM_DOORDECODER)
            || (var_s1 == ITEM_EXPLOSIVEFLOPPY)
            || (var_s1 == ITEM_DATTAPE))
        {
            if (temp_s0->field_88C == 0)
            {
                temp_v0_8 = propFindForInteract();

                if (temp_v0_8 != NULL)
                {
                    temp_v0_8->obj->state |= 0x40;
                }
            }
        }
        else if ((var_s1 != ITEM_POLARIZEDGLASSES)
            && (var_s1 != ITEM_DARKGLASSES)
            && (var_s1 != ITEM_WATCHGEIGERCOUNTER)
            && (var_s1 != ITEM_WATCHMAGNETREPEL)
            && (var_s1 != ITEM_KEYCARD)
            && (var_s1 != ITEM_KEYYALE)
            && (var_s1 != ITEM_KEYBOLT)
            && (var_s1 != ITEM_SAFECRACKERCASE)
            && (var_s1 != ITEM_LOCKEXPLODER)
            && (var_s1 != ITEM_DOOREXPLODER)
            && (var_s1 != ITEM_CREDITCARD)
            && (var_s1 != ITEM_GASKEYRING)
            && (var_s1 == ITEM_BUNGEE))
        {
            // removed
        }
        else if (var_s1 == ITEM_PITONGUN
            || var_s1 == ITEM_GASKEYRING
            || var_s1 == ITEM_BUNGEE)
        {
            // removed
        }

        if (temp_s0->field_888 != 0)
        {
            temp_s0->when_detonating_mines_is_0 = 0;
            temp_s0->field_890 = 0.0f;
            temp_s0->field_88C = 0;
        }
    }
}
#undef F_7F05C6FC_ARG1
#undef WHEN_1_CASE_GRENADELAUNCH_FLD890
#undef WHEN_1_CASE_GRENADE_FLD890
#undef WHEN_D_FLD890
#undef WHEN_5_SP188_INIT
#undef WHEN_5_SP188_MULTI
#undef WHEN_5_FLD8B0_SP
#undef WHEN_5_FLD8B0_MULTI
#undef WHEN_8_SP178_INIT
#undef WHEN_8_SP178_MULTI
#undef WHEN_A_FLD890
#undef WHEN_A_FLD8B0
#undef WHEN_C_FLD890
#undef WHEN_E_FLD890
#undef WHEN_10_FLD890
#undef WHEN_11_FLD890_1
#undef WHEN_11_FLD890_2
#undef WHEN_1E_FLD890




void analyzeGEKey(void)
{
    if (bondinvHasGEKey())
    {
   	    HUDMESSAGEBOTTOM(langGet(getStringID(LGUN, GUN_STR_D8_ANALYZINGTHEGOLDENEYEKEY_LF))); //Analyzing the GoldenEye key...
    	g_CurrentPlayer->copiedgoldeneye = TRUE;
    	sndPlaySfx(g_musicSfxBufferPtr, KEY_ANALYSER_SFX, 0x0);
    	currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_GOLDENEYEKEY);
    	currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
  	}
  	else
  	{
	    HUDMESSAGEBOTTOM(langGet(getStringID(LGUN, GUN_STR_D9_YOUDONOTHAVETHEGOLDENEYEKEY_LF))); //You do not have the GoldenEye key.
	    sub_GAME_7F05D690();
  	}
  	return;
}


int get_keyanalyzer_flag(void)
{
  return g_CurrentPlayer->copiedgoldeneye;
}


void give_weapon_case_items(void)
{
  add_ammo_to_inventory(AMMO_KNIFE, 2, 0, 1);
  add_ammo_to_inventory(AMMO_GRENADE, 2, 0, 1);
  bondinvAddInvItem(ITEM_SNIPERRIFLE);
  set_sound_effect_for_weapontype_collection(ITEM_SNIPERRIFLE);
  display_text_for_weapon_in_lower_left_corner(ITEM_SNIPERRIFLE);
  give_cur_player_ammo(sniperrifle_stats.AmmoType, check_cur_player_ammo_amount_in_inventory(sniperrifle_stats.AmmoType) + sniperrifle_stats.MagSize);
  bondinvRemoveItemByID(ITEM_WEAPONCASE);
  currentPlayerEquipWeaponWrapper(GUNRIGHT,ITEM_SNIPERRIFLE);
  currentPlayerEquipWeaponWrapper(GUNLEFT,ITEM_UNARMED);
}


f32 get_vertical_position_solo_watch_menu_main_page_for_item(ITEM_IDS item)
{
  return gitem_structs[item].watch_pos_x;
}


f32 get_lateral_position_solo_watch_menu_main_page_for_item(ITEM_IDS item)
{
  return gitem_structs[item].watch_pos_y;
}


f32 get_depth_on_solo_watch_menu_page_for_item(ITEM_IDS item)
{
  return gitem_structs[item].watch_pos_z;
}


f32 get_xrotation_solo_watch_menu_for_item(ITEM_IDS item)

{
  return gitem_structs[item].x_rotation;
}


f32 get_yrotation_solo_watch_menu_for_item(ITEM_IDS item)
{
  return gitem_structs[item].y_rotation;
}


f32 get_45_degree_angle(s32 unk) {
  return 45.0f;
}


u16 *get_ptr_first_title_line_item(ITEM_IDS item)
{
  return langGet(gitem_structs[item].upper_watch_text);
}


u16 *get_ptr_second_title_line_item(ITEM_IDS item)
{
    return langGet(gitem_structs[item].lower_watch_text);
}


u16 *get_ptr_short_watch_text_for_item(ITEM_IDS item)
{
    return langGet(gitem_structs[item].watch_equipment_text);
}


u16 *get_ptr_long_watch_text_for_item(ITEM_IDS item)
{
    return langGet(gitem_structs[item].weapon_of_choice_text);
}


f32 get_45_degree_angle_0(s32 unk)
{
	return 45.0f;
}


f32 get_horizontal_offset_on_solo_watch_menu_for_item(ITEM_IDS item)
{
  return gitem_structs[item].equip_watch_x;
}


f32 get_vertical_offset_on_solo_watch_menu_for_item(ITEM_IDS item)
{
  return gitem_structs[item].equip_watch_y;
}


f32 get_depth_offset_solo_watch_menu_inventory_page_for_item(ITEM_IDS item)
{
  return gitem_structs[item].equip_watch_z;
}



f32 getCurrentPlayerNoise(GUNHAND hand)
{
    return g_CurrentPlayer->hands[hand].noise;
}


void gunTickNoise(void)
{
    enum ITEM_IDS weapon_id_right;
    enum ITEM_IDS weapon_id_left;
    s32 unused2;
    f32 noise_reduction;
    WeaponStats *item_right_stats;
    WeaponStats *item_left_stats;
    f32 noise_reduction_max;
    s32 unused;

    weapon_id_right = getCurrentPlayerWeaponId(GUNRIGHT);
    weapon_id_left = getCurrentPlayerWeaponId(GUNLEFT);
    item_right_stats = get_ptr_item_statistics(weapon_id_right);
    item_left_stats = get_ptr_item_statistics(weapon_id_left);

    if (weapon_id_right != ITEM_UNARMED && get_hands_firing_status(GUNRIGHT))
    {
        g_CurrentPlayer->hands[GUNRIGHT].noise += item_right_stats->NoiseIncreasePerShot;

        if (item_right_stats->LoudnessMax < g_CurrentPlayer->hands[GUNRIGHT].noise)
        {
            g_CurrentPlayer->hands[GUNRIGHT].noise = item_right_stats->LoudnessMax;
        }
    }

    if (weapon_id_left != ITEM_UNARMED && get_hands_firing_status(GUNLEFT))
    {
        g_CurrentPlayer->hands[GUNLEFT].noise += item_left_stats->NoiseIncreasePerShot;

        if (item_left_stats->LoudnessMax < g_CurrentPlayer->hands[GUNLEFT].noise)
        {
            g_CurrentPlayer->hands[GUNLEFT].noise = item_left_stats->LoudnessMax;
        }
    }

    noise_reduction = (item_right_stats->NoiseIncreasePerShot * g_GlobalTimerDelta) / (item_right_stats->field_60 * 60.0f);
    noise_reduction_max = ((g_CurrentPlayer->hands[GUNRIGHT].noise - item_right_stats->LoudnessMin) * g_GlobalTimerDelta) / (item_right_stats->field_64 * 60.0f);

    if (noise_reduction < noise_reduction_max)
    {
        noise_reduction = noise_reduction_max;
    }

    g_CurrentPlayer->hands[GUNRIGHT].noise -= noise_reduction;

    if (g_CurrentPlayer->hands[GUNRIGHT].noise < item_right_stats->LoudnessMin)
    {
        g_CurrentPlayer->hands[GUNRIGHT].noise = item_right_stats->LoudnessMin;
    }

    noise_reduction = (item_left_stats->NoiseIncreasePerShot * g_GlobalTimerDelta) / (item_left_stats->field_60 * 60.0f);
    noise_reduction_max = ((g_CurrentPlayer->hands[GUNLEFT].noise - item_left_stats->LoudnessMin) * g_GlobalTimerDelta) / (item_left_stats->field_64 * 60.0f);

    if (noise_reduction < noise_reduction_max)
    {
        noise_reduction = noise_reduction_max;
    }

    g_CurrentPlayer->hands[GUNLEFT].noise -= noise_reduction;

    if (g_CurrentPlayer->hands[GUNLEFT].noise < item_left_stats->LoudnessMin)
    {
        g_CurrentPlayer->hands[GUNLEFT].noise = item_left_stats->LoudnessMin;
    }
}

/**
 * Returns true if the hand has a melee weapon or has ammo in the magazine.
 */
s32 gunCanUseWeapon(enum GUNHAND hand)
{
    return (get_ammo_type_for_weapon(getCurrentPlayerWeaponId(hand)) == 0)
        || (g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine > 0);
}


/**
 * US address 7F067420.
 * Perfect Dark method bgunTickGameplay.
 * 
 * Handles logic for single gun and dual wield trigger presses.
 * Calls updates to first person gun animations, gun model loading, noise to AI, and updating color from collision tiles.
 * Also handles the Watch Magnet Attract hum noise.
*/
void gunTickGameplay(s32 triggerOn)
{
    struct gun_trigger_state trigger_state;
    enum ITEM_IDS weapon_id_right;
    enum ITEM_IDS weapon_id_left;
    enum GUNHAND hand = GUNLEFT;
    struct rgba_u8 weapon_color;

    trigger_state = g_ZeroTriggerState;

    // Save previous trigger state.
    g_CurrentPlayer->prev_trigger_down = g_CurrentPlayer->trigger_down;

    // Save raw trigger state.
    g_CurrentPlayer->trigger_down = triggerOn;

    if ((g_CurrentPlayer->trigger_down == 0) && (g_CurrentPlayer->prev_trigger_down != 0))
    {
        g_CurrentPlayer->trigger_released = 1;
    }

    // Z button pressed this frame.
    if (g_CurrentPlayer->trigger_down != 0)
    {
        weapon_id_right = getCurrentPlayerWeaponId(GUNRIGHT);
        weapon_id_left = getCurrentPlayerWeaponId(GUNLEFT);

        g_CurrentPlayer->z_trigger_timer += g_ClockTimer;

        // Dual wielding.
        if ((weapon_id_right != ITEM_UNARMED) && (weapon_id_left != ITEM_UNARMED))
        {
            // Both guns prefer to take turns firing in dual wield.
            if ((bondwalkItemCheckBitflags(weapon_id_right, WEAPONSTATBITFLAG_DUAL_WIELD_ALTERNATING_FIRE) != 0) && (bondwalkItemCheckBitflags(weapon_id_left, WEAPONSTATBITFLAG_DUAL_WIELD_ALTERNATING_FIRE) != 0))
            {
                // Trigger has been held longer than 20 ticks on NTSC (24 on PAL).
                if (g_CurrentPlayer->z_trigger_timer > DUAL_WIELD_TRIGGER_SWAP_TICKS)
                {
                    // 'hand' still has its default value here, which behaves like trigger-on.
                    trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = hand;

                    // If the gun in the other hand is usable or has been held for any amount of time, depress its trigger as well.
                    if (gunCanUseWeapon(1 - g_CurrentPlayer->current_trigger_hand) || g_CurrentPlayer->hands[1 - g_CurrentPlayer->current_trigger_hand].weapon_hold_time)
                    {
                        trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 1;
                    }
                }
                // Z has been held for less than or equal to 20 ticks on NTSC (24 on PAL).
                else
                {
                    if ((g_CurrentPlayer->prev_trigger_down == 0) &&
                        ((gunCanUseWeapon(1 - g_CurrentPlayer->current_trigger_hand) != 0) || (gunCanUseWeapon(g_CurrentPlayer->current_trigger_hand) == 0)))
                    {
                        g_CurrentPlayer->current_trigger_hand = 1 - g_CurrentPlayer->current_trigger_hand;
                    }

                    trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = 1;
                    trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 0;
                }
            }
            /**
             * One gun prefers to take turns firing in dual wield.
             * This doesn't happen much in the vanilla US version with the notable
             * exception of equipping Xenia's RC-P90 and Grenade Launcher in Jungle
            */
            else if ((bondwalkItemCheckBitflags(weapon_id_right, WEAPONSTATBITFLAG_DUAL_WIELD_ALTERNATING_FIRE) != 0) || (bondwalkItemCheckBitflags(weapon_id_left, WEAPONSTATBITFLAG_DUAL_WIELD_ALTERNATING_FIRE) != 0))
            {
                // Z has been held more than 30 ticks on NTSC (36 for PAL), depress trigger on both guns.
                if (g_CurrentPlayer->z_trigger_timer > DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS)
                {
                    trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = hand;

                    if ((gunCanUseWeapon(1 - g_CurrentPlayer->current_trigger_hand) != 0) || g_CurrentPlayer->hands[1 - g_CurrentPlayer->current_trigger_hand].weapon_hold_time != 0)
                    {
                        trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 1;
                    }
                }
                // Before the hold threshold (30 ticks NTSC or 36 PAL), prefer the hand whose weapon uses alternating dual wield fire.
                else
                {
                    hand = bondwalkItemCheckBitflags(weapon_id_right, WEAPONSTATBITFLAG_DUAL_WIELD_ALTERNATING_FIRE) ? GUNRIGHT : GUNLEFT;

                    if (gunCanUseWeapon(hand) != 0 || g_CurrentPlayer->hands[hand].weapon_hold_time != 0)
                    {
                        g_CurrentPlayer->current_trigger_hand = hand;
                    }
                    else
                    {
                        if ((gunCanUseWeapon(1 - hand) != 0) || g_CurrentPlayer->hands[1 - hand].weapon_hold_time != 0)
                        {
                            g_CurrentPlayer->current_trigger_hand = 1 - hand;
                        }
                        else
                        {
                            g_CurrentPlayer->current_trigger_hand = 1 - g_CurrentPlayer->current_trigger_hand;
                        }
                    }

                    trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = 1;
                    trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 0;
                }
            }
            /**
             * Neither weapon uses alternating dual wield fire.
             * Once the hold threshold is exceeded, allow the off-hand to become active too.
             */
            else if (g_CurrentPlayer->z_trigger_timer > DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS)
            {
                trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = hand;

                if (gunCanUseWeapon(1 - g_CurrentPlayer->current_trigger_hand) || g_CurrentPlayer->hands[1 - g_CurrentPlayer->current_trigger_hand].weapon_hold_time)
                {
                    trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 1;
                }
            }
            /**
             * Neither weapon uses alternating dual wield fire.
             * On a fresh Z press, switch lead hands if the other hand is usable or the current hand cannot be used.
             * The lead hand continues being the lead hand.
             */
            else
            {
                if ((g_CurrentPlayer->prev_trigger_down == 0) &&
                    ((gunCanUseWeapon(1 - g_CurrentPlayer->current_trigger_hand) != 0) || (gunCanUseWeapon(g_CurrentPlayer->current_trigger_hand) == 0)))
                {
                    g_CurrentPlayer->current_trigger_hand = 1 - g_CurrentPlayer->current_trigger_hand;
                }

                trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = 1;
                trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 0;
            }
        }
        // Not dual wielding.
        else
        {
            if ((getCurrentPlayerWeaponId(g_CurrentPlayer->current_trigger_hand) == ITEM_UNARMED) && (getCurrentPlayerWeaponId(1 - g_CurrentPlayer->current_trigger_hand) != ITEM_UNARMED))
            {
                g_CurrentPlayer->current_trigger_hand = 1 - g_CurrentPlayer->current_trigger_hand;
            }

            trigger_state.triggerOn[g_CurrentPlayer->current_trigger_hand] = 1;
            trigger_state.triggerOn[1 - g_CurrentPlayer->current_trigger_hand] = 0;
        }
    }
    // Z button not pressed. Reset the trigger timer.
    else
    {
        g_CurrentPlayer->z_trigger_timer = 0;
    }

    handle_weapon_id_values_possibly_1st_person_animation(0, trigger_state.triggerOn[0]); // Right hand
    handle_weapon_id_values_possibly_1st_person_animation(1, trigger_state.triggerOn[1]); // Left hand
    used_to_load_1st_person_model_on_demand(0);
    used_to_load_1st_person_model_on_demand(1);
    gunTickNoise();

    if (g_CurrentPlayer->resetshadecol)
    {
        set_color_shading_from_tile(get_curplayer_positiondata(), (struct rgba_u8 *) &g_CurrentPlayer->tileColor);
        g_CurrentPlayer->resetshadecol = FALSE;
    }
    else
    {
        set_color_shading_from_tile(get_curplayer_positiondata(), &weapon_color);
        update_color_shading(&g_CurrentPlayer->tileColor, &weapon_color);
    }

    bondinvIncrementHeldTime(getCurrentPlayerWeaponId(GUNRIGHT), getCurrentPlayerWeaponId(GUNLEFT));

    if(1);

    if (g_CurrentPlayer->magnetattracttime >= 0)
    {
        struct hand *hand_right = &g_CurrentPlayer->hands[0];

        g_CurrentPlayer->magnetattracttime += g_ClockTimer;

        if (g_CurrentPlayer->magnetattracttime < WATCH_SOUND_DURATION_TICKS)
        {
            // Start or restart the hum sound if needed
            if (hand_right->audioHandle == NULL
                || sndGetPlayingState((struct ALSoundState *) hand_right->audioHandle) == 0)
            {
                if (lvlGetControlsLockedFlag() == 0)
                {
                    sndPlaySfx(
                        (struct ALBankAlt_s *) g_musicSfxBufferPtr,
                        MAGNETIC_HUM_SFX,
                        (struct ALSoundState *) &hand_right->audioHandle);
                }
            }
        }
        else
        {
            g_CurrentPlayer->magnetattracttime = -1;

            if (hand_right->audioHandle != NULL)
            {
                if (sndGetPlayingState((struct ALSoundState *) hand_right->audioHandle) != 0)
                {
                    sndDeactivate((struct ALSoundState *) hand_right->audioHandle);
                }
            }
        }
    }
}


void gunSetAimType(s32 param_1)
{
  g_CurrentPlayer->aimtype = param_1;
}


void sub_GAME_7F067AB4(coord3d *param_1)
{
  g_CurrentPlayer->hands[GUNRIGHT].field_A38 = sub_GAME_7F05DCB8(GUNRIGHT) + param_1->x;
  g_CurrentPlayer->hands[GUNRIGHT].field_A3C = param_1->y;
  g_CurrentPlayer->hands[GUNRIGHT].field_A40 = param_1->z;

  g_CurrentPlayer->hands[GUNLEFT].field_A38 = sub_GAME_7F05DCB8(GUNLEFT) + param_1->x;
  g_CurrentPlayer->hands[GUNLEFT].field_A3C = param_1->y;
  g_CurrentPlayer->hands[GUNLEFT].field_A40 = param_1->z;

}


void sub_GAME_7F067B4C(coord3d* pos)
{
    g_CurrentPlayer->hands[GUNLEFT].item_related.x = g_CurrentPlayer->hands[GUNRIGHT].item_related.x = pos->x;
    g_CurrentPlayer->hands[GUNLEFT].item_related.y = g_CurrentPlayer->hands[GUNRIGHT].item_related.y = pos->y;
    g_CurrentPlayer->hands[GUNLEFT].item_related.z = g_CurrentPlayer->hands[GUNRIGHT].item_related.z = pos->z;
}


void caclulate_gun_crosshair_position_rotation(f32 turn_x, f32 turn_y, f32 guncrossdamp, f32 gunaimdamp)
{
    s32 i;
    f32 screen_width;
    f32 screen_height;
    coord3d coords;

    screen_width = getPlayer_c_screenwidth();
    screen_height = getPlayer_c_screenheight();

    if (guncrossdamp != g_CurrentPlayer->guncrossdamp)
    {
        g_CurrentPlayer->crosshair_x_pos = (g_CurrentPlayer->crosshair_x_pos * (1.0f - g_CurrentPlayer->guncrossdamp)) / (1.0f - guncrossdamp);
        g_CurrentPlayer->crosshair_y_pos = (g_CurrentPlayer->crosshair_y_pos * (1.0f - g_CurrentPlayer->guncrossdamp)) / (1.0f - guncrossdamp);
        g_CurrentPlayer->guncrossdamp = guncrossdamp;
    }

    if (gunaimdamp != g_CurrentPlayer->gunaimdamp)
    {
        g_CurrentPlayer->gun_azimuth_angle = (g_CurrentPlayer->gun_azimuth_angle * (1.0f - g_CurrentPlayer->gunaimdamp)) / (1.0f - gunaimdamp);
        g_CurrentPlayer->gun_azimuth_turning = (g_CurrentPlayer->gun_azimuth_turning * (1.0f - g_CurrentPlayer->gunaimdamp)) / (1.0f - gunaimdamp);
        g_CurrentPlayer->gunaimdamp = gunaimdamp;
    }

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->crosshair_x_pos = (g_CurrentPlayer->crosshair_x_pos * guncrossdamp) + turn_x;
        g_CurrentPlayer->crosshair_y_pos = (g_CurrentPlayer->crosshair_y_pos * guncrossdamp) + turn_y;
    }

    g_CurrentPlayer->crosshair_angle.f[0] = (g_CurrentPlayer->crosshair_x_pos * (1.0f - guncrossdamp) * screen_width * 0.5f) + (screen_width * 0.5f);
    g_CurrentPlayer->crosshair_angle.f[1] = (g_CurrentPlayer->crosshair_y_pos * (1.0f - guncrossdamp) * screen_height * 0.5f) + (screen_height * 0.5f);

    if (g_CurrentPlayer->crosshair_angle.f[0] < 3.0f)
    {
        g_CurrentPlayer->crosshair_angle.f[0] = 3.0f;
    }
    else if ((screen_width - 4.0f) < g_CurrentPlayer->crosshair_angle.f[0])
    {
        g_CurrentPlayer->crosshair_angle.f[0] = screen_width - 4.0f;
    }

    if (g_CurrentPlayer->crosshair_angle.f[1] < 3.0f)
    {
        g_CurrentPlayer->crosshair_angle.f[1] = 3.0f;
    }
    else if ((screen_height - 4.0f) < g_CurrentPlayer->crosshair_angle.f[1])
    {
        g_CurrentPlayer->crosshair_angle.f[1] = (screen_height - 4.0f);
    }

    g_CurrentPlayer->crosshair_angle.f[0] += getPlayer_c_screenleft();
    g_CurrentPlayer->crosshair_angle.f[1] += getPlayer_c_screentop();

    for (i = 0; i < g_ClockTimer; i++)
    {
        g_CurrentPlayer->gun_azimuth_angle = (g_CurrentPlayer->gun_azimuth_angle * gunaimdamp) + turn_x;
        g_CurrentPlayer->gun_azimuth_turning = (g_CurrentPlayer->gun_azimuth_turning * gunaimdamp) + turn_y;
    }

    g_CurrentPlayer->field_FFC.x = (g_CurrentPlayer->gun_azimuth_angle * (1.0f - gunaimdamp) * screen_width * 0.5f) + (screen_width * 0.5f);
    g_CurrentPlayer->field_FFC.y = (g_CurrentPlayer->gun_azimuth_turning * (1.0f - gunaimdamp) * screen_height * 0.5f) + (screen_height * 0.5f);

    g_CurrentPlayer->field_FFC.x += getPlayer_c_screenleft();
    g_CurrentPlayer->field_FFC.y += getPlayer_c_screentop();

    transformAndNormalizeByLength2Dto3D(&g_CurrentPlayer->field_FFC, &coords, 1000.0f);
    sub_GAME_7F067AB4(&coords);
}



void sub_GAME_7F067F58(f32 turn_x, f32 turn_y, f32 max_aim_lock_speed)
{
    f32 aim_lock_speed;

#if defined(VERSION_US) || defined(VERSION_JP)
    aim_lock_speed = get_ptr_item_statistics(getCurrentPlayerWeaponId(GUNRIGHT))->AimLockSpeed;
#elif defined(VERSION_EU)
    aim_lock_speed = get_ptr_item_statistics(getCurrentPlayerWeaponId(GUNRIGHT))->CrosshairSpeed;
#endif

    if (aim_lock_speed < max_aim_lock_speed)
    {
        aim_lock_speed = max_aim_lock_speed;
    }

    caclulate_gun_crosshair_position_rotation(turn_x, turn_y, max_aim_lock_speed, aim_lock_speed);
}



void sub_GAME_7F067FBC(f32 turn_x, f32 turn_y)
{
    WeaponStats * item_stats;
    f32 guncrossdamp;
    f32 gunaimdamp;

    item_stats = get_ptr_item_statistics(getCurrentPlayerWeaponId(GUNRIGHT));

#if defined(VERSION_US)
    guncrossdamp = item_stats->CrosshairSpeed;
    gunaimdamp = item_stats->AimLockSpeed;
#elif defined(VERSION_EU)
    guncrossdamp = 0.7651f;
    gunaimdamp = item_stats->CrosshairSpeed;
#elif defined(VERSION_JP)
    guncrossdamp = 0.8f;
    gunaimdamp = item_stats->AimLockSpeed;
#endif

    caclulate_gun_crosshair_position_rotation(turn_x, turn_y, guncrossdamp, gunaimdamp);
}



/*
* Address: 0x7f068008
*/
void get_bullet_angle(f32* horizontal_angle, f32* vertical_angle) {
	*horizontal_angle = g_CurrentPlayer->crosshair_angle.f[0];
	*vertical_angle = g_CurrentPlayer->crosshair_angle.f[1];
}



void sub_GAME_7F06802C(void)
{
    coord3d coord;
    f32 tmp;

    tmp = getPlayer_c_screenleft() + (getPlayer_c_screenwidth() * 0.5f);
    g_CurrentPlayer->crosshair_angle.x = tmp;
    g_CurrentPlayer->field_FFC.x = tmp;

    tmp = getPlayer_c_screentop() + (getPlayer_c_screenheight() * 0.5f);
    g_CurrentPlayer->crosshair_angle.y = tmp;
    g_CurrentPlayer->field_FFC.y = tmp;

    transformAndNormalizeByLength2Dto3D((coord2d *) &g_CurrentPlayer->field_FFC, &coord, 1000.0f);
    sub_GAME_7F067AB4(&coord);
}



void sub_GAME_7F0680D4(coord3d * coord)
{
    coord3d tmp;

    g_CurrentPlayer->field_1010.x = coord->x;
    g_CurrentPlayer->field_1010.y = coord->y;
    g_CurrentPlayer->field_1010.z = coord->z;
    matrix_4x4_set_rotation_around_xyz(coord->f, &g_CurrentPlayer->field_101C);

    tmp.x = g_CurrentPlayer->field_101C.m[2][0] * 1000.0f;
    tmp.y = g_CurrentPlayer->field_101C.m[2][1] * 1000.0f;
    tmp.z = g_CurrentPlayer->field_101C.m[2][2] * 1000.0f;
    transform3Dto2DCoords(&tmp, (coord3d* ) &g_CurrentPlayer->crosshair_angle);

    g_CurrentPlayer->field_FFC.x = g_CurrentPlayer->crosshair_angle.x;
    g_CurrentPlayer->field_FFC.y = g_CurrentPlayer->crosshair_angle.y;

    sub_GAME_7F067AB4(&tmp);
}



/**
 * Address 0x7F068190.
*/
void sub_GAME_7F068190(coord3d *zeropos, coord3d *vec)
{
    zeropos->x = 0.0f;
    zeropos->y = 0.0f;
    zeropos->z = 0.0f;

    transformAndNormalizeByLength2Dto3D(&g_CurrentPlayer->crosshair_angle, vec, 1.0f);
}

/*
* Address: 0x7f0681cc
* This function computes the angle the player's bullets are fired at
*/
void bullet_path_from_screen_center(coord3d* arg0, coord3d* result, enum GUNHAND arg2)
{
    coord2d crosspos;
    s32 unused;
    f32 inaccuracy;
    f32 scaledspread;
    f32 randfactor;

    inaccuracy = get_ptr_item_statistics(getCurrentPlayerWeaponId(arg2))->Inaccuracy;
    if ((bondwalkItemCheckBitflags(get_item_in_hand_or_watch_menu(arg2), WEAPONSTATBITFLAG_FIRST_SHOT_ACCURACY) != 0) && (g_CurrentPlayer->hands[arg2].volley == 1))
    {
        // Single shots are four times more accurate
        inaccuracy *= 0.25f;
    }

    scaledspread = (120.0f * inaccuracy) / viGetFovY();

    randfactor = (RANDOMFRAC() - 0.5f) * RANDOMFRAC();
    crosspos.x = g_CurrentPlayer->crosshair_angle.f[0] + randfactor * scaledspread * getPlayer_c_screenwidth() * (PAL ? ASPECT_RATIO_PAL : ASPECT_RATIO_SD)
        / (getPlayer_c_perspaspect() * 320.0f);

    randfactor = (RANDOMFRAC() - 0.5f) * RANDOMFRAC();
    crosspos.y =  g_CurrentPlayer->crosshair_angle.f[1] + randfactor * scaledspread * getPlayer_c_screenheight()
        / (PAL ? (f32)(SCREEN_HEIGHT_272) : (f32)(SCREEN_HEIGHT_240));

    arg0->x = 0.0f;
    arg0->y = 0.0f;
    arg0->z = 0.0f;

    // Result is a normalized vector describing the path the bullet will follow
    // Can be used to compute x,y,z displacement off the center of the screen if done for a projectile
    transformAndNormalizeByLength2Dto3D(&crosspos, result, 1.0f);
}


/*
* Address: 0x7f068420
*/
CasingRecord* casingCreate(ModelFileHeader* header, Mtxf* mtx)
{
    CasingRecord* entry = g_Casings;
    CasingRecord* end = g_Casings + ARRAYCOUNT(g_Casings);

    while (entry < end && entry->header != NULL)
    {
        entry++;
    }

    if (entry < end)
    {
        entry->header = header;

        entry->pos.x = mtx->m[3][0];
        entry->pos.y = mtx->m[3][1];
        entry->pos.z = mtx->m[3][2];
#if VERSION_EU
        matrix_7f05842c_eu(mtx, entry->unk1C);
#else
        entry->unk1C.m[0][0] = mtx->m[0][0];
        entry->unk1C.m[0][1] = mtx->m[0][1];
        entry->unk1C.m[0][2] = mtx->m[0][2];
        entry->unk1C.m[0][3] = 0.0f;

        entry->unk1C.m[1][0] = mtx->m[1][0];
        entry->unk1C.m[1][1] = mtx->m[1][1];
        entry->unk1C.m[1][2] = mtx->m[1][2];
        entry->unk1C.m[1][3] = 0.0f;

        entry->unk1C.m[2][0] = mtx->m[2][0];
        entry->unk1C.m[2][1] = mtx->m[2][1];
        entry->unk1C.m[2][2] = mtx->m[2][2];
        entry->unk1C.m[2][3] = 0.0f;

        entry->unk1C.m[3][0] = 0.0f;
        entry->unk1C.m[3][1] = 0.0f;
        entry->unk1C.m[3][2] = 0.0f;
        entry->unk1C.m[3][3] = 1.0f;
#endif
        return entry;
    }

    return NULL;
}


#ifdef NONMATCHING
void sub_GAME_7F068508(void) {

}
#else
#ifndef VERSION_EU
GLOBAL_ASM(
.late_rodata
glabel D_800543B4
.word 0x3dccccce /*0.10000001*/
glabel D_800543B8
.word 0x3f088888 /*0.5333333*/
glabel D_800543BC
.word 0x40c90fdb /*6.2831855*/
glabel D_800543C0
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543C4
.word 0x40c90fdb /*6.2831855*/
glabel D_800543C8
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543CC
.word 0x40c90fdb /*6.2831855*/
glabel D_800543D0
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543D4
.word 0x493d6c30 /*775875.0*/
glabel expended_shell_initial_gravity_modifier_pistol
.word 0x3e8e38e4 /*0.27777779*/
glabel D_800543DC
.word 0x3fb55555 /*1.4166666*/
glabel D_800543E0
.word 0x3fd55555 /*1.6666666*/
glabel D_800543E4
.word 0x40c90fdb /*6.2831855*/
glabel D_800543E8
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543EC
.word 0x40c90fdb /*6.2831855*/
glabel D_800543F0
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543F4
.word 0x40c90fdb /*6.2831855*/
glabel D_800543F8
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543FC
.word 0x493d6c30 /*775875.0*/
glabel expended_shell_initial_gravity_modifier_non_pistol
.word 0x3e8e38e4 /*0.27777779*/
.text
glabel sub_GAME_7F068508
/* 09D038 7F068508 27BDFF40 */  addiu $sp, $sp, -0xc0
/* 09D03C 7F06850C AFBF001C */  sw    $ra, 0x1c($sp)
/* 09D040 7F068510 AFB00014 */  sw    $s0, 0x14($sp)
/* 09D044 7F068514 00808025 */  move  $s0, $a0
/* 09D048 7F068518 AFB10018 */  sw    $s1, 0x18($sp)
/* 09D04C 7F06851C 0FC17674 */  jal   getCurrentPlayerWeaponId
/* 09D050 7F068520 AFA500C4 */   sw    $a1, 0xc4($sp)
/* 09D054 7F068524 AFA20078 */  sw    $v0, 0x78($sp)
/* 09D058 7F068528 0FC1722D */  jal   get_ptr_item_statistics
/* 09D05C 7F06852C 00402025 */   move  $a0, $v0
/* 09D060 7F068530 8C430028 */  lw    $v1, 0x28($v0)
/* 09D064 7F068534 506001F6 */  beql  $v1, $zero, .L7F068D10
/* 09D068 7F068538 8FBF001C */   lw    $ra, 0x1c($sp)
/* 09D06C 7F06853C 0FC26919 */  jal   getPlayerCount
/* 09D070 7F068540 AFA30070 */   sw    $v1, 0x70($sp)
/* 09D074 7F068544 28410002 */  slti  $at, $v0, 2
/* 09D078 7F068548 102001F0 */  beqz  $at, .L7F068D0C
/* 09D07C 7F06854C 3C028008 */   lui   $v0, %hi(g_CurrentPlayer)
/* 09D080 7F068550 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D084 7F068554 00107140 */  sll   $t6, $s0, 5
/* 09D088 7F068558 001088C0 */  sll   $s1, $s0, 3
/* 09D08C 7F06855C 004E7821 */  addu  $t7, $v0, $t6
/* 09D090 7F068560 8DF80818 */  lw    $t8, 0x818($t7)
/* 09D094 7F068564 02308823 */  subu  $s1, $s1, $s0
/* 09D098 7F068568 00118880 */  sll   $s1, $s1, 2
/* 09D09C 7F06856C 8F030000 */  lw    $v1, ($t8)
/* 09D0A0 7F068570 02308821 */  addu  $s1, $s1, $s0
/* 09D0A4 7F068574 00118880 */  sll   $s1, $s1, 2
/* 09D0A8 7F068578 1060001F */  beqz  $v1, .L7F0685F8
/* 09D0AC 7F06857C 02308821 */   addu  $s1, $s1, $s0
/* 09D0B0 7F068580 8C620004 */  lw    $v0, 4($v1)
/* 09D0B4 7F068584 3C018005 */  lui   $at, %hi(D_800543B4)
/* 09D0B8 7F068588 C42043B4 */  lwc1  $f0, %lo(D_800543B4)($at)
/* 09D0BC 7F06858C C4440000 */  lwc1  $f4, ($v0)
/* 09D0C0 7F068590 27A40064 */  addiu $a0, $sp, 0x64
/* 09D0C4 7F068594 27A5007C */  addiu $a1, $sp, 0x7c
/* 09D0C8 7F068598 46002182 */  mul.s $f6, $f4, $f0
/* 09D0CC 7F06859C E7A60064 */  swc1  $f6, 0x64($sp)
/* 09D0D0 7F0685A0 C4480004 */  lwc1  $f8, 4($v0)
/* 09D0D4 7F0685A4 46004282 */  mul.s $f10, $f8, $f0
/* 09D0D8 7F0685A8 E7AA0068 */  swc1  $f10, 0x68($sp)
/* 09D0DC 7F0685AC C4520008 */  lwc1  $f18, 8($v0)
/* 09D0E0 7F0685B0 46009102 */  mul.s $f4, $f18, $f0
/* 09D0E4 7F0685B4 0FC16259 */  jal   matrix_4x4_set_identity_and_position
/* 09D0E8 7F0685B8 E7A4006C */   swc1  $f4, 0x6c($sp)
/* 09D0EC 7F0685BC 001088C0 */  sll   $s1, $s0, 3
/* 09D0F0 7F0685C0 02308823 */  subu  $s1, $s1, $s0
/* 09D0F4 7F0685C4 00118880 */  sll   $s1, $s1, 2
/* 09D0F8 7F0685C8 02308821 */  addu  $s1, $s1, $s0
/* 09D0FC 7F0685CC 3C198008 */  lui   $t9, %hi(g_CurrentPlayer)
/* 09D100 7F0685D0 8F39A0B0 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 09D104 7F0685D4 00118880 */  sll   $s1, $s1, 2
/* 09D108 7F0685D8 02308821 */  addu  $s1, $s1, $s0
/* 09D10C 7F0685DC 001188C0 */  sll   $s1, $s1, 3
/* 09D110 7F0685E0 03312021 */  addu  $a0, $t9, $s1
/* 09D114 7F0685E4 24840AD8 */  addiu $a0, $a0, 0xad8
/* 09D118 7F0685E8 0FC1601A */  jal   matrix_4x4_multiply_in_place
/* 09D11C 7F0685EC 27A5007C */   addiu $a1, $sp, 0x7c
/* 09D120 7F0685F0 10000007 */  b     .L7F068610
/* 09D124 7F0685F4 8FA40070 */   lw    $a0, 0x70($sp)
.L7F0685F8:
/* 09D128 7F0685F8 001188C0 */  sll   $s1, $s1, 3
/* 09D12C 7F0685FC 00512021 */  addu  $a0, $v0, $s1
/* 09D130 7F068600 24840AD8 */  addiu $a0, $a0, 0xad8
/* 09D134 7F068604 0FC16008 */  jal   matrix_4x4_copy
/* 09D138 7F068608 27A5007C */   addiu $a1, $sp, 0x7c
/* 09D13C 7F06860C 8FA40070 */  lw    $a0, 0x70($sp)
.L7F068610:
/* 09D140 7F068610 0FC1A108 */  jal   casingCreate
/* 09D144 7F068614 27A5007C */   addiu $a1, $sp, 0x7c
/* 09D148 7F068618 104001BC */  beqz  $v0, .L7F068D0C
/* 09D14C 7F06861C 00408025 */   move  $s0, $v0
/* 09D150 7F068620 3C098003 */  lui   $t1, %hi(D_80035EA4)
/* 09D154 7F068624 25295EA4 */  addiu $t1, %lo(D_80035EA4) # addiu $t1, $t1, 0x5ea4
/* 09D158 7F068628 8D210000 */  lw    $at, ($t1)
/* 09D15C 7F06862C 8FA30078 */  lw    $v1, 0x78($sp)
/* 09D160 7F068630 27A80054 */  addiu $t0, $sp, 0x54
/* 09D164 7F068634 AD010000 */  sw    $at, ($t0)
/* 09D168 7F068638 8D210008 */  lw    $at, 8($t1)
/* 09D16C 7F06863C 8D2B0004 */  lw    $t3, 4($t1)
/* 09D170 7F068640 AD010008 */  sw    $at, 8($t0)
/* 09D174 7F068644 AD0B0004 */  sw    $t3, 4($t0)
/* 09D178 7F068648 C7A600C4 */  lwc1  $f6, 0xc4($sp)
/* 09D17C 7F06864C 24010004 */  li    $at, 4
/* 09D180 7F068650 1061000A */  beq   $v1, $at, .L7F06867C
/* 09D184 7F068654 E4460000 */   swc1  $f6, ($v0)
/* 09D188 7F068658 24010005 */  li    $at, 5
/* 09D18C 7F06865C 10610007 */  beq   $v1, $at, .L7F06867C
/* 09D190 7F068660 24010006 */   li    $at, 6
/* 09D194 7F068664 10610005 */  beq   $v1, $at, .L7F06867C
/* 09D198 7F068668 24010014 */   li    $at, 20
/* 09D19C 7F06866C 10610003 */  beq   $v1, $at, .L7F06867C
/* 09D1A0 7F068670 24010015 */   li    $at, 21
/* 09D1A4 7F068674 146100D3 */  bne   $v1, $at, .L7F0689C4
/* 09D1A8 7F068678 00000000 */   nop
.L7F06867C:
/* 09D1AC 7F06867C 0C002914 */  jal   randomGetNext
/* 09D1B0 7F068680 00000000 */   nop
/* 09D1B4 7F068684 44824000 */  mtc1  $v0, $f8
/* 09D1B8 7F068688 3C018005 */  lui   $at, %hi(D_800543B8)
/* 09D1BC 7F06868C C42043B8 */  lwc1  $f0, %lo(D_800543B8)($at)
/* 09D1C0 7F068690 04410005 */  bgez  $v0, .L7F0686A8
/* 09D1C4 7F068694 468042A0 */   cvt.s.w $f10, $f8
/* 09D1C8 7F068698 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D1CC 7F06869C 44819000 */  mtc1  $at, $f18
/* 09D1D0 7F0686A0 00000000 */  nop
/* 09D1D4 7F0686A4 46125280 */  add.s $f10, $f10, $f18
.L7F0686A8:
/* 09D1D8 7F0686A8 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D1DC 7F0686AC 44812000 */  mtc1  $at, $f4
/* 09D1E0 7F0686B0 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D1E4 7F0686B4 44819000 */  mtc1  $at, $f18
/* 09D1E8 7F0686B8 46045182 */  mul.s $f6, $f10, $f4
/* 09D1EC 7F0686BC 00000000 */  nop
/* 09D1F0 7F0686C0 46003202 */  mul.s $f8, $f6, $f0
/* 09D1F4 7F0686C4 00000000 */  nop
/* 09D1F8 7F0686C8 46124282 */  mul.s $f10, $f8, $f18
/* 09D1FC 7F0686CC 46005100 */  add.s $f4, $f10, $f0
/* 09D200 7F0686D0 46002187 */  neg.s $f6, $f4
/* 09D204 7F0686D4 0C002914 */  jal   randomGetNext
/* 09D208 7F0686D8 E6060010 */   swc1  $f6, 0x10($s0)
/* 09D20C 7F0686DC 44824000 */  mtc1  $v0, $f8
/* 09D210 7F0686E0 3C014020 */  li    $at, 0x40200000 # 2.500000
/* 09D214 7F0686E4 44810000 */  mtc1  $at, $f0
/* 09D218 7F0686E8 04410005 */  bgez  $v0, .L7F068700
/* 09D21C 7F0686EC 468044A0 */   cvt.s.w $f18, $f8
/* 09D220 7F0686F0 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D224 7F0686F4 44815000 */  mtc1  $at, $f10
/* 09D228 7F0686F8 00000000 */  nop
/* 09D22C 7F0686FC 460A9480 */  add.s $f18, $f18, $f10
.L7F068700:
/* 09D230 7F068700 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D234 7F068704 44812000 */  mtc1  $at, $f4
/* 09D238 7F068708 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D23C 7F06870C 44815000 */  mtc1  $at, $f10
/* 09D240 7F068710 46049182 */  mul.s $f6, $f18, $f4
/* 09D244 7F068714 3C0C8008 */  lui   $t4, %hi(g_CurrentPlayer)
/* 09D248 7F068718 26050010 */  addiu $a1, $s0, 0x10
/* 09D24C 7F06871C 46003202 */  mul.s $f8, $f6, $f0
/* 09D250 7F068720 44803000 */  mtc1  $zero, $f6
/* 09D254 7F068724 00000000 */  nop
/* 09D258 7F068728 E6060018 */  swc1  $f6, 0x18($s0)
/* 09D25C 7F06872C 460A4482 */  mul.s $f18, $f8, $f10
/* 09D260 7F068730 46009100 */  add.s $f4, $f18, $f0
/* 09D264 7F068734 E6040014 */  swc1  $f4, 0x14($s0)
/* 09D268 7F068738 8D8CA0B0 */  lw    $t4, %lo(g_CurrentPlayer)($t4)
/* 09D26C 7F06873C 01912021 */  addu  $a0, $t4, $s1
/* 09D270 7F068740 0FC160F6 */  jal   mtx4RotateVecInPlace
/* 09D274 7F068744 24840AD8 */   addiu $a0, $a0, 0xad8
/* 09D278 7F068748 0C002914 */  jal   randomGetNext
/* 09D27C 7F06874C 00000000 */   nop
/* 09D280 7F068750 44824000 */  mtc1  $v0, $f8
/* 09D284 7F068754 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D288 7F068758 04410004 */  bgez  $v0, .L7F06876C
/* 09D28C 7F06875C 468042A0 */   cvt.s.w $f10, $f8
/* 09D290 7F068760 44819000 */  mtc1  $at, $f18
/* 09D294 7F068764 00000000 */  nop
/* 09D298 7F068768 46125280 */  add.s $f10, $f10, $f18
.L7F06876C:
/* 09D29C 7F06876C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D2A0 7F068770 44812000 */  mtc1  $at, $f4
/* 09D2A4 7F068774 3C018005 */  lui   $at, %hi(D_800543BC)
/* 09D2A8 7F068778 C42843BC */  lwc1  $f8, %lo(D_800543BC)($at)
/* 09D2AC 7F06877C 46045002 */  mul.s $f0, $f10, $f4
/* 09D2B0 7F068780 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D2B4 7F068784 44815000 */  mtc1  $at, $f10
/* 09D2B8 7F068788 3C018005 */  lui   $at, %hi(D_800543C0)
/* 09D2BC 7F06878C 46000180 */  add.s $f6, $f0, $f0
/* 09D2C0 7F068790 46083482 */  mul.s $f18, $f6, $f8
/* 09D2C4 7F068794 C42643C0 */  lwc1  $f6, %lo(D_800543C0)($at)
/* 09D2C8 7F068798 460A9102 */  mul.s $f4, $f18, $f10
/* 09D2CC 7F06879C 46062201 */  sub.s $f8, $f4, $f6
/* 09D2D0 7F0687A0 0C002914 */  jal   randomGetNext
/* 09D2D4 7F0687A4 E7A80054 */   swc1  $f8, 0x54($sp)
/* 09D2D8 7F0687A8 44829000 */  mtc1  $v0, $f18
/* 09D2DC 7F0687AC 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D2E0 7F0687B0 04410004 */  bgez  $v0, .L7F0687C4
/* 09D2E4 7F0687B4 468092A0 */   cvt.s.w $f10, $f18
/* 09D2E8 7F0687B8 44812000 */  mtc1  $at, $f4
/* 09D2EC 7F0687BC 00000000 */  nop
/* 09D2F0 7F0687C0 46045280 */  add.s $f10, $f10, $f4
.L7F0687C4:
/* 09D2F4 7F0687C4 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D2F8 7F0687C8 44813000 */  mtc1  $at, $f6
/* 09D2FC 7F0687CC 3C018005 */  lui   $at, %hi(D_800543C4)
/* 09D300 7F0687D0 C43243C4 */  lwc1  $f18, %lo(D_800543C4)($at)
/* 09D304 7F0687D4 46065002 */  mul.s $f0, $f10, $f6
/* 09D308 7F0687D8 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D30C 7F0687DC 44815000 */  mtc1  $at, $f10
/* 09D310 7F0687E0 3C018005 */  lui   $at, %hi(D_800543C8)
/* 09D314 7F0687E4 46000200 */  add.s $f8, $f0, $f0
/* 09D318 7F0687E8 46124102 */  mul.s $f4, $f8, $f18
/* 09D31C 7F0687EC C42843C8 */  lwc1  $f8, %lo(D_800543C8)($at)
/* 09D320 7F0687F0 460A2182 */  mul.s $f6, $f4, $f10
/* 09D324 7F0687F4 46083481 */  sub.s $f18, $f6, $f8
/* 09D328 7F0687F8 0C002914 */  jal   randomGetNext
/* 09D32C 7F0687FC E7B20058 */   swc1  $f18, 0x58($sp)
/* 09D330 7F068800 44822000 */  mtc1  $v0, $f4
/* 09D334 7F068804 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D338 7F068808 04410004 */  bgez  $v0, .L7F06881C
/* 09D33C 7F06880C 468022A0 */   cvt.s.w $f10, $f4
/* 09D340 7F068810 44813000 */  mtc1  $at, $f6
/* 09D344 7F068814 00000000 */  nop
/* 09D348 7F068818 46065280 */  add.s $f10, $f10, $f6
.L7F06881C:
/* 09D34C 7F06881C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D350 7F068820 44814000 */  mtc1  $at, $f8
/* 09D354 7F068824 3C018005 */  lui   $at, %hi(D_800543CC)
/* 09D358 7F068828 C42443CC */  lwc1  $f4, %lo(D_800543CC)($at)
/* 09D35C 7F06882C 46085002 */  mul.s $f0, $f10, $f8
/* 09D360 7F068830 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D364 7F068834 44815000 */  mtc1  $at, $f10
/* 09D368 7F068838 3C018005 */  lui   $at, %hi(D_800543D0)
/* 09D36C 7F06883C 27A40054 */  addiu $a0, $sp, 0x54
/* 09D370 7F068840 2605005C */  addiu $a1, $s0, 0x5c
/* 09D374 7F068844 46000480 */  add.s $f18, $f0, $f0
/* 09D378 7F068848 46049182 */  mul.s $f6, $f18, $f4
/* 09D37C 7F06884C C43243D0 */  lwc1  $f18, %lo(D_800543D0)($at)
/* 09D380 7F068850 460A3202 */  mul.s $f8, $f6, $f10
/* 09D384 7F068854 46124101 */  sub.s $f4, $f8, $f18
/* 09D388 7F068858 0FC161C5 */  jal   matrix_4x4_set_rotation_around_xyz
/* 09D38C 7F06885C E7A4005C */   swc1  $f4, 0x5c($sp)
/* 09D390 7F068860 0C002914 */  jal   randomGetNext
/* 09D394 7F068864 00000000 */   nop
/* 09D398 7F068868 3C030015 */  lui   $v1, (0x00158679 >> 16) # lui $v1, 0x15
/* 09D39C 7F06886C 34638679 */  ori   $v1, (0x00158679 & 0xFFFF) # ori $v1, $v1, 0x8679
/* 09D3A0 7F068870 00026E02 */  srl   $t5, $v0, 0x18
/* 09D3A4 7F068874 01A30019 */  multu $t5, $v1
/* 09D3A8 7F068878 00007012 */  mflo  $t6
/* 09D3AC 7F06887C 000E7A83 */  sra   $t7, $t6, 0xa
/* 09D3B0 7F068880 01E3C021 */  addu  $t8, $t7, $v1
/* 09D3B4 7F068884 0C002914 */  jal   randomGetNext
/* 09D3B8 7F068888 AFB8004C */   sw    $t8, 0x4c($sp)
/* 09D3BC 7F06888C 8FB9004C */  lw    $t9, 0x4c($sp)
/* 09D3C0 7F068890 C60C0014 */  lwc1  $f12, 0x14($s0)
/* 09D3C4 7F068894 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D3C8 7F068898 0059001B */  divu  $zero, $v0, $t9
/* 09D3CC 7F06889C 00005010 */  mfhi  $t2
/* 09D3D0 7F0688A0 448A3000 */  mtc1  $t2, $f6
/* 09D3D4 7F0688A4 17200002 */  bnez  $t9, .L7F0688B0
/* 09D3D8 7F0688A8 00000000 */   nop
/* 09D3DC 7F0688AC 0007000D */  break 7
.L7F0688B0:
/* 09D3E0 7F0688B0 3C048008 */  lui   $a0, %hi(g_CurrentPlayer)
/* 09D3E4 7F0688B4 05410004 */  bgez  $t2, .L7F0688C8
/* 09D3E8 7F0688B8 468032A0 */   cvt.s.w $f10, $f6
/* 09D3EC 7F0688BC 44814000 */  mtc1  $at, $f8
/* 09D3F0 7F0688C0 00000000 */  nop
/* 09D3F4 7F0688C4 46085280 */  add.s $f10, $f10, $f8
.L7F0688C8:
/* 09D3F8 7F0688C8 3C018005 */  lui   $at, %hi(D_800543D4)
/* 09D3FC 7F0688CC C43243D4 */  lwc1  $f18, %lo(D_800543D4)($at)
/* 09D400 7F0688D0 3C018005 */  lui   $at, %hi(expended_shell_initial_gravity_modifier_pistol)
/* 09D404 7F0688D4 C42443D8 */  lwc1  $f4, %lo(expended_shell_initial_gravity_modifier_pistol)($at)
/* 09D408 7F0688D8 46125003 */  div.s $f0, $f10, $f18
/* 09D40C 7F0688DC 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09D410 7F0688E0 44819000 */  mtc1  $at, $f18
/* 09D414 7F0688E4 C60E0010 */  lwc1  $f14, 0x10($s0)
/* 09D418 7F0688E8 C6100018 */  lwc1  $f16, 0x18($s0)
/* 09D41C 7F0688EC 3C088005 */  lui   $t0, %hi(g_ClockTimer)
/* 09D420 7F0688F0 2484A0B0 */  addiu $a0, %lo(g_CurrentPlayer) # addiu $a0, $a0, -0x5f50
/* 09D424 7F0688F4 46040182 */  mul.s $f6, $f0, $f4
/* 09D428 7F0688F8 46066081 */  sub.s $f2, $f12, $f6
/* 09D42C 7F0688FC C6060008 */  lwc1  $f6, 8($s0)
/* 09D430 7F068900 46026200 */  add.s $f8, $f12, $f2
/* 09D434 7F068904 E6020014 */  swc1  $f2, 0x14($s0)
/* 09D438 7F068908 46080282 */  mul.s $f10, $f0, $f8
/* 09D43C 7F06890C 00000000 */  nop
/* 09D440 7F068910 46125102 */  mul.s $f4, $f10, $f18
/* 09D444 7F068914 C60A0004 */  lwc1  $f10, 4($s0)
/* 09D448 7F068918 460E0482 */  mul.s $f18, $f0, $f14
/* 09D44C 7F06891C 46043200 */  add.s $f8, $f6, $f4
/* 09D450 7F068920 C604000C */  lwc1  $f4, 0xc($s0)
/* 09D454 7F068924 46125180 */  add.s $f6, $f10, $f18
/* 09D458 7F068928 E6080008 */  swc1  $f8, 8($s0)
/* 09D45C 7F06892C 46100202 */  mul.s $f8, $f0, $f16
/* 09D460 7F068930 E6060004 */  swc1  $f6, 4($s0)
/* 09D464 7F068934 46082280 */  add.s $f10, $f4, $f8
/* 09D468 7F068938 E60A000C */  swc1  $f10, 0xc($s0)
/* 09D46C 7F06893C 8D088374 */  lw    $t0, %lo(g_ClockTimer)($t0)
/* 09D470 7F068940 190000F2 */  blez  $t0, .L7F068D0C
/* 09D474 7F068944 00000000 */   nop
/* 09D478 7F068948 8C890000 */  lw    $t1, ($a0)
/* 09D47C 7F06894C 3C038005 */  lui   $v1, %hi(g_GlobalTimerDelta)
/* 09D480 7F068950 24638378 */  addiu $v1, %lo(g_GlobalTimerDelta) # addiu $v1, $v1, -0x7c88
/* 09D484 7F068954 01311021 */  addu  $v0, $t1, $s1
/* 09D488 7F068958 C4520B08 */  lwc1  $f18, 0xb08($v0)
/* 09D48C 7F06895C C4460B48 */  lwc1  $f6, 0xb48($v0)
/* 09D490 7F068960 C4680000 */  lwc1  $f8, ($v1)
/* 09D494 7F068964 46069101 */  sub.s $f4, $f18, $f6
/* 09D498 7F068968 46082283 */  div.s $f10, $f4, $f8
/* 09D49C 7F06896C 460A7480 */  add.s $f18, $f14, $f10
/* 09D4A0 7F068970 E6120010 */  swc1  $f18, 0x10($s0)
/* 09D4A4 7F068974 8C8B0000 */  lw    $t3, ($a0)
/* 09D4A8 7F068978 C46A0000 */  lwc1  $f10, ($v1)
/* 09D4AC 7F06897C 01711021 */  addu  $v0, $t3, $s1
/* 09D4B0 7F068980 C4460B0C */  lwc1  $f6, 0xb0c($v0)
/* 09D4B4 7F068984 C4440B4C */  lwc1  $f4, 0xb4c($v0)
/* 09D4B8 7F068988 46043201 */  sub.s $f8, $f6, $f4
/* 09D4BC 7F06898C C6060014 */  lwc1  $f6, 0x14($s0)
/* 09D4C0 7F068990 460A4483 */  div.s $f18, $f8, $f10
/* 09D4C4 7F068994 46123100 */  add.s $f4, $f6, $f18
/* 09D4C8 7F068998 E6040014 */  swc1  $f4, 0x14($s0)
/* 09D4CC 7F06899C 8C8C0000 */  lw    $t4, ($a0)
/* 09D4D0 7F0689A0 C4720000 */  lwc1  $f18, ($v1)
/* 09D4D4 7F0689A4 01911021 */  addu  $v0, $t4, $s1
/* 09D4D8 7F0689A8 C4480B10 */  lwc1  $f8, 0xb10($v0)
/* 09D4DC 7F0689AC C44A0B50 */  lwc1  $f10, 0xb50($v0)
/* 09D4E0 7F0689B0 460A4181 */  sub.s $f6, $f8, $f10
/* 09D4E4 7F0689B4 46123103 */  div.s $f4, $f6, $f18
/* 09D4E8 7F0689B8 46048200 */  add.s $f8, $f16, $f4
/* 09D4EC 7F0689BC 100000D3 */  b     .L7F068D0C
/* 09D4F0 7F0689C0 E6080018 */   swc1  $f8, 0x18($s0)
.L7F0689C4:
/* 09D4F4 7F0689C4 0C002914 */  jal   randomGetNext
/* 09D4F8 7F0689C8 00000000 */   nop
/* 09D4FC 7F0689CC 44825000 */  mtc1  $v0, $f10
/* 09D500 7F0689D0 3C018005 */  lui   $at, %hi(D_800543DC)
/* 09D504 7F0689D4 C42043DC */  lwc1  $f0, %lo(D_800543DC)($at)
/* 09D508 7F0689D8 04410005 */  bgez  $v0, .L7F0689F0
/* 09D50C 7F0689DC 468051A0 */   cvt.s.w $f6, $f10
/* 09D510 7F0689E0 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D514 7F0689E4 44819000 */  mtc1  $at, $f18
/* 09D518 7F0689E8 00000000 */  nop
/* 09D51C 7F0689EC 46123180 */  add.s $f6, $f6, $f18
.L7F0689F0:
/* 09D520 7F0689F0 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D524 7F0689F4 44812000 */  mtc1  $at, $f4
/* 09D528 7F0689F8 3C013E00 */  li    $at, 0x3E000000 # 0.125000
/* 09D52C 7F0689FC 44819000 */  mtc1  $at, $f18
/* 09D530 7F068A00 46043202 */  mul.s $f8, $f6, $f4
/* 09D534 7F068A04 00000000 */  nop
/* 09D538 7F068A08 46004282 */  mul.s $f10, $f8, $f0
/* 09D53C 7F068A0C 00000000 */  nop
/* 09D540 7F068A10 46125182 */  mul.s $f6, $f10, $f18
/* 09D544 7F068A14 46003100 */  add.s $f4, $f6, $f0
/* 09D548 7F068A18 46002207 */  neg.s $f8, $f4
/* 09D54C 7F068A1C 0C002914 */  jal   randomGetNext
/* 09D550 7F068A20 E6080010 */   swc1  $f8, 0x10($s0)
/* 09D554 7F068A24 44825000 */  mtc1  $v0, $f10
/* 09D558 7F068A28 3C018005 */  lui   $at, %hi(D_800543E0)
/* 09D55C 7F068A2C C42043E0 */  lwc1  $f0, %lo(D_800543E0)($at)
/* 09D560 7F068A30 04410005 */  bgez  $v0, .L7F068A48
/* 09D564 7F068A34 468054A0 */   cvt.s.w $f18, $f10
/* 09D568 7F068A38 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D56C 7F068A3C 44813000 */  mtc1  $at, $f6
/* 09D570 7F068A40 00000000 */  nop
/* 09D574 7F068A44 46069480 */  add.s $f18, $f18, $f6
.L7F068A48:
/* 09D578 7F068A48 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D57C 7F068A4C 44812000 */  mtc1  $at, $f4
/* 09D580 7F068A50 3C013E00 */  li    $at, 0x3E000000 # 0.125000
/* 09D584 7F068A54 44813000 */  mtc1  $at, $f6
/* 09D588 7F068A58 46049202 */  mul.s $f8, $f18, $f4
/* 09D58C 7F068A5C 3C0D8008 */  lui   $t5, %hi(g_CurrentPlayer)
/* 09D590 7F068A60 26050010 */  addiu $a1, $s0, 0x10
/* 09D594 7F068A64 46004282 */  mul.s $f10, $f8, $f0
/* 09D598 7F068A68 44804000 */  mtc1  $zero, $f8
/* 09D59C 7F068A6C 00000000 */  nop
/* 09D5A0 7F068A70 E6080018 */  swc1  $f8, 0x18($s0)
/* 09D5A4 7F068A74 46065482 */  mul.s $f18, $f10, $f6
/* 09D5A8 7F068A78 46009100 */  add.s $f4, $f18, $f0
/* 09D5AC 7F068A7C E6040014 */  swc1  $f4, 0x14($s0)
/* 09D5B0 7F068A80 8DADA0B0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 09D5B4 7F068A84 01B12021 */  addu  $a0, $t5, $s1
/* 09D5B8 7F068A88 0FC160F6 */  jal   mtx4RotateVecInPlace
/* 09D5BC 7F068A8C 24840AD8 */   addiu $a0, $a0, 0xad8
/* 09D5C0 7F068A90 0C002914 */  jal   randomGetNext
/* 09D5C4 7F068A94 00000000 */   nop
/* 09D5C8 7F068A98 44825000 */  mtc1  $v0, $f10
/* 09D5CC 7F068A9C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D5D0 7F068AA0 04410004 */  bgez  $v0, .L7F068AB4
/* 09D5D4 7F068AA4 468051A0 */   cvt.s.w $f6, $f10
/* 09D5D8 7F068AA8 44819000 */  mtc1  $at, $f18
/* 09D5DC 7F068AAC 00000000 */  nop
/* 09D5E0 7F068AB0 46123180 */  add.s $f6, $f6, $f18
.L7F068AB4:
/* 09D5E4 7F068AB4 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D5E8 7F068AB8 44812000 */  mtc1  $at, $f4
/* 09D5EC 7F068ABC 3C018005 */  lui   $at, %hi(D_800543E4)
/* 09D5F0 7F068AC0 C42A43E4 */  lwc1  $f10, %lo(D_800543E4)($at)
/* 09D5F4 7F068AC4 46043002 */  mul.s $f0, $f6, $f4
/* 09D5F8 7F068AC8 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D5FC 7F068ACC 44813000 */  mtc1  $at, $f6
/* 09D600 7F068AD0 3C018005 */  lui   $at, %hi(D_800543E8)
/* 09D604 7F068AD4 46000200 */  add.s $f8, $f0, $f0
/* 09D608 7F068AD8 460A4482 */  mul.s $f18, $f8, $f10
/* 09D60C 7F068ADC C42843E8 */  lwc1  $f8, %lo(D_800543E8)($at)
/* 09D610 7F068AE0 46069102 */  mul.s $f4, $f18, $f6
/* 09D614 7F068AE4 46082281 */  sub.s $f10, $f4, $f8
/* 09D618 7F068AE8 0C002914 */  jal   randomGetNext
/* 09D61C 7F068AEC E7AA0054 */   swc1  $f10, 0x54($sp)
/* 09D620 7F068AF0 44829000 */  mtc1  $v0, $f18
/* 09D624 7F068AF4 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D628 7F068AF8 04410004 */  bgez  $v0, .L7F068B0C
/* 09D62C 7F068AFC 468091A0 */   cvt.s.w $f6, $f18
/* 09D630 7F068B00 44812000 */  mtc1  $at, $f4
/* 09D634 7F068B04 00000000 */  nop
/* 09D638 7F068B08 46043180 */  add.s $f6, $f6, $f4
.L7F068B0C:
/* 09D63C 7F068B0C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D640 7F068B10 44814000 */  mtc1  $at, $f8
/* 09D644 7F068B14 3C018005 */  lui   $at, %hi(D_800543EC)
/* 09D648 7F068B18 C43243EC */  lwc1  $f18, %lo(D_800543EC)($at)
/* 09D64C 7F068B1C 46083002 */  mul.s $f0, $f6, $f8
/* 09D650 7F068B20 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D654 7F068B24 44813000 */  mtc1  $at, $f6
/* 09D658 7F068B28 3C018005 */  lui   $at, %hi(D_800543F0)
/* 09D65C 7F068B2C 46000280 */  add.s $f10, $f0, $f0
/* 09D660 7F068B30 46125102 */  mul.s $f4, $f10, $f18
/* 09D664 7F068B34 C42A43F0 */  lwc1  $f10, %lo(D_800543F0)($at)
/* 09D668 7F068B38 46062202 */  mul.s $f8, $f4, $f6
/* 09D66C 7F068B3C 460A4481 */  sub.s $f18, $f8, $f10
/* 09D670 7F068B40 0C002914 */  jal   randomGetNext
/* 09D674 7F068B44 E7B20058 */   swc1  $f18, 0x58($sp)
/* 09D678 7F068B48 44822000 */  mtc1  $v0, $f4
/* 09D67C 7F068B4C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D680 7F068B50 04410004 */  bgez  $v0, .L7F068B64
/* 09D684 7F068B54 468021A0 */   cvt.s.w $f6, $f4
/* 09D688 7F068B58 44814000 */  mtc1  $at, $f8
/* 09D68C 7F068B5C 00000000 */  nop
/* 09D690 7F068B60 46083180 */  add.s $f6, $f6, $f8
.L7F068B64:
/* 09D694 7F068B64 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09D698 7F068B68 44815000 */  mtc1  $at, $f10
/* 09D69C 7F068B6C 3C018005 */  lui   $at, %hi(D_800543F4)
/* 09D6A0 7F068B70 C42443F4 */  lwc1  $f4, %lo(D_800543F4)($at)
/* 09D6A4 7F068B74 460A3002 */  mul.s $f0, $f6, $f10
/* 09D6A8 7F068B78 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09D6AC 7F068B7C 44813000 */  mtc1  $at, $f6
/* 09D6B0 7F068B80 3C018005 */  lui   $at, %hi(D_800543F8)
/* 09D6B4 7F068B84 27A40054 */  addiu $a0, $sp, 0x54
/* 09D6B8 7F068B88 2605005C */  addiu $a1, $s0, 0x5c
/* 09D6BC 7F068B8C 46000480 */  add.s $f18, $f0, $f0
/* 09D6C0 7F068B90 46049202 */  mul.s $f8, $f18, $f4
/* 09D6C4 7F068B94 C43243F8 */  lwc1  $f18, %lo(D_800543F8)($at)
/* 09D6C8 7F068B98 46064282 */  mul.s $f10, $f8, $f6
/* 09D6CC 7F068B9C 46125101 */  sub.s $f4, $f10, $f18
/* 09D6D0 7F068BA0 0FC161C5 */  jal   matrix_4x4_set_rotation_around_xyz
/* 09D6D4 7F068BA4 E7A4005C */   swc1  $f4, 0x5c($sp)
/* 09D6D8 7F068BA8 0C002914 */  jal   randomGetNext
/* 09D6DC 7F068BAC 00000000 */   nop
/* 09D6E0 7F068BB0 3C030015 */  lui   $v1, (0x00158679 >> 16) # lui $v1, 0x15
/* 09D6E4 7F068BB4 34638679 */  ori   $v1, (0x00158679 & 0xFFFF) # ori $v1, $v1, 0x8679
/* 09D6E8 7F068BB8 00027602 */  srl   $t6, $v0, 0x18
/* 09D6EC 7F068BBC 01C30019 */  multu $t6, $v1
/* 09D6F0 7F068BC0 00007812 */  mflo  $t7
/* 09D6F4 7F068BC4 000FC283 */  sra   $t8, $t7, 0xa
/* 09D6F8 7F068BC8 0303C821 */  addu  $t9, $t8, $v1
/* 09D6FC 7F068BCC 0C002914 */  jal   randomGetNext
/* 09D700 7F068BD0 AFB9003C */   sw    $t9, 0x3c($sp)
/* 09D704 7F068BD4 8FAA003C */  lw    $t2, 0x3c($sp)
/* 09D708 7F068BD8 C60C0014 */  lwc1  $f12, 0x14($s0)
/* 09D70C 7F068BDC 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09D710 7F068BE0 004A001B */  divu  $zero, $v0, $t2
/* 09D714 7F068BE4 00004010 */  mfhi  $t0
/* 09D718 7F068BE8 44884000 */  mtc1  $t0, $f8
/* 09D71C 7F068BEC 15400002 */  bnez  $t2, .L7F068BF8
/* 09D720 7F068BF0 00000000 */   nop
/* 09D724 7F068BF4 0007000D */  break 7
.L7F068BF8:
/* 09D728 7F068BF8 3C0B8008 */  lui   $t3, %hi(g_CurrentPlayer)
/* 09D72C 7F068BFC 05010004 */  bgez  $t0, .L7F068C10
/* 09D730 7F068C00 468041A0 */   cvt.s.w $f6, $f8
/* 09D734 7F068C04 44815000 */  mtc1  $at, $f10
/* 09D738 7F068C08 00000000 */  nop
/* 09D73C 7F068C0C 460A3180 */  add.s $f6, $f6, $f10
.L7F068C10:
/* 09D740 7F068C10 3C018005 */  lui   $at, %hi(D_800543FC)
/* 09D744 7F068C14 C43243FC */  lwc1  $f18, %lo(D_800543FC)($at)
/* 09D748 7F068C18 3C018005 */  lui   $at, %hi(expended_shell_initial_gravity_modifier_non_pistol)
/* 09D74C 7F068C1C C4244400 */  lwc1  $f4, %lo(expended_shell_initial_gravity_modifier_non_pistol)($at)
/* 09D750 7F068C20 46123003 */  div.s $f0, $f6, $f18
/* 09D754 7F068C24 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09D758 7F068C28 44819000 */  mtc1  $at, $f18
/* 09D75C 7F068C2C C60E0010 */  lwc1  $f14, 0x10($s0)
/* 09D760 7F068C30 C6100018 */  lwc1  $f16, 0x18($s0)
/* 09D764 7F068C34 3C098005 */  lui   $t1, %hi(g_ClockTimer)
/* 09D768 7F068C38 46040202 */  mul.s $f8, $f0, $f4
/* 09D76C 7F068C3C 46086081 */  sub.s $f2, $f12, $f8
/* 09D770 7F068C40 C6080008 */  lwc1  $f8, 8($s0)
/* 09D774 7F068C44 46026280 */  add.s $f10, $f12, $f2
/* 09D778 7F068C48 E6020014 */  swc1  $f2, 0x14($s0)
/* 09D77C 7F068C4C 460A0182 */  mul.s $f6, $f0, $f10
/* 09D780 7F068C50 00000000 */  nop
/* 09D784 7F068C54 46123102 */  mul.s $f4, $f6, $f18
/* 09D788 7F068C58 C6060004 */  lwc1  $f6, 4($s0)
/* 09D78C 7F068C5C 460E0482 */  mul.s $f18, $f0, $f14
/* 09D790 7F068C60 46044280 */  add.s $f10, $f8, $f4
/* 09D794 7F068C64 C604000C */  lwc1  $f4, 0xc($s0)
/* 09D798 7F068C68 46123200 */  add.s $f8, $f6, $f18
/* 09D79C 7F068C6C E60A0008 */  swc1  $f10, 8($s0)
/* 09D7A0 7F068C70 46100282 */  mul.s $f10, $f0, $f16
/* 09D7A4 7F068C74 E6080004 */  swc1  $f8, 4($s0)
/* 09D7A8 7F068C78 460A2180 */  add.s $f6, $f4, $f10
/* 09D7AC 7F068C7C E606000C */  swc1  $f6, 0xc($s0)
/* 09D7B0 7F068C80 8D298374 */  lw    $t1, %lo(g_ClockTimer)($t1)
/* 09D7B4 7F068C84 19200021 */  blez  $t1, .L7F068D0C
/* 09D7B8 7F068C88 00000000 */   nop
/* 09D7BC 7F068C8C 8D6BA0B0 */  lw    $t3, %lo(g_CurrentPlayer)($t3)
/* 09D7C0 7F068C90 3C038005 */  lui   $v1, %hi(g_GlobalTimerDelta)
/* 09D7C4 7F068C94 24638378 */  addiu $v1, %lo(g_GlobalTimerDelta) # addiu $v1, $v1, -0x7c88
/* 09D7C8 7F068C98 01711021 */  addu  $v0, $t3, $s1
/* 09D7CC 7F068C9C C4520B08 */  lwc1  $f18, 0xb08($v0)
/* 09D7D0 7F068CA0 C4480B48 */  lwc1  $f8, 0xb48($v0)
/* 09D7D4 7F068CA4 C46A0000 */  lwc1  $f10, ($v1)
/* 09D7D8 7F068CA8 3C0C8008 */  lui   $t4, %hi(g_CurrentPlayer)
/* 09D7DC 7F068CAC 46089101 */  sub.s $f4, $f18, $f8
/* 09D7E0 7F068CB0 3C0D8008 */  lui   $t5, %hi(g_CurrentPlayer)
/* 09D7E4 7F068CB4 460A2183 */  div.s $f6, $f4, $f10
/* 09D7E8 7F068CB8 46067480 */  add.s $f18, $f14, $f6
/* 09D7EC 7F068CBC E6120010 */  swc1  $f18, 0x10($s0)
/* 09D7F0 7F068CC0 8D8CA0B0 */  lw    $t4, %lo(g_CurrentPlayer)($t4)
/* 09D7F4 7F068CC4 C4660000 */  lwc1  $f6, ($v1)
/* 09D7F8 7F068CC8 01911021 */  addu  $v0, $t4, $s1
/* 09D7FC 7F068CCC C4480B0C */  lwc1  $f8, 0xb0c($v0)
/* 09D800 7F068CD0 C4440B4C */  lwc1  $f4, 0xb4c($v0)
/* 09D804 7F068CD4 46044281 */  sub.s $f10, $f8, $f4
/* 09D808 7F068CD8 C6080014 */  lwc1  $f8, 0x14($s0)
/* 09D80C 7F068CDC 46065483 */  div.s $f18, $f10, $f6
/* 09D810 7F068CE0 46124100 */  add.s $f4, $f8, $f18
/* 09D814 7F068CE4 E6040014 */  swc1  $f4, 0x14($s0)
/* 09D818 7F068CE8 8DADA0B0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 09D81C 7F068CEC C4720000 */  lwc1  $f18, ($v1)
/* 09D820 7F068CF0 01B11021 */  addu  $v0, $t5, $s1
/* 09D824 7F068CF4 C44A0B10 */  lwc1  $f10, 0xb10($v0)
/* 09D828 7F068CF8 C4460B50 */  lwc1  $f6, 0xb50($v0)
/* 09D82C 7F068CFC 46065201 */  sub.s $f8, $f10, $f6
/* 09D830 7F068D00 46124103 */  div.s $f4, $f8, $f18
/* 09D834 7F068D04 46048280 */  add.s $f10, $f16, $f4
/* 09D838 7F068D08 E60A0018 */  swc1  $f10, 0x18($s0)
.L7F068D0C:
/* 09D83C 7F068D0C 8FBF001C */  lw    $ra, 0x1c($sp)
.L7F068D10:
/* 09D840 7F068D10 8FB00014 */  lw    $s0, 0x14($sp)
/* 09D844 7F068D14 8FB10018 */  lw    $s1, 0x18($sp)
/* 09D848 7F068D18 03E00008 */  jr    $ra
/* 09D84C 7F068D1C 27BD00C0 */   addiu $sp, $sp, 0xc0
)
#endif

#ifdef VERSION_EU
GLOBAL_ASM(
.late_rodata
glabel D_800543B4
.word 0x3dccccce /*0.10000001*/
glabel D_800543B8
.word 0x3f088888 /*0.5333333*/
glabel D_800543BC
.word 0x40c90fdb /*6.2831855*/
glabel D_800543C0
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543C4
.word 0x40c90fdb /*6.2831855*/
glabel D_800543C8
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543CC
.word 0x40c90fdb /*6.2831855*/
glabel D_800543D0
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543D4
.word 0x49634ea0 /* 931050.0 */
glabel expended_shell_initial_gravity_modifier_pistol
.word 0x3e8e38e4 /*0.27777779*/
glabel D_800543DC
.word 0x3fb55555 /*1.4166666*/
glabel D_800543E0
.word 0x3fd55555 /*1.6666666*/
glabel D_800543E4
.word 0x40c90fdb /*6.2831855*/
glabel D_800543E8
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543EC
.word 0x40c90fdb /*6.2831855*/
glabel D_800543F0
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543F4
.word 0x40c90fdb /*6.2831855*/
glabel D_800543F8
.word 0x3ec90fdb /*0.39269909*/
glabel D_800543FC
.word 0x49634ea0 /* 931050.0 */
glabel expended_shell_initial_gravity_modifier_non_pistol
.word 0x3e8e38e4 /*0.27777779*/
.text
glabel sub_GAME_7F068508
/* 09B658 7F068C68 27BDFEF8 */  addiu $sp, $sp, -0x108
/* 09B65C 7F068C6C AFBF001C */  sw    $ra, 0x1c($sp)
/* 09B660 7F068C70 AFB00014 */  sw    $s0, 0x14($sp)
/* 09B664 7F068C74 00808025 */  move  $s0, $a0
/* 09B668 7F068C78 AFB10018 */  sw    $s1, 0x18($sp)
/* 09B66C 7F068C7C 0FC177A2 */  jal   getCurrentPlayerWeaponId
/* 09B670 7F068C80 AFA5010C */   sw    $a1, 0x10c($sp)
/* 09B674 7F068C84 AFA200C0 */  sw    $v0, 0xc0($sp)
/* 09B678 7F068C88 0FC17359 */  jal   get_ptr_item_statistics
/* 09B67C 7F068C8C 00402025 */   move  $a0, $v0
/* 09B680 7F068C90 8C430028 */  lw    $v1, 0x28($v0)
/* 09B684 7F068C94 506001FE */  beql  $v1, $zero, .L7F069490
/* 09B688 7F068C98 8FBF001C */   lw    $ra, 0x1c($sp)
/* 09B68C 7F068C9C 0FC26669 */  jal   getPlayerCount
/* 09B690 7F068CA0 AFA300B8 */   sw    $v1, 0xb8($sp)
/* 09B694 7F068CA4 28410002 */  slti  $at, $v0, 2
/* 09B698 7F068CA8 102001F8 */  beqz  $at, .L7F06948C
/* 09B69C 7F068CAC 3C028007 */   lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09B6A0 7F068CB0 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09B6A4 7F068CB4 001070C0 */  sll   $t6, $s0, 3
/* 09B6A8 7F068CB8 01D07023 */  subu  $t6, $t6, $s0
/* 09B6AC 7F068CBC 000E7080 */  sll   $t6, $t6, 2
/* 09B6B0 7F068CC0 004E7821 */  addu  $t7, $v0, $t6
/* 09B6B4 7F068CC4 8DF80818 */  lw    $t8, 0x818($t7)
/* 09B6B8 7F068CC8 001088C0 */  sll   $s1, $s0, 3
/* 09B6BC 7F068CCC 02308823 */  subu  $s1, $s1, $s0
/* 09B6C0 7F068CD0 8F030000 */  lw    $v1, ($t8)
/* 09B6C4 7F068CD4 00118880 */  sll   $s1, $s1, 2
/* 09B6C8 7F068CD8 02308821 */  addu  $s1, $s1, $s0
/* 09B6CC 7F068CDC 1060001F */  beqz  $v1, .L7F068D5C
/* 09B6D0 7F068CE0 00118880 */   sll   $s1, $s1, 2
/* 09B6D4 7F068CE4 8C620004 */  lw    $v0, 4($v1)
/* 09B6D8 7F068CE8 3C018005 */  lui   $at, %hi(D_800543B4) # $at, 0x8005
/* 09B6DC 7F068CEC C420A4F4 */  lwc1  $f0, %lo(D_800543B4)($at)
/* 09B6E0 7F068CF0 C4440000 */  lwc1  $f4, ($v0)
/* 09B6E4 7F068CF4 27A400AC */  addiu $a0, $sp, 0xac
/* 09B6E8 7F068CF8 27A500C4 */  addiu $a1, $sp, 0xc4
/* 09B6EC 7F068CFC 46002182 */  mul.s $f6, $f4, $f0
/* 09B6F0 7F068D00 E7A600AC */  swc1  $f6, 0xac($sp)
/* 09B6F4 7F068D04 C4480004 */  lwc1  $f8, 4($v0)
/* 09B6F8 7F068D08 46004282 */  mul.s $f10, $f8, $f0
/* 09B6FC 7F068D0C E7AA00B0 */  swc1  $f10, 0xb0($sp)
/* 09B700 7F068D10 C4520008 */  lwc1  $f18, 8($v0)
/* 09B704 7F068D14 46009102 */  mul.s $f4, $f18, $f0
/* 09B708 7F068D18 0FC16383 */  jal   matrix_4x4_set_identity_and_position
/* 09B70C 7F068D1C E7A400B4 */   swc1  $f4, 0xb4($sp)
/* 09B710 7F068D20 001088C0 */  sll   $s1, $s0, 3
/* 09B714 7F068D24 02308823 */  subu  $s1, $s1, $s0
/* 09B718 7F068D28 00118880 */  sll   $s1, $s1, 2
/* 09B71C 7F068D2C 02308821 */  addu  $s1, $s1, $s0
/* 09B720 7F068D30 3C198007 */  lui   $t9, %hi(g_CurrentPlayer) # $t9, 0x8007
/* 09B724 7F068D34 8F398BC0 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 09B728 7F068D38 00118880 */  sll   $s1, $s1, 2
/* 09B72C 7F068D3C 02308821 */  addu  $s1, $s1, $s0
/* 09B730 7F068D40 001188C0 */  sll   $s1, $s1, 3
/* 09B734 7F068D44 03312021 */  addu  $a0, $t9, $s1
/* 09B738 7F068D48 24840AD0 */  addiu $a0, $a0, 0xad0
/* 09B73C 7F068D4C 0FC16144 */  jal   matrix_4x4_multiply_in_place
/* 09B740 7F068D50 27A500C4 */   addiu $a1, $sp, 0xc4
/* 09B744 7F068D54 10000008 */  b     .L7F068D78
/* 09B748 7F068D58 8FA400B8 */   lw    $a0, 0xb8($sp)
.L7F068D5C:
/* 09B74C 7F068D5C 02308821 */  addu  $s1, $s1, $s0
/* 09B750 7F068D60 001188C0 */  sll   $s1, $s1, 3
/* 09B754 7F068D64 00512021 */  addu  $a0, $v0, $s1
/* 09B758 7F068D68 24840AD0 */  addiu $a0, $a0, 0xad0
/* 09B75C 7F068D6C 0FC16132 */  jal   matrix_4x4_copy
/* 09B760 7F068D70 27A500C4 */   addiu $a1, $sp, 0xc4
/* 09B764 7F068D74 8FA400B8 */  lw    $a0, 0xb8($sp)
.L7F068D78:
/* 09B768 7F068D78 0FC1A2F2 */  jal   casingCreate
/* 09B76C 7F068D7C 27A500C4 */   addiu $a1, $sp, 0xc4
/* 09B770 7F068D80 104001C2 */  beqz  $v0, .L7F06948C
/* 09B774 7F068D84 00408025 */   move  $s0, $v0
/* 09B778 7F068D88 3C098003 */  lui   $t1, %hi(D_80035EA4) # $t1, 0x8003
/* 09B77C 7F068D8C 252913F4 */  addiu $t1, %lo(D_80035EA4) # addiu $t1, $t1, 0x13f4
/* 09B780 7F068D90 8D210000 */  lw    $at, ($t1)
/* 09B784 7F068D94 8FA300C0 */  lw    $v1, 0xc0($sp)
/* 09B788 7F068D98 27A8009C */  addiu $t0, $sp, 0x9c
/* 09B78C 7F068D9C AD010000 */  sw    $at, ($t0)
/* 09B790 7F068DA0 8D210008 */  lw    $at, 8($t1)
/* 09B794 7F068DA4 8D2B0004 */  lw    $t3, 4($t1)
/* 09B798 7F068DA8 AD010008 */  sw    $at, 8($t0)
/* 09B79C 7F068DAC AD0B0004 */  sw    $t3, 4($t0)
/* 09B7A0 7F068DB0 C7A6010C */  lwc1  $f6, 0x10c($sp)
/* 09B7A4 7F068DB4 24010004 */  li    $at, 4
/* 09B7A8 7F068DB8 1061000A */  beq   $v1, $at, .L7F068DE4
/* 09B7AC 7F068DBC E4460000 */   swc1  $f6, ($v0)
/* 09B7B0 7F068DC0 24010005 */  li    $at, 5
/* 09B7B4 7F068DC4 10610007 */  beq   $v1, $at, .L7F068DE4
/* 09B7B8 7F068DC8 24010006 */   li    $at, 6
/* 09B7BC 7F068DCC 10610005 */  beq   $v1, $at, .L7F068DE4
/* 09B7C0 7F068DD0 24010014 */   li    $at, 20
/* 09B7C4 7F068DD4 10610003 */  beq   $v1, $at, .L7F068DE4
/* 09B7C8 7F068DD8 24010015 */   li    $at, 21
/* 09B7CC 7F068DDC 146100D6 */  bne   $v1, $at, .L7F069138
/* 09B7D0 7F068DE0 00000000 */   nop
.L7F068DE4:
/* 09B7D4 7F068DE4 0C00262C */  jal   randomGetNext
/* 09B7D8 7F068DE8 00000000 */   nop
/* 09B7DC 7F068DEC 44824000 */  mtc1  $v0, $f8
/* 09B7E0 7F068DF0 3C018005 */  lui   $at, %hi(D_800543B8) # $at, 0x8005
/* 09B7E4 7F068DF4 C420A4F8 */  lwc1  $f0, %lo(D_800543B8)($at)
/* 09B7E8 7F068DF8 04410005 */  bgez  $v0, .L7F068E10
/* 09B7EC 7F068DFC 468042A0 */   cvt.s.w $f10, $f8
/* 09B7F0 7F068E00 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09B7F4 7F068E04 44819000 */  mtc1  $at, $f18
/* 09B7F8 7F068E08 00000000 */  nop
/* 09B7FC 7F068E0C 46125280 */  add.s $f10, $f10, $f18
.L7F068E10:
/* 09B800 7F068E10 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09B804 7F068E14 44812000 */  mtc1  $at, $f4
/* 09B808 7F068E18 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09B80C 7F068E1C 44819000 */  mtc1  $at, $f18
/* 09B810 7F068E20 46045182 */  mul.s $f6, $f10, $f4
/* 09B814 7F068E24 00000000 */  nop
/* 09B818 7F068E28 46003202 */  mul.s $f8, $f6, $f0
/* 09B81C 7F068E2C 00000000 */  nop
/* 09B820 7F068E30 46124282 */  mul.s $f10, $f8, $f18
/* 09B824 7F068E34 46005100 */  add.s $f4, $f10, $f0
/* 09B828 7F068E38 46002187 */  neg.s $f6, $f4
/* 09B82C 7F068E3C 0C00262C */  jal   randomGetNext
/* 09B830 7F068E40 E6060010 */   swc1  $f6, 0x10($s0)
/* 09B834 7F068E44 44824000 */  mtc1  $v0, $f8
/* 09B838 7F068E48 3C014020 */  li    $at, 0x40200000 # 2.500000
/* 09B83C 7F068E4C 44810000 */  mtc1  $at, $f0
/* 09B840 7F068E50 04410005 */  bgez  $v0, .L7F068E68
/* 09B844 7F068E54 468044A0 */   cvt.s.w $f18, $f8
/* 09B848 7F068E58 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09B84C 7F068E5C 44815000 */  mtc1  $at, $f10
/* 09B850 7F068E60 00000000 */  nop
/* 09B854 7F068E64 460A9480 */  add.s $f18, $f18, $f10
.L7F068E68:
/* 09B858 7F068E68 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09B85C 7F068E6C 44812000 */  mtc1  $at, $f4
/* 09B860 7F068E70 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09B864 7F068E74 44815000 */  mtc1  $at, $f10
/* 09B868 7F068E78 46049182 */  mul.s $f6, $f18, $f4
/* 09B86C 7F068E7C 3C0C8007 */  lui   $t4, %hi(g_CurrentPlayer) # $t4, 0x8007
/* 09B870 7F068E80 26050010 */  addiu $a1, $s0, 0x10
/* 09B874 7F068E84 46003202 */  mul.s $f8, $f6, $f0
/* 09B878 7F068E88 44803000 */  mtc1  $zero, $f6
/* 09B87C 7F068E8C 00000000 */  nop
/* 09B880 7F068E90 E6060018 */  swc1  $f6, 0x18($s0)
/* 09B884 7F068E94 460A4482 */  mul.s $f18, $f8, $f10
/* 09B888 7F068E98 46009100 */  add.s $f4, $f18, $f0
/* 09B88C 7F068E9C E6040014 */  swc1  $f4, 0x14($s0)
/* 09B890 7F068EA0 8D8C8BC0 */  lw    $t4, %lo(g_CurrentPlayer)($t4)
/* 09B894 7F068EA4 01912021 */  addu  $a0, $t4, $s1
/* 09B898 7F068EA8 0FC16220 */  jal   mtx4RotateVecInPlace
/* 09B89C 7F068EAC 24840AD0 */   addiu $a0, $a0, 0xad0
/* 09B8A0 7F068EB0 0C00262C */  jal   randomGetNext
/* 09B8A4 7F068EB4 00000000 */   nop
/* 09B8A8 7F068EB8 44824000 */  mtc1  $v0, $f8
/* 09B8AC 7F068EBC 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09B8B0 7F068EC0 04410004 */  bgez  $v0, .L7F068ED4
/* 09B8B4 7F068EC4 468042A0 */   cvt.s.w $f10, $f8
/* 09B8B8 7F068EC8 44819000 */  mtc1  $at, $f18
/* 09B8BC 7F068ECC 00000000 */  nop
/* 09B8C0 7F068ED0 46125280 */  add.s $f10, $f10, $f18
.L7F068ED4:
/* 09B8C4 7F068ED4 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09B8C8 7F068ED8 44812000 */  mtc1  $at, $f4
/* 09B8CC 7F068EDC 3C018005 */  lui   $at, %hi(D_800543BC) # $at, 0x8005
/* 09B8D0 7F068EE0 C428A4FC */  lwc1  $f8, %lo(D_800543BC)($at)
/* 09B8D4 7F068EE4 46045002 */  mul.s $f0, $f10, $f4
/* 09B8D8 7F068EE8 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09B8DC 7F068EEC 44815000 */  mtc1  $at, $f10
/* 09B8E0 7F068EF0 3C018005 */  lui   $at, %hi(D_800543C0) # $at, 0x8005
/* 09B8E4 7F068EF4 46000180 */  add.s $f6, $f0, $f0
/* 09B8E8 7F068EF8 46083482 */  mul.s $f18, $f6, $f8
/* 09B8EC 7F068EFC C426A500 */  lwc1  $f6, %lo(D_800543C0)($at)
/* 09B8F0 7F068F00 460A9102 */  mul.s $f4, $f18, $f10
/* 09B8F4 7F068F04 46062201 */  sub.s $f8, $f4, $f6
/* 09B8F8 7F068F08 0C00262C */  jal   randomGetNext
/* 09B8FC 7F068F0C E7A8009C */   swc1  $f8, 0x9c($sp)
/* 09B900 7F068F10 44829000 */  mtc1  $v0, $f18
/* 09B904 7F068F14 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09B908 7F068F18 04410004 */  bgez  $v0, .L7F068F2C
/* 09B90C 7F068F1C 468092A0 */   cvt.s.w $f10, $f18
/* 09B910 7F068F20 44812000 */  mtc1  $at, $f4
/* 09B914 7F068F24 00000000 */  nop
/* 09B918 7F068F28 46045280 */  add.s $f10, $f10, $f4
.L7F068F2C:
/* 09B91C 7F068F2C 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09B920 7F068F30 44813000 */  mtc1  $at, $f6
/* 09B924 7F068F34 3C018005 */  lui   $at, %hi(D_800543C4) # $at, 0x8005
/* 09B928 7F068F38 C432A504 */  lwc1  $f18, %lo(D_800543C4)($at)
/* 09B92C 7F068F3C 46065002 */  mul.s $f0, $f10, $f6
/* 09B930 7F068F40 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09B934 7F068F44 44815000 */  mtc1  $at, $f10
/* 09B938 7F068F48 3C018005 */  lui   $at, %hi(D_800543C8) # $at, 0x8005
/* 09B93C 7F068F4C 46000200 */  add.s $f8, $f0, $f0
/* 09B940 7F068F50 46124102 */  mul.s $f4, $f8, $f18
/* 09B944 7F068F54 C428A508 */  lwc1  $f8, %lo(D_800543C8)($at)
/* 09B948 7F068F58 460A2182 */  mul.s $f6, $f4, $f10
/* 09B94C 7F068F5C 46083481 */  sub.s $f18, $f6, $f8
/* 09B950 7F068F60 0C00262C */  jal   randomGetNext
/* 09B954 7F068F64 E7B200A0 */   swc1  $f18, 0xa0($sp)
/* 09B958 7F068F68 44822000 */  mtc1  $v0, $f4
/* 09B95C 7F068F6C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09B960 7F068F70 04410004 */  bgez  $v0, .L7F068F84
/* 09B964 7F068F74 468022A0 */   cvt.s.w $f10, $f4
/* 09B968 7F068F78 44813000 */  mtc1  $at, $f6
/* 09B96C 7F068F7C 00000000 */  nop
/* 09B970 7F068F80 46065280 */  add.s $f10, $f10, $f6
.L7F068F84:
/* 09B974 7F068F84 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09B978 7F068F88 44814000 */  mtc1  $at, $f8
/* 09B97C 7F068F8C 3C018005 */  lui   $at, %hi(D_800543CC) # $at, 0x8005
/* 09B980 7F068F90 C424A50C */  lwc1  $f4, %lo(D_800543CC)($at)
/* 09B984 7F068F94 46085002 */  mul.s $f0, $f10, $f8
/* 09B988 7F068F98 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09B98C 7F068F9C 44815000 */  mtc1  $at, $f10
/* 09B990 7F068FA0 3C018005 */  lui   $at, %hi(D_800543D0) # $at, 0x8005
/* 09B994 7F068FA4 27A4009C */  addiu $a0, $sp, 0x9c
/* 09B998 7F068FA8 27A5005C */  addiu $a1, $sp, 0x5c
/* 09B99C 7F068FAC 46000480 */  add.s $f18, $f0, $f0
/* 09B9A0 7F068FB0 46049182 */  mul.s $f6, $f18, $f4
/* 09B9A4 7F068FB4 C432A510 */  lwc1  $f18, %lo(D_800543D0)($at)
/* 09B9A8 7F068FB8 460A3202 */  mul.s $f8, $f6, $f10
/* 09B9AC 7F068FBC 46124101 */  sub.s $f4, $f8, $f18
/* 09B9B0 7F068FC0 0FC162EF */  jal   matrix_4x4_set_rotation_around_xyz
/* 09B9B4 7F068FC4 E7A400A4 */   swc1  $f4, 0xa4($sp)
/* 09B9B8 7F068FC8 27A4005C */  addiu $a0, $sp, 0x5c
/* 09B9BC 7F068FCC 0FC1610B */  jal   matrix_7f05842c_eu
/* 09B9C0 7F068FD0 26050040 */   addiu $a1, $s0, 0x40
/* 09B9C4 7F068FD4 0C00262C */  jal   randomGetNext
/* 09B9C8 7F068FD8 00000000 */   nop
/* 09B9CC 7F068FDC 3C030015 */  lui   $v1, (0x00158679 >> 16) # lui $v1, 0x15
/* 09B9D0 7F068FE0 34638679 */  ori   $v1, (0x00158679 & 0xFFFF) # ori $v1, $v1, 0x8679
/* 09B9D4 7F068FE4 00026E02 */  srl   $t5, $v0, 0x18
/* 09B9D8 7F068FE8 01A30019 */  multu $t5, $v1
/* 09B9DC 7F068FEC 00007012 */  mflo  $t6
/* 09B9E0 7F068FF0 000E7A83 */  sra   $t7, $t6, 0xa
/* 09B9E4 7F068FF4 01E3C021 */  addu  $t8, $t7, $v1
/* 09B9E8 7F068FF8 0C00262C */  jal   randomGetNext
/* 09B9EC 7F068FFC AFB8004C */   sw    $t8, 0x4c($sp)
/* 09B9F0 7F069000 8FB9004C */  lw    $t9, 0x4c($sp)
/* 09B9F4 7F069004 C60C0014 */  lwc1  $f12, 0x14($s0)
/* 09B9F8 7F069008 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09B9FC 7F06900C 0059001B */  divu  $zero, $v0, $t9
/* 09BA00 7F069010 00005010 */  mfhi  $t2
/* 09BA04 7F069014 448A3000 */  mtc1  $t2, $f6
/* 09BA08 7F069018 17200002 */  bnez  $t9, .L7F069024
/* 09BA0C 7F06901C 00000000 */   nop
/* 09BA10 7F069020 0007000D */  break 7
.L7F069024:
/* 09BA14 7F069024 3C048007 */  lui   $a0, %hi(g_CurrentPlayer) # $a0, 0x8007
/* 09BA18 7F069028 05410004 */  bgez  $t2, .L7F06903C
/* 09BA1C 7F06902C 468032A0 */   cvt.s.w $f10, $f6
/* 09BA20 7F069030 44814000 */  mtc1  $at, $f8
/* 09BA24 7F069034 00000000 */  nop
/* 09BA28 7F069038 46085280 */  add.s $f10, $f10, $f8
.L7F06903C:
/* 09BA2C 7F06903C 3C018005 */  lui   $at, %hi(D_800543D4) # $at, 0x8005
/* 09BA30 7F069040 C432A514 */  lwc1  $f18, %lo(D_800543D4)($at)
/* 09BA34 7F069044 3C018005 */  lui   $at, %hi(expended_shell_initial_gravity_modifier_pistol) # $at, 0x8005
/* 09BA38 7F069048 C424A518 */  lwc1  $f4, %lo(expended_shell_initial_gravity_modifier_pistol)($at)
/* 09BA3C 7F06904C 46125003 */  div.s $f0, $f10, $f18
/* 09BA40 7F069050 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09BA44 7F069054 44819000 */  mtc1  $at, $f18
/* 09BA48 7F069058 C60E0010 */  lwc1  $f14, 0x10($s0)
/* 09BA4C 7F06905C C6100018 */  lwc1  $f16, 0x18($s0)
/* 09BA50 7F069060 3C088004 */  lui   $t0, %hi(g_ClockTimer) # $t0, 0x8004
/* 09BA54 7F069064 24848BC0 */  addiu $a0, %lo(g_CurrentPlayer) # addiu $a0, $a0, -0x7440
/* 09BA58 7F069068 46040182 */  mul.s $f6, $f0, $f4
/* 09BA5C 7F06906C 46066081 */  sub.s $f2, $f12, $f6
/* 09BA60 7F069070 C6060008 */  lwc1  $f6, 8($s0)
/* 09BA64 7F069074 46026200 */  add.s $f8, $f12, $f2
/* 09BA68 7F069078 E6020014 */  swc1  $f2, 0x14($s0)
/* 09BA6C 7F06907C 46080282 */  mul.s $f10, $f0, $f8
/* 09BA70 7F069080 00000000 */  nop
/* 09BA74 7F069084 46125102 */  mul.s $f4, $f10, $f18
/* 09BA78 7F069088 C60A0004 */  lwc1  $f10, 4($s0)
/* 09BA7C 7F06908C 460E0482 */  mul.s $f18, $f0, $f14
/* 09BA80 7F069090 46043200 */  add.s $f8, $f6, $f4
/* 09BA84 7F069094 C604000C */  lwc1  $f4, 0xc($s0)
/* 09BA88 7F069098 46125180 */  add.s $f6, $f10, $f18
/* 09BA8C 7F06909C E6080008 */  swc1  $f8, 8($s0)
/* 09BA90 7F0690A0 46100202 */  mul.s $f8, $f0, $f16
/* 09BA94 7F0690A4 E6060004 */  swc1  $f6, 4($s0)
/* 09BA98 7F0690A8 46082280 */  add.s $f10, $f4, $f8
/* 09BA9C 7F0690AC E60A000C */  swc1  $f10, 0xc($s0)
/* 09BAA0 7F0690B0 8D080FF4 */  lw    $t0, %lo(g_ClockTimer)($t0)
/* 09BAA4 7F0690B4 190000F5 */  blez  $t0, .L7F06948C
/* 09BAA8 7F0690B8 00000000 */   nop
/* 09BAAC 7F0690BC 8C890000 */  lw    $t1, ($a0)
/* 09BAB0 7F0690C0 3C038004 */  lui   $v1, %hi(g_GlobalTimerDelta) # $v1, 0x8004
/* 09BAB4 7F0690C4 24631004 */  addiu $v1, %lo(g_GlobalTimerDelta) # addiu $v1, $v1, 0x1004
/* 09BAB8 7F0690C8 01311021 */  addu  $v0, $t1, $s1
/* 09BABC 7F0690CC C4520B00 */  lwc1  $f18, 0xb00($v0)
/* 09BAC0 7F0690D0 C4460B40 */  lwc1  $f6, 0xb40($v0)
/* 09BAC4 7F0690D4 C4680000 */  lwc1  $f8, ($v1)
/* 09BAC8 7F0690D8 46069101 */  sub.s $f4, $f18, $f6
/* 09BACC 7F0690DC 46082283 */  div.s $f10, $f4, $f8
/* 09BAD0 7F0690E0 460A7480 */  add.s $f18, $f14, $f10
/* 09BAD4 7F0690E4 E6120010 */  swc1  $f18, 0x10($s0)
/* 09BAD8 7F0690E8 8C8B0000 */  lw    $t3, ($a0)
/* 09BADC 7F0690EC C46A0000 */  lwc1  $f10, ($v1)
/* 09BAE0 7F0690F0 01711021 */  addu  $v0, $t3, $s1
/* 09BAE4 7F0690F4 C4460B04 */  lwc1  $f6, 0xb04($v0)
/* 09BAE8 7F0690F8 C4440B44 */  lwc1  $f4, 0xb44($v0)
/* 09BAEC 7F0690FC 46043201 */  sub.s $f8, $f6, $f4
/* 09BAF0 7F069100 C6060014 */  lwc1  $f6, 0x14($s0)
/* 09BAF4 7F069104 460A4483 */  div.s $f18, $f8, $f10
/* 09BAF8 7F069108 46123100 */  add.s $f4, $f6, $f18
/* 09BAFC 7F06910C E6040014 */  swc1  $f4, 0x14($s0)
/* 09BB00 7F069110 8C8C0000 */  lw    $t4, ($a0)
/* 09BB04 7F069114 C4720000 */  lwc1  $f18, ($v1)
/* 09BB08 7F069118 01911021 */  addu  $v0, $t4, $s1
/* 09BB0C 7F06911C C4480B08 */  lwc1  $f8, 0xb08($v0)
/* 09BB10 7F069120 C44A0B48 */  lwc1  $f10, 0xb48($v0)
/* 09BB14 7F069124 460A4181 */  sub.s $f6, $f8, $f10
/* 09BB18 7F069128 46123103 */  div.s $f4, $f6, $f18
/* 09BB1C 7F06912C 46048200 */  add.s $f8, $f16, $f4
/* 09BB20 7F069130 100000D6 */  b     .L7F06948C
/* 09BB24 7F069134 E6080018 */   swc1  $f8, 0x18($s0)
.L7F069138:
/* 09BB28 7F069138 0C00262C */  jal   randomGetNext
/* 09BB2C 7F06913C 00000000 */   nop
/* 09BB30 7F069140 44825000 */  mtc1  $v0, $f10
/* 09BB34 7F069144 3C018005 */  lui   $at, %hi(D_800543DC) # $at, 0x8005
/* 09BB38 7F069148 C420A51C */  lwc1  $f0, %lo(D_800543DC)($at)
/* 09BB3C 7F06914C 04410005 */  bgez  $v0, .L7F069164
/* 09BB40 7F069150 468051A0 */   cvt.s.w $f6, $f10
/* 09BB44 7F069154 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09BB48 7F069158 44819000 */  mtc1  $at, $f18
/* 09BB4C 7F06915C 00000000 */  nop
/* 09BB50 7F069160 46123180 */  add.s $f6, $f6, $f18
.L7F069164:
/* 09BB54 7F069164 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09BB58 7F069168 44812000 */  mtc1  $at, $f4
/* 09BB5C 7F06916C 3C013E00 */  li    $at, 0x3E000000 # 0.125000
/* 09BB60 7F069170 44819000 */  mtc1  $at, $f18
/* 09BB64 7F069174 46043202 */  mul.s $f8, $f6, $f4
/* 09BB68 7F069178 00000000 */  nop
/* 09BB6C 7F06917C 46004282 */  mul.s $f10, $f8, $f0
/* 09BB70 7F069180 00000000 */  nop
/* 09BB74 7F069184 46125182 */  mul.s $f6, $f10, $f18
/* 09BB78 7F069188 46003100 */  add.s $f4, $f6, $f0
/* 09BB7C 7F06918C 46002207 */  neg.s $f8, $f4
/* 09BB80 7F069190 0C00262C */  jal   randomGetNext
/* 09BB84 7F069194 E6080010 */   swc1  $f8, 0x10($s0)
/* 09BB88 7F069198 44825000 */  mtc1  $v0, $f10
/* 09BB8C 7F06919C 3C018005 */  lui   $at, %hi(D_800543E0) # $at, 0x8005
/* 09BB90 7F0691A0 C420A520 */  lwc1  $f0, %lo(D_800543E0)($at)
/* 09BB94 7F0691A4 04410005 */  bgez  $v0, .L7F0691BC
/* 09BB98 7F0691A8 468054A0 */   cvt.s.w $f18, $f10
/* 09BB9C 7F0691AC 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09BBA0 7F0691B0 44813000 */  mtc1  $at, $f6
/* 09BBA4 7F0691B4 00000000 */  nop
/* 09BBA8 7F0691B8 46069480 */  add.s $f18, $f18, $f6
.L7F0691BC:
/* 09BBAC 7F0691BC 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09BBB0 7F0691C0 44812000 */  mtc1  $at, $f4
/* 09BBB4 7F0691C4 3C013E00 */  li    $at, 0x3E000000 # 0.125000
/* 09BBB8 7F0691C8 44813000 */  mtc1  $at, $f6
/* 09BBBC 7F0691CC 46049202 */  mul.s $f8, $f18, $f4
/* 09BBC0 7F0691D0 3C0D8007 */  lui   $t5, %hi(g_CurrentPlayer) # $t5, 0x8007
/* 09BBC4 7F0691D4 26050010 */  addiu $a1, $s0, 0x10
/* 09BBC8 7F0691D8 46004282 */  mul.s $f10, $f8, $f0
/* 09BBCC 7F0691DC 44804000 */  mtc1  $zero, $f8
/* 09BBD0 7F0691E0 00000000 */  nop
/* 09BBD4 7F0691E4 E6080018 */  swc1  $f8, 0x18($s0)
/* 09BBD8 7F0691E8 46065482 */  mul.s $f18, $f10, $f6
/* 09BBDC 7F0691EC 46009100 */  add.s $f4, $f18, $f0
/* 09BBE0 7F0691F0 E6040014 */  swc1  $f4, 0x14($s0)
/* 09BBE4 7F0691F4 8DAD8BC0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 09BBE8 7F0691F8 01B12021 */  addu  $a0, $t5, $s1
/* 09BBEC 7F0691FC 0FC16220 */  jal   mtx4RotateVecInPlace
/* 09BBF0 7F069200 24840AD0 */   addiu $a0, $a0, 0xad0
/* 09BBF4 7F069204 0C00262C */  jal   randomGetNext
/* 09BBF8 7F069208 00000000 */   nop
/* 09BBFC 7F06920C 44825000 */  mtc1  $v0, $f10
/* 09BC00 7F069210 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09BC04 7F069214 04410004 */  bgez  $v0, .L7F069228
/* 09BC08 7F069218 468051A0 */   cvt.s.w $f6, $f10
/* 09BC0C 7F06921C 44819000 */  mtc1  $at, $f18
/* 09BC10 7F069220 00000000 */  nop
/* 09BC14 7F069224 46123180 */  add.s $f6, $f6, $f18
.L7F069228:
/* 09BC18 7F069228 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09BC1C 7F06922C 44812000 */  mtc1  $at, $f4
/* 09BC20 7F069230 3C018005 */  lui   $at, %hi(D_800543E4) # $at, 0x8005
/* 09BC24 7F069234 C42AA524 */  lwc1  $f10, %lo(D_800543E4)($at)
/* 09BC28 7F069238 46043002 */  mul.s $f0, $f6, $f4
/* 09BC2C 7F06923C 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09BC30 7F069240 44813000 */  mtc1  $at, $f6
/* 09BC34 7F069244 3C018005 */  lui   $at, %hi(D_800543E8) # $at, 0x8005
/* 09BC38 7F069248 46000200 */  add.s $f8, $f0, $f0
/* 09BC3C 7F06924C 460A4482 */  mul.s $f18, $f8, $f10
/* 09BC40 7F069250 C428A528 */  lwc1  $f8, %lo(D_800543E8)($at)
/* 09BC44 7F069254 46069102 */  mul.s $f4, $f18, $f6
/* 09BC48 7F069258 46082281 */  sub.s $f10, $f4, $f8
/* 09BC4C 7F06925C 0C00262C */  jal   randomGetNext
/* 09BC50 7F069260 E7AA009C */   swc1  $f10, 0x9c($sp)
/* 09BC54 7F069264 44829000 */  mtc1  $v0, $f18
/* 09BC58 7F069268 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09BC5C 7F06926C 04410004 */  bgez  $v0, .L7F069280
/* 09BC60 7F069270 468091A0 */   cvt.s.w $f6, $f18
/* 09BC64 7F069274 44812000 */  mtc1  $at, $f4
/* 09BC68 7F069278 00000000 */  nop
/* 09BC6C 7F06927C 46043180 */  add.s $f6, $f6, $f4
.L7F069280:
/* 09BC70 7F069280 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09BC74 7F069284 44814000 */  mtc1  $at, $f8
/* 09BC78 7F069288 3C018005 */  lui   $at, %hi(D_800543EC) # $at, 0x8005
/* 09BC7C 7F06928C C432A52C */  lwc1  $f18, %lo(D_800543EC)($at)
/* 09BC80 7F069290 46083002 */  mul.s $f0, $f6, $f8
/* 09BC84 7F069294 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09BC88 7F069298 44813000 */  mtc1  $at, $f6
/* 09BC8C 7F06929C 3C018005 */  lui   $at, %hi(D_800543F0) # $at, 0x8005
/* 09BC90 7F0692A0 46000280 */  add.s $f10, $f0, $f0
/* 09BC94 7F0692A4 46125102 */  mul.s $f4, $f10, $f18
/* 09BC98 7F0692A8 C42AA530 */  lwc1  $f10, %lo(D_800543F0)($at)
/* 09BC9C 7F0692AC 46062202 */  mul.s $f8, $f4, $f6
/* 09BCA0 7F0692B0 460A4481 */  sub.s $f18, $f8, $f10
/* 09BCA4 7F0692B4 0C00262C */  jal   randomGetNext
/* 09BCA8 7F0692B8 E7B200A0 */   swc1  $f18, 0xa0($sp)
/* 09BCAC 7F0692BC 44822000 */  mtc1  $v0, $f4
/* 09BCB0 7F0692C0 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09BCB4 7F0692C4 04410004 */  bgez  $v0, .L7F0692D8
/* 09BCB8 7F0692C8 468021A0 */   cvt.s.w $f6, $f4
/* 09BCBC 7F0692CC 44814000 */  mtc1  $at, $f8
/* 09BCC0 7F0692D0 00000000 */  nop
/* 09BCC4 7F0692D4 46083180 */  add.s $f6, $f6, $f8
.L7F0692D8:
/* 09BCC8 7F0692D8 3C012F80 */  li    $at, 0x2F800000 # 0.000000
/* 09BCCC 7F0692DC 44815000 */  mtc1  $at, $f10
/* 09BCD0 7F0692E0 3C018005 */  lui   $at, %hi(D_800543F4) # $at, 0x8005
/* 09BCD4 7F0692E4 C424A534 */  lwc1  $f4, %lo(D_800543F4)($at)
/* 09BCD8 7F0692E8 460A3002 */  mul.s $f0, $f6, $f10
/* 09BCDC 7F0692EC 3C013D80 */  li    $at, 0x3D800000 # 0.062500
/* 09BCE0 7F0692F0 44813000 */  mtc1  $at, $f6
/* 09BCE4 7F0692F4 3C018005 */  lui   $at, %hi(D_800543F8) # $at, 0x8005
/* 09BCE8 7F0692F8 27A4009C */  addiu $a0, $sp, 0x9c
/* 09BCEC 7F0692FC 27A5005C */  addiu $a1, $sp, 0x5c
/* 09BCF0 7F069300 46000480 */  add.s $f18, $f0, $f0
/* 09BCF4 7F069304 46049202 */  mul.s $f8, $f18, $f4
/* 09BCF8 7F069308 C432A538 */  lwc1  $f18, %lo(D_800543F8)($at)
/* 09BCFC 7F06930C 46064282 */  mul.s $f10, $f8, $f6
/* 09BD00 7F069310 46125101 */  sub.s $f4, $f10, $f18
/* 09BD04 7F069314 0FC162EF */  jal   matrix_4x4_set_rotation_around_xyz
/* 09BD08 7F069318 E7A400A4 */   swc1  $f4, 0xa4($sp)
/* 09BD0C 7F06931C 27A4005C */  addiu $a0, $sp, 0x5c
/* 09BD10 7F069320 0FC1610B */  jal   matrix_7f05842c_eu
/* 09BD14 7F069324 26050040 */   addiu $a1, $s0, 0x40
/* 09BD18 7F069328 0C00262C */  jal   randomGetNext
/* 09BD1C 7F06932C 00000000 */   nop
/* 09BD20 7F069330 3C030015 */  lui   $v1, (0x00158679 >> 16) # lui $v1, 0x15
/* 09BD24 7F069334 34638679 */  ori   $v1, (0x00158679 & 0xFFFF) # ori $v1, $v1, 0x8679
/* 09BD28 7F069338 00027602 */  srl   $t6, $v0, 0x18
/* 09BD2C 7F06933C 01C30019 */  multu $t6, $v1
/* 09BD30 7F069340 00007812 */  mflo  $t7
/* 09BD34 7F069344 000FC283 */  sra   $t8, $t7, 0xa
/* 09BD38 7F069348 0303C821 */  addu  $t9, $t8, $v1
/* 09BD3C 7F06934C 0C00262C */  jal   randomGetNext
/* 09BD40 7F069350 AFB9003C */   sw    $t9, 0x3c($sp)
/* 09BD44 7F069354 8FAA003C */  lw    $t2, 0x3c($sp)
/* 09BD48 7F069358 C60C0014 */  lwc1  $f12, 0x14($s0)
/* 09BD4C 7F06935C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09BD50 7F069360 004A001B */  divu  $zero, $v0, $t2
/* 09BD54 7F069364 00004010 */  mfhi  $t0
/* 09BD58 7F069368 44884000 */  mtc1  $t0, $f8
/* 09BD5C 7F06936C 15400002 */  bnez  $t2, .L7F069378
/* 09BD60 7F069370 00000000 */   nop
/* 09BD64 7F069374 0007000D */  break 7
.L7F069378:
/* 09BD68 7F069378 3C0B8007 */  lui   $t3, %hi(g_CurrentPlayer) # $t3, 0x8007
/* 09BD6C 7F06937C 05010004 */  bgez  $t0, .L7F069390
/* 09BD70 7F069380 468041A0 */   cvt.s.w $f6, $f8
/* 09BD74 7F069384 44815000 */  mtc1  $at, $f10
/* 09BD78 7F069388 00000000 */  nop
/* 09BD7C 7F06938C 460A3180 */  add.s $f6, $f6, $f10
.L7F069390:
/* 09BD80 7F069390 3C018005 */  lui   $at, %hi(D_800543FC) # $at, 0x8005
/* 09BD84 7F069394 C432A53C */  lwc1  $f18, %lo(D_800543FC)($at)
/* 09BD88 7F069398 3C018005 */  lui   $at, %hi(expended_shell_initial_gravity_modifier_non_pistol) # $at, 0x8005
/* 09BD8C 7F06939C C424A540 */  lwc1  $f4, %lo(expended_shell_initial_gravity_modifier_non_pistol)($at)
/* 09BD90 7F0693A0 46123003 */  div.s $f0, $f6, $f18
/* 09BD94 7F0693A4 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09BD98 7F0693A8 44819000 */  mtc1  $at, $f18
/* 09BD9C 7F0693AC C60E0010 */  lwc1  $f14, 0x10($s0)
/* 09BDA0 7F0693B0 C6100018 */  lwc1  $f16, 0x18($s0)
/* 09BDA4 7F0693B4 3C098004 */  lui   $t1, %hi(g_ClockTimer) # $t1, 0x8004
/* 09BDA8 7F0693B8 46040202 */  mul.s $f8, $f0, $f4
/* 09BDAC 7F0693BC 46086081 */  sub.s $f2, $f12, $f8
/* 09BDB0 7F0693C0 C6080008 */  lwc1  $f8, 8($s0)
/* 09BDB4 7F0693C4 46026280 */  add.s $f10, $f12, $f2
/* 09BDB8 7F0693C8 E6020014 */  swc1  $f2, 0x14($s0)
/* 09BDBC 7F0693CC 460A0182 */  mul.s $f6, $f0, $f10
/* 09BDC0 7F0693D0 00000000 */  nop
/* 09BDC4 7F0693D4 46123102 */  mul.s $f4, $f6, $f18
/* 09BDC8 7F0693D8 C6060004 */  lwc1  $f6, 4($s0)
/* 09BDCC 7F0693DC 460E0482 */  mul.s $f18, $f0, $f14
/* 09BDD0 7F0693E0 46044280 */  add.s $f10, $f8, $f4
/* 09BDD4 7F0693E4 C604000C */  lwc1  $f4, 0xc($s0)
/* 09BDD8 7F0693E8 46123200 */  add.s $f8, $f6, $f18
/* 09BDDC 7F0693EC E60A0008 */  swc1  $f10, 8($s0)
/* 09BDE0 7F0693F0 46100282 */  mul.s $f10, $f0, $f16
/* 09BDE4 7F0693F4 E6080004 */  swc1  $f8, 4($s0)
/* 09BDE8 7F0693F8 460A2180 */  add.s $f6, $f4, $f10
/* 09BDEC 7F0693FC E606000C */  swc1  $f6, 0xc($s0)
/* 09BDF0 7F069400 8D290FF4 */  lw    $t1, %lo(g_ClockTimer)($t1)
/* 09BDF4 7F069404 19200021 */  blez  $t1, .L7F06948C
/* 09BDF8 7F069408 00000000 */   nop
/* 09BDFC 7F06940C 8D6B8BC0 */  lw    $t3, %lo(g_CurrentPlayer)($t3)
/* 09BE00 7F069410 3C038004 */  lui   $v1, %hi(g_GlobalTimerDelta) # $v1, 0x8004
/* 09BE04 7F069414 24631004 */  addiu $v1, %lo(g_GlobalTimerDelta) # addiu $v1, $v1, 0x1004
/* 09BE08 7F069418 01711021 */  addu  $v0, $t3, $s1
/* 09BE0C 7F06941C C4520B00 */  lwc1  $f18, 0xb00($v0)
/* 09BE10 7F069420 C4480B40 */  lwc1  $f8, 0xb40($v0)
/* 09BE14 7F069424 C46A0000 */  lwc1  $f10, ($v1)
/* 09BE18 7F069428 3C0C8007 */  lui   $t4, %hi(g_CurrentPlayer) # $t4, 0x8007
/* 09BE1C 7F06942C 46089101 */  sub.s $f4, $f18, $f8
/* 09BE20 7F069430 3C0D8007 */  lui   $t5, %hi(g_CurrentPlayer) # $t5, 0x8007
/* 09BE24 7F069434 460A2183 */  div.s $f6, $f4, $f10
/* 09BE28 7F069438 46067480 */  add.s $f18, $f14, $f6
/* 09BE2C 7F06943C E6120010 */  swc1  $f18, 0x10($s0)
/* 09BE30 7F069440 8D8C8BC0 */  lw    $t4, %lo(g_CurrentPlayer)($t4)
/* 09BE34 7F069444 C4660000 */  lwc1  $f6, ($v1)
/* 09BE38 7F069448 01911021 */  addu  $v0, $t4, $s1
/* 09BE3C 7F06944C C4480B04 */  lwc1  $f8, 0xb04($v0)
/* 09BE40 7F069450 C4440B44 */  lwc1  $f4, 0xb44($v0)
/* 09BE44 7F069454 46044281 */  sub.s $f10, $f8, $f4
/* 09BE48 7F069458 C6080014 */  lwc1  $f8, 0x14($s0)
/* 09BE4C 7F06945C 46065483 */  div.s $f18, $f10, $f6
/* 09BE50 7F069460 46124100 */  add.s $f4, $f8, $f18
/* 09BE54 7F069464 E6040014 */  swc1  $f4, 0x14($s0)
/* 09BE58 7F069468 8DAD8BC0 */  lw    $t5, %lo(g_CurrentPlayer)($t5)
/* 09BE5C 7F06946C C4720000 */  lwc1  $f18, ($v1)
/* 09BE60 7F069470 01B11021 */  addu  $v0, $t5, $s1
/* 09BE64 7F069474 C44A0B08 */  lwc1  $f10, 0xb08($v0)
/* 09BE68 7F069478 C4460B48 */  lwc1  $f6, 0xb48($v0)
/* 09BE6C 7F06947C 46065201 */  sub.s $f8, $f10, $f6
/* 09BE70 7F069480 46124103 */  div.s $f4, $f8, $f18
/* 09BE74 7F069484 46048280 */  add.s $f10, $f16, $f4
/* 09BE78 7F069488 E60A0018 */  swc1  $f10, 0x18($s0)
.L7F06948C:
/* 09BE7C 7F06948C 8FBF001C */  lw    $ra, 0x1c($sp)
.L7F069490:
/* 09BE80 7F069490 8FB00014 */  lw    $s0, 0x14($sp)
/* 09BE84 7F069494 8FB10018 */  lw    $s1, 0x18($sp)
/* 09BE88 7F069498 03E00008 */  jr    $ra
/* 09BE8C 7F06949C 27BD0108 */   addiu $sp, $sp, 0x108
)
#endif


#endif


void update_bullet_casing(CasingRecord* casing)
{
    f32 new_val_y;
    f32 delta;
    s32 i;
    struct player* current_player;

    delta = g_GlobalTimerDelta;
    new_val_y = casing->vel.y - (delta * 0.2777778f);

    casing->pos.y += delta * 0.5f * (casing->vel.y + new_val_y);

    if (casing->pos.y < casing->floor_y_pos)
    {
#if defined(BUGFIX_R1)
        if (dword_CODE_bss_80075DB0 == 0 && (g_ClockTimer > 0))
#else
        if (dword_CODE_bss_80075DB0 == 0)
#endif
        {
            if ((g_CurrentPlayer->hands[0].when_detonating_mines_is_0 != 2) && (g_CurrentPlayer->hands[1].when_detonating_mines_is_0 != 2))
            {
                // Play bullet casing rolling on floor sound
                sndPlaySfx((struct ALBankAlt_s* ) g_musicSfxBufferPtr, CART_SPENT_SFX, (ALSoundState* ) &dword_CODE_bss_80075DB0);
            }
        }

        // This casing is removed and not updated anymore
        casing->header = NULL;
        return;
    }

    casing->vel.y = new_val_y;
    casing->pos.x += delta * casing->vel.x;
    casing->pos.z += delta * casing->vel.z;

    for (i = 0; i < g_ClockTimer; i++)
    {
#if defined(VERSION_US) || defined(VERSION_JP)
        matrix_4x4_multiply_homogeneous_in_place(&casing->unk5C, &casing->unk1C);
#else
        matrix_4x4_multiply_homogeneous_in_place_eu(casing->unk40, casing->unk1C);
#endif
    }
}


void update_bullet_casings(void)
{
    CasingRecord* end = g_Casings + ARRAYCOUNT(g_Casings);
    CasingRecord* entry = g_Casings;
    while (entry < end)
    {
        if (entry->header)
        {
            update_bullet_casing(entry);
        }
        entry++;
    }
}


#ifdef NONMATCHING
void sub_GAME_7F068EC4(void) {

}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.late_rodata
glabel D_80054408
.word 0x3dccccce /*0.10000001*/
glabel D_8005440C
.word 0xc6ea6000 /*-30000.0*/
glabel D_80054410
.word 0x46ea6000 /*30000.0*/
.text
glabel sub_GAME_7F068EC4
/* 09D9F4 7F068EC4 27BDFF18 */  addiu $sp, $sp, -0xe8
/* 09D9F8 7F068EC8 AFBF001C */  sw    $ra, 0x1c($sp)
/* 09D9FC 7F068ECC AFB00018 */  sw    $s0, 0x18($sp)
/* 09DA00 7F068ED0 AFA400E8 */  sw    $a0, 0xe8($sp)
/* 09DA04 7F068ED4 AFA500EC */  sw    $a1, 0xec($sp)
/* 09DA08 7F068ED8 8CAF0000 */  lw    $t7, ($a1)
/* 09DA0C 7F068EDC AFAF00E4 */  sw    $t7, 0xe4($sp)
/* 09DA10 7F068EE0 8C82009C */  lw    $v0, 0x9c($a0)
/* 09DA14 7F068EE4 8444000E */  lh    $a0, 0xe($v0)
/* 09DA18 7F068EE8 AFA200E0 */  sw    $v0, 0xe0($sp)
/* 09DA1C 7F068EEC 0004C980 */  sll   $t9, $a0, 6
/* 09DA20 7F068EF0 0FC2F5C5 */  jal   dynAllocate
/* 09DA24 7F068EF4 03202025 */   move  $a0, $t9
/* 09DA28 7F068EF8 3C098003 */  lui   $t1, %hi(D_80035EB0)
/* 09DA2C 7F068EFC 25295EB0 */  addiu $t1, %lo(D_80035EB0) # addiu $t1, $t1, 0x5eb0
/* 09DA30 7F068F00 AFA200DC */  sw    $v0, 0xdc($sp)
/* 09DA34 7F068F04 252C003C */  addiu $t4, $t1, 0x3c
/* 09DA38 7F068F08 27A8007C */  addiu $t0, $sp, 0x7c
.L7F068F0C:
/* 09DA3C 7F068F0C 8D210000 */  lw    $at, ($t1)
/* 09DA40 7F068F10 2529000C */  addiu $t1, $t1, 0xc
/* 09DA44 7F068F14 2508000C */  addiu $t0, $t0, 0xc
/* 09DA48 7F068F18 AD01FFF4 */  sw    $at, -0xc($t0)
/* 09DA4C 7F068F1C 8D21FFF8 */  lw    $at, -8($t1)
/* 09DA50 7F068F20 AD01FFF8 */  sw    $at, -8($t0)
/* 09DA54 7F068F24 8D21FFFC */  lw    $at, -4($t1)
/* 09DA58 7F068F28 152CFFF8 */  bne   $t1, $t4, .L7F068F0C
/* 09DA5C 7F068F2C AD01FFFC */   sw    $at, -4($t0)
/* 09DA60 7F068F30 8D210000 */  lw    $at, ($t1)
/* 09DA64 7F068F34 24100001 */  li    $s0, 1
/* 09DA68 7F068F38 AD010000 */  sw    $at, ($t0)
/* 09DA6C 7F068F3C 0FC1D73D */  jal   modelCalculateRwDataLen
/* 09DA70 7F068F40 8FA400E0 */   lw    $a0, 0xe0($sp)
/* 09DA74 7F068F44 27A400BC */  addiu $a0, $sp, 0xbc
/* 09DA78 7F068F48 8FA500E0 */  lw    $a1, 0xe0($sp)
/* 09DA7C 7F068F4C 0FC1D7DA */  jal   modelInit
/* 09DA80 7F068F50 00003025 */   move  $a2, $zero
/* 09DA84 7F068F54 8FAD00DC */  lw    $t5, 0xdc($sp)
/* 09DA88 7F068F58 8FA400E8 */  lw    $a0, 0xe8($sp)
/* 09DA8C 7F068F5C 27A5003C */  addiu $a1, $sp, 0x3c
/* 09DA90 7F068F60 AFAD00C8 */  sw    $t5, 0xc8($sp)
/* 09DA94 7F068F64 0FC16008 */  jal   matrix_4x4_copy
/* 09DA98 7F068F68 2484001C */   addiu $a0, $a0, 0x1c
/* 09DA9C 7F068F6C 3C018005 */  lui   $at, %hi(D_80054408)
/* 09DAA0 7F068F70 C42C4408 */  lwc1  $f12, %lo(D_80054408)($at)
/* 09DAA4 7F068F74 0FC1629F */  jal   matrix_scalar_multiply
/* 09DAA8 7F068F78 27A5003C */   addiu $a1, $sp, 0x3c
/* 09DAAC 7F068F7C 8FA400E8 */  lw    $a0, 0xe8($sp)
/* 09DAB0 7F068F80 27A5003C */  addiu $a1, $sp, 0x3c
/* 09DAB4 7F068F84 0FC16266 */  jal   matrix_4x4_set_position
/* 09DAB8 7F068F88 24840004 */   addiu $a0, $a0, 4
/* 09DABC 7F068F8C 0FC1E0F1 */  jal   camGetWorldToScreenMtxf
/* 09DAC0 7F068F90 00000000 */   nop
/* 09DAC4 7F068F94 00402025 */  move  $a0, $v0
/* 09DAC8 7F068F98 27A5003C */  addiu $a1, $sp, 0x3c
/* 09DACC 7F068F9C 0FC16063 */  jal   matrix_4x4_multiply_homogeneous
/* 09DAD0 7F068FA0 8FA600C8 */   lw    $a2, 0xc8($sp)
/* 09DAD4 7F068FA4 3C018005 */  lui   $at, %hi(D_8005440C)
/* 09DAD8 7F068FA8 C42C440C */  lwc1  $f12, %lo(D_8005440C)($at)
/* 09DADC 7F068FAC 3C018005 */  lui   $at, %hi(D_80054410)
/* 09DAE0 7F068FB0 C4224410 */  lwc1  $f2, %lo(D_80054410)($at)
/* 09DAE4 7F068FB4 00001025 */  move  $v0, $zero
/* 09DAE8 7F068FB8 8FA300C8 */  lw    $v1, 0xc8($sp)
/* 09DAEC 7F068FBC 2404000C */  li    $a0, 12
.L7F068FC0:
/* 09DAF0 7F068FC0 C4600030 */  lwc1  $f0, 0x30($v1)
/* 09DAF4 7F068FC4 24420004 */  addiu $v0, $v0, 4
/* 09DAF8 7F068FC8 4600103C */  c.lt.s $f2, $f0
/* 09DAFC 7F068FCC 00000000 */  nop
/* 09DB00 7F068FD0 45020004 */  bc1fl .L7F068FE4
/* 09DB04 7F068FD4 460C003C */   c.lt.s $f0, $f12
/* 09DB08 7F068FD8 10000006 */  b     .L7F068FF4
/* 09DB0C 7F068FDC 00008025 */   move  $s0, $zero
/* 09DB10 7F068FE0 460C003C */  c.lt.s $f0, $f12
.L7F068FE4:
/* 09DB14 7F068FE4 00000000 */  nop
/* 09DB18 7F068FE8 45000002 */  bc1f  .L7F068FF4
/* 09DB1C 7F068FEC 00000000 */   nop
/* 09DB20 7F068FF0 00008025 */  move  $s0, $zero
.L7F068FF4:
/* 09DB24 7F068FF4 1444FFF2 */  bne   $v0, $a0, .L7F068FC0
/* 09DB28 7F068FF8 24630004 */   addiu $v1, $v1, 4
/* 09DB2C 7F068FFC 1200001E */  beqz  $s0, .L7F069078
/* 09DB30 7F069000 24180004 */   li    $t8, 4
/* 09DB34 7F069004 8FAE00E4 */  lw    $t6, 0xe4($sp)
/* 09DB38 7F069008 8FAF00DC */  lw    $t7, 0xdc($sp)
/* 09DB3C 7F06900C 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09DB40 7F069010 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09DB44 7F069014 AFA00080 */  sw    $zero, 0x80($sp)
/* 09DB48 7F069018 AFB800AC */  sw    $t8, 0xac($sp)
/* 09DB4C 7F06901C AFAE0088 */  sw    $t6, 0x88($sp)
/* 09DB50 7F069020 AFAF008C */  sw    $t7, 0x8c($sp)
/* 09DB54 7F069024 904B0FDC */  lbu   $t3, 0xfdc($v0)
/* 09DB58 7F069028 90490FDD */  lbu   $t1, 0xfdd($v0)
/* 09DB5C 7F06902C 90590FDF */  lbu   $t9, 0xfdf($v0)
/* 09DB60 7F069030 904E0FDE */  lbu   $t6, 0xfde($v0)
/* 09DB64 7F069034 000B5600 */  sll   $t2, $t3, 0x18
/* 09DB68 7F069038 00094400 */  sll   $t0, $t1, 0x10
/* 09DB6C 7F06903C 032A6025 */  or    $t4, $t9, $t2
/* 09DB70 7F069040 01886825 */  or    $t5, $t4, $t0
/* 09DB74 7F069044 000E7A00 */  sll   $t7, $t6, 8
/* 09DB78 7F069048 01AFC025 */  or    $t8, $t5, $t7
/* 09DB7C 7F06904C AFB800B0 */  sw    $t8, 0xb0($sp)
/* 09DB80 7F069050 27A4007C */  addiu $a0, $sp, 0x7c
/* 09DB84 7F069054 0FC1D1A1 */  jal   subdraw
/* 09DB88 7F069058 27A500BC */   addiu $a1, $sp, 0xbc
/* 09DB8C 7F06905C 8FAB0088 */  lw    $t3, 0x88($sp)
/* 09DB90 7F069060 8FB900EC */  lw    $t9, 0xec($sp)
/* 09DB94 7F069064 AF2B0000 */  sw    $t3, ($t9)
/* 09DB98 7F069068 8FAA00E0 */  lw    $t2, 0xe0($sp)
/* 09DB9C 7F06906C 8FA400DC */  lw    $a0, 0xdc($sp)
/* 09DBA0 7F069070 0FC22F52 */  jal   bondviewTransformManyPosToViewMatrix
/* 09DBA4 7F069074 8545000E */   lh    $a1, 0xe($t2)
.L7F069078:
/* 09DBA8 7F069078 8FBF001C */  lw    $ra, 0x1c($sp)
/* 09DBAC 7F06907C 8FB00018 */  lw    $s0, 0x18($sp)
/* 09DBB0 7F069080 27BD00E8 */  addiu $sp, $sp, 0xe8
/* 09DBB4 7F069084 03E00008 */  jr    $ra
/* 09DBB8 7F069088 00000000 */   nop
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.late_rodata
glabel D_80054408
.word 0x3dccccce /*0.10000001*/
glabel D_8005440C
.word 0xc6ea6000 /*-30000.0*/
glabel D_80054410
.word 0x46ea6000 /*30000.0*/
.text
glabel sub_GAME_7F068EC4
/* 09C048 7F069658 27BDFF18 */  addiu $sp, $sp, -0xe8
/* 09C04C 7F06965C AFBF001C */  sw    $ra, 0x1c($sp)
/* 09C050 7F069660 AFB00018 */  sw    $s0, 0x18($sp)
/* 09C054 7F069664 AFA400E8 */  sw    $a0, 0xe8($sp)
/* 09C058 7F069668 AFA500EC */  sw    $a1, 0xec($sp)
/* 09C05C 7F06966C 8CAF0000 */  lw    $t7, ($a1)
/* 09C060 7F069670 AFAF00E4 */  sw    $t7, 0xe4($sp)
/* 09C064 7F069674 8C820064 */  lw    $v0, 0x64($a0)
/* 09C068 7F069678 8444000E */  lh    $a0, 0xe($v0)
/* 09C06C 7F06967C AFA200E0 */  sw    $v0, 0xe0($sp)
/* 09C070 7F069680 0004C980 */  sll   $t9, $a0, 6
/* 09C074 7F069684 0FC2F2B1 */  jal   dynAllocate
/* 09C078 7F069688 03202025 */   move  $a0, $t9
/* 09C07C 7F06968C 3C098003 */  lui   $t1, %hi(D_80035EB0) # $t1, 0x8003
/* 09C080 7F069690 25291400 */  addiu $t1, %lo(D_80035EB0) # addiu $t1, $t1, 0x1400
/* 09C084 7F069694 AFA200DC */  sw    $v0, 0xdc($sp)
/* 09C088 7F069698 252C003C */  addiu $t4, $t1, 0x3c
/* 09C08C 7F06969C 27A8007C */  addiu $t0, $sp, 0x7c
.L7F0696A0:
/* 09C090 7F0696A0 8D210000 */  lw    $at, ($t1)
/* 09C094 7F0696A4 2529000C */  addiu $t1, $t1, 0xc
/* 09C098 7F0696A8 2508000C */  addiu $t0, $t0, 0xc
/* 09C09C 7F0696AC AD01FFF4 */  sw    $at, -0xc($t0)
/* 09C0A0 7F0696B0 8D21FFF8 */  lw    $at, -8($t1)
/* 09C0A4 7F0696B4 AD01FFF8 */  sw    $at, -8($t0)
/* 09C0A8 7F0696B8 8D21FFFC */  lw    $at, -4($t1)
/* 09C0AC 7F0696BC 152CFFF8 */  bne   $t1, $t4, .L7F0696A0
/* 09C0B0 7F0696C0 AD01FFFC */   sw    $at, -4($t0)
/* 09C0B4 7F0696C4 8D210000 */  lw    $at, ($t1)
/* 09C0B8 7F0696C8 24100001 */  li    $s0, 1
/* 09C0BC 7F0696CC AD010000 */  sw    $at, ($t0)
/* 09C0C0 7F0696D0 0FC1D75F */  jal   modelCalculateRwDataLen
/* 09C0C4 7F0696D4 8FA400E0 */   lw    $a0, 0xe0($sp)
/* 09C0C8 7F0696D8 27A400BC */  addiu $a0, $sp, 0xbc
/* 09C0CC 7F0696DC 8FA500E0 */  lw    $a1, 0xe0($sp)
/* 09C0D0 7F0696E0 0FC1D7F9 */  jal   modelInit
/* 09C0D4 7F0696E4 00003025 */   move  $a2, $zero
/* 09C0D8 7F0696E8 8FAD00DC */  lw    $t5, 0xdc($sp)
/* 09C0DC 7F0696EC 8FA400E8 */  lw    $a0, 0xe8($sp)
/* 09C0E0 7F0696F0 27A5003C */  addiu $a1, $sp, 0x3c
/* 09C0E4 7F0696F4 AFAD00C8 */  sw    $t5, 0xc8($sp)
/* 09C0E8 7F0696F8 0FC160EF */  jal   matrix_4x4_copy_eu
/* 09C0EC 7F0696FC 2484001C */   addiu $a0, $a0, 0x1c
/* 09C0F0 7F069700 3C018005 */  lui   $at, %hi(D_80054408) # $at, 0x8005
/* 09C0F4 7F069704 C42CA548 */  lwc1  $f12, %lo(D_80054408)($at)
/* 09C0F8 7F069708 0FC163C9 */  jal   matrix_scalar_multiply
/* 09C0FC 7F06970C 27A5003C */   addiu $a1, $sp, 0x3c
/* 09C100 7F069710 8FA400E8 */  lw    $a0, 0xe8($sp)
/* 09C104 7F069714 27A5003C */  addiu $a1, $sp, 0x3c
/* 09C108 7F069718 0FC16390 */  jal   matrix_4x4_set_position
/* 09C10C 7F06971C 24840004 */   addiu $a0, $a0, 4
/* 09C110 7F069720 0FC1E111 */  jal   camGetWorldToScreenMtxf
/* 09C114 7F069724 00000000 */   nop
/* 09C118 7F069728 00402025 */  move  $a0, $v0
/* 09C11C 7F06972C 27A5003C */  addiu $a1, $sp, 0x3c
/* 09C120 7F069730 0FC1618D */  jal   matrix_4x4_multiply_homogeneous
/* 09C124 7F069734 8FA600C8 */   lw    $a2, 0xc8($sp)
/* 09C128 7F069738 3C018005 */  lui   $at, %hi(D_8005440C) # $at, 0x8005
/* 09C12C 7F06973C C42CA54C */  lwc1  $f12, %lo(D_8005440C)($at)
/* 09C130 7F069740 3C018005 */  lui   $at, %hi(D_80054410) # $at, 0x8005
/* 09C134 7F069744 C422A550 */  lwc1  $f2, %lo(D_80054410)($at)
/* 09C138 7F069748 00001025 */  move  $v0, $zero
/* 09C13C 7F06974C 8FA300C8 */  lw    $v1, 0xc8($sp)
/* 09C140 7F069750 2404000C */  li    $a0, 12
.L7F069754:
/* 09C144 7F069754 C4600030 */  lwc1  $f0, 0x30($v1)
/* 09C148 7F069758 24420004 */  addiu $v0, $v0, 4
/* 09C14C 7F06975C 4600103C */  c.lt.s $f2, $f0
/* 09C150 7F069760 00000000 */  nop
/* 09C154 7F069764 45020004 */  bc1fl .L7F069778
/* 09C158 7F069768 460C003C */   c.lt.s $f0, $f12
/* 09C15C 7F06976C 10000006 */  b     .L7F069788
/* 09C160 7F069770 00008025 */   move  $s0, $zero
/* 09C164 7F069774 460C003C */  c.lt.s $f0, $f12
.L7F069778:
/* 09C168 7F069778 00000000 */  nop
/* 09C16C 7F06977C 45000002 */  bc1f  .L7F069788
/* 09C170 7F069780 00000000 */   nop
/* 09C174 7F069784 00008025 */  move  $s0, $zero
.L7F069788:
/* 09C178 7F069788 1444FFF2 */  bne   $v0, $a0, .L7F069754
/* 09C17C 7F06978C 24630004 */   addiu $v1, $v1, 4
/* 09C180 7F069790 1200001E */  beqz  $s0, .L7F06980C
/* 09C184 7F069794 24180004 */   li    $t8, 4
/* 09C188 7F069798 8FAE00E4 */  lw    $t6, 0xe4($sp)
/* 09C18C 7F06979C 8FAF00DC */  lw    $t7, 0xdc($sp)
/* 09C190 7F0697A0 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09C194 7F0697A4 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09C198 7F0697A8 AFA00080 */  sw    $zero, 0x80($sp)
/* 09C19C 7F0697AC AFB800AC */  sw    $t8, 0xac($sp)
/* 09C1A0 7F0697B0 AFAE0088 */  sw    $t6, 0x88($sp)
/* 09C1A4 7F0697B4 AFAF008C */  sw    $t7, 0x8c($sp)
/* 09C1A8 7F0697B8 904B0FD4 */  lbu   $t3, 0xfd4($v0)
/* 09C1AC 7F0697BC 90490FD5 */  lbu   $t1, 0xfd5($v0)
/* 09C1B0 7F0697C0 90590FD7 */  lbu   $t9, 0xfd7($v0)
/* 09C1B4 7F0697C4 904E0FD6 */  lbu   $t6, 0xfd6($v0)
/* 09C1B8 7F0697C8 000B5600 */  sll   $t2, $t3, 0x18
/* 09C1BC 7F0697CC 00094400 */  sll   $t0, $t1, 0x10
/* 09C1C0 7F0697D0 032A6025 */  or    $t4, $t9, $t2
/* 09C1C4 7F0697D4 01886825 */  or    $t5, $t4, $t0
/* 09C1C8 7F0697D8 000E7A00 */  sll   $t7, $t6, 8
/* 09C1CC 7F0697DC 01AFC025 */  or    $t8, $t5, $t7
/* 09C1D0 7F0697E0 AFB800B0 */  sw    $t8, 0xb0($sp)
/* 09C1D4 7F0697E4 27A4007C */  addiu $a0, $sp, 0x7c
/* 09C1D8 7F0697E8 0FC1D1D6 */  jal   subdraw
/* 09C1DC 7F0697EC 27A500BC */   addiu $a1, $sp, 0xbc
/* 09C1E0 7F0697F0 8FAB0088 */  lw    $t3, 0x88($sp)
/* 09C1E4 7F0697F4 8FB900EC */  lw    $t9, 0xec($sp)
/* 09C1E8 7F0697F8 AF2B0000 */  sw    $t3, ($t9)
/* 09C1EC 7F0697FC 8FAA00E0 */  lw    $t2, 0xe0($sp)
/* 09C1F0 7F069800 8FA400DC */  lw    $a0, 0xdc($sp)
/* 09C1F4 7F069804 0FC2300F */  jal   bondviewTransformManyPosToViewMatrix
/* 09C1F8 7F069808 8545000E */   lh    $a1, 0xe($t2)
.L7F06980C:
/* 09C1FC 7F06980C 8FBF001C */  lw    $ra, 0x1c($sp)
/* 09C200 7F069810 8FB00018 */  lw    $s0, 0x18($sp)
/* 09C204 7F069814 27BD00E8 */  addiu $sp, $sp, 0xe8
/* 09C208 7F069818 03E00008 */  jr    $ra
/* 09C20C 7F06981C 00000000 */   nop
)
#endif
#endif


void sub_GAME_7F06908C(Gfx** arg0)
{
    CasingRecord* end = g_Casings + ARRAYCOUNT(g_Casings);
    CasingRecord* entry = g_Casings;
    while (entry < end)
    {
        if (entry->header)
        {
            sub_GAME_7F068EC4(entry, arg0);
        }
        entry++;
    }
}


void gunSetGunAmmoVisible(s32 reason, bool enable) {

	if (enable)
    {
		g_CurrentPlayer->gunammooff &= ~reason;
		return;
	}

	g_CurrentPlayer->gunammooff |= reason;
}



void give_cur_player_ammo(s32 ammo_type, s32 ammo_amount) {
    enum ITEM_IDS weapon_id;
    s32 max_ammo;

    weapon_id = getCurrentPlayerWeaponId(GUNRIGHT);
    if ((get_ammo_type_for_weapon(weapon_id) == ammo_type) && (bondwalkItemCheckBitflags(weapon_id, WEAPONSTATBITFLAG_AMMO_CLIP_LIMIT) != 0))
    {
        g_CurrentPlayer->hands[0].weapon_ammo_in_magazine += ammo_amount;
        if (get_ptr_item_statistics(weapon_id)->MagSize < g_CurrentPlayer->hands[0].weapon_ammo_in_magazine)
        {
            g_CurrentPlayer->hands[0].weapon_ammo_in_magazine = (s32) get_ptr_item_statistics(weapon_id)->MagSize;
        }
        g_CurrentPlayer->ammoheldarr[ammo_type] = 0;
        return;
    }

    max_ammo = ammo_related[ammo_type].MaxAmmo;
    if (max_ammo < ammo_amount)
    {
        g_CurrentPlayer->ammoheldarr[ammo_type] = max_ammo;
        return;
    }

    g_CurrentPlayer->ammoheldarr[ammo_type] = ammo_amount;
}




s32 check_cur_player_ammo_amount_in_inventory(AMMOTYPE ammotype) {
    return g_CurrentPlayer->ammoheldarr[ammotype];
}

s32 currentPlayerGetAmmoCount(AMMOTYPE ammotype) {

    s32 total_ammo = check_cur_player_ammo_amount_in_inventory(ammotype);

    if (get_ammo_type_for_weapon(getCurrentPlayerWeaponId(GUNRIGHT)) == ammotype) {
        total_ammo += get_ammo_in_hands_magazine(GUNRIGHT);
    }

    if (get_ammo_type_for_weapon(getCurrentPlayerWeaponId(GUNLEFT)) == ammotype) {
        total_ammo += get_ammo_in_hands_magazine(GUNLEFT);
    }

    return total_ammo;
}



s32 get_max_ammo_for_type(s32 arg0)
{
    return ammo_related[arg0].MaxAmmo;
}




void set_max_ammo_for_cur_player(void)
{
    s32 ammo_type;

    for (ammo_type = 0; ammo_type < AMMO_RELATED_MAX; ammo_type++)
    {
        give_cur_player_ammo(ammo_type, ammo_related[ammo_type].MaxAmmo);
    }
}



s32 get_ammo_in_hands_magazine(GUNHAND hand) {
    return g_CurrentPlayer->hands[hand].weapon_ammo_in_magazine;
}



s32 get_ammo_in_hands_weapon(enum GUNHAND hand)
{
    s32 weapon_id;
    s32 ammo_count;

    weapon_id = getCurrentPlayerWeaponId(hand);
    ammo_count = get_ammo_count_for_weapon(weapon_id);

    if ((weapon_id == ITEM_SHOTGUN) || (weapon_id == ITEM_AUTOSHOT))
    {
        s32 other_weapon_id;
        other_weapon_id = getCurrentPlayerWeaponId(1 - hand);

        if ((other_weapon_id == ITEM_SHOTGUN) || (other_weapon_id == ITEM_AUTOSHOT))
        {
            return ammo_count - g_CurrentPlayer->hands[1 - hand].field_8A4;
        }

        /* I don't know why there's an extra return here, but it's needed to match */
        return ammo_count;
    }

    return ammo_count;
}



s32 get_ammo_type_for_weapon(ITEM_IDS weapon) {
    return get_ptr_item_statistics(weapon)->AmmoType;
}

s32 get_ammo_count_for_weapon(ITEM_IDS weapon) {
  WeaponStats *weaponstats = get_ptr_item_statistics(weapon);
  return g_CurrentPlayer->ammoheldarr[weaponstats->AmmoType];
}

void add_ammo_to_weapon(ITEM_IDS weapon, s32 ammo) {
    give_cur_player_ammo(get_ptr_item_statistics(weapon)->AmmoType, ammo);
}

s32 get_max_ammo_for_weapon(enum ITEM_IDS weapon)
{
    return ammo_related[get_ptr_item_statistics(weapon)->AmmoType].MaxAmmo;
}



#ifdef NONMATCHING
void *microcode_generation_ammo_related(void *arg0, void *arg1, f32 arg2, f32 arg3, f32 arg4, s32 arg5, f32 arg6, s32 arg7, ?32 arg8, ?32 arg9, ?32 argA, ?32 argB) {
    f32 spA8;
    f32 spAC;
    f32 spB0;
    f32 spB4;
    f32 temp_f4;
    f32 temp_f4_2;
    f32 temp_f18;
    f32 temp_f10;
    f32 temp_f6;
    f32 temp_f18_2;
    f32 temp_f16;
    s32 phi_t9;
    f32 phi_f4;
    f32 phi_f18;
    s32 phi_t2;
    f32 phi_f18_2;
    f32 phi_f16;
    ? phi_a2;

    // Node 0
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0xc0;
    *arg0 = 0xba000602; //gDPSetColorDither(glistp++, G_CD_MAGICSQ);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xba001301;//gDPSetTexturePersp(glistp++, G_TP_NONE);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xb9000002;
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xba001001;//gDPSetTextureLOD(glistp++, G_TL_TILE);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xba000c02;//gDPSetTextureFilter(glistp++, G_TF_POINT);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0xc00;
    *arg0 = 0xba000903;//gDPSetTextureConvert(glistp++, G_TC_CONV);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xba000e02;//gDPSetTextureLUT(glistp++, G_TT_NONE);
    phi_t9 = ((s32) arg1->unk4 >> 1);
    if (arg1->unk4 < 0)
    {
        // Node 1
        phi_t9 = ((s32) (arg1->unk4 + 1) >> 1);
    }
    // Node 2
    temp_f4 = (((f32) (u32) arg1->unk4 * 0.5f) - (f32) phi_t9);
    spB0 = temp_f4;
    if (arg5 != 0)
    {
        // Node 3
        spB0 = (f32) -temp_f4;
    }
    // Node 4
    spB0 = (f32) (spB0 + arg2);
    if (0.0f <= arg3)
    {
        // Node 5
        temp_f4_2 = (f32) arg1->unk5;
        phi_f4 = temp_f4_2;
        if (arg1->unk5 < 0)
        {
            // Node 6
            phi_f4 = (temp_f4_2 + M_U32_MAX_VALUE_F);
        }
        // Node 7
        spB4 = (f32) (arg3 - (phi_f4 * 0.5f));
    }
    else
    {
        // Node 8
        temp_f18 = (f32) arg1->unk5;
        phi_f18 = temp_f18;
        if (arg1->unk5 < 0)
        {
            // Node 9
            phi_f18 = (temp_f18 + M_U32_MAX_VALUE_F);
        }
        // Node 10
        phi_t2 = ((s32) arg1->unk5 >> 1);
        if (arg1->unk5 < 0)
        {
            // Node 11
            phi_t2 = ((s32) (arg1->unk5 + 1) >> 1);
        }
        // Node 12
        temp_f10 = ((phi_f18 * 0.5f) - (f32) phi_t2);
        temp_f6 = (arg4 - temp_f10);
        spB4 = (f32) -temp_f10;
        spB4 = temp_f6;
        spB4 = (f32) (temp_f6 + arg6);
    }
    // Node 13
    temp_f18_2 = (f32) arg1->unk4;
    phi_f18_2 = temp_f18_2;
    if (arg1->unk4 < 0)
    {
        // Node 14
        phi_f18_2 = (temp_f18_2 + M_U32_MAX_VALUE_F);
    }
    // Node 15
    spA8 = (f32) (phi_f18_2 * 0.5f);
    temp_f16 = (f32) arg1->unk5;
    phi_f16 = temp_f16;
    if (arg1->unk5 < 0)
    {
        // Node 16
        phi_f16 = (temp_f16 + M_U32_MAX_VALUE_F);
    }
    // Node 17
    arg0 = (void *) (arg0 + 8);
    spAC = (f32) (phi_f16 * 0.5f);
    arg0->unk4 = 0;
    *arg0 = 0xe7000000;//gDPPipeSync(glistp++);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xba001402;//gDPSetCyceType(glistp++, G_CYC_1CYCLE);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0x504240;
    *arg0 = 0xb900031d;//gDPSetRenderMode(glistp++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0xfffdf6fb;
    *arg0 = 0xfcffffff;//gDPSetCombineMode(glistp++, PRIMITIVE, PRIMITIVE2);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xfa000000;
    arg0 = (void *) (arg0 + 8);
    *arg0 = (s32) ((((((s32) (spAC + spB4) + 1) & 0x3ff) * 4) | 0xf6000000) | ((((s32) (spB0 + spA8) + 1) & 0x3ff) << 0xe));
    arg0->unk4 = (s32) (((((s32) (spB4 - spAC) + -1) & 0x3ff) * 4) | ((((s32) (spB0 - spA8) + -1) & 0x3ff) << 0xe));
    phi_a2 = 1;
    if (arg7 != 0)
    {
        // Node 18
        phi_a2 = 2;
    }
    // Node 19
    texSelect(arg3, arg2, &arg0, arg1, phi_a2, 0, 0);
    display_image_at_position(&arg0, &spB0, &spA8, arg1->unk4, (s32) arg1->unk5, 0, 0, 1, arg8, arg9, argA, argB, (s32) (0 < arg1->unk6), 0);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xe7000000;//gDPPipeSync(glistp++);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0x40;
    *arg0 = 0xba000602;//gDPSetColorDither(glistp++, G_CD_BAYER);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0x80000;
    *arg0 = 0xba001301;//gDPSetTexturePersp(glistp++, G_TP_PERSP);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xb9000002;
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0x10000;
    *arg0 = 0xba001001;//gDPSetTextureLOD(glistp++, G_TL_LOD);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0x2000;
    *arg0 = 0xba000c02;//gDPSetTextureFilter(glistp++, G_TF_BILERP);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0xc00;
    *arg0 = 0xba000903;//gDPSetTextureConvert(glistp++, G_TC_FILT);
    arg0 = (void *) (arg0 + 8);
    arg0->unk4 = 0;
    *arg0 = 0xba000e02;
    return arg0;
}

#else
GLOBAL_ASM(
.text
glabel microcode_generation_ammo_related
/* 09E018 7F0694E8 27BDFF48 */  addiu $sp, $sp, -0xb8
/* 09E01C 7F0694EC AFA400B8 */  sw    $a0, 0xb8($sp)
/* 09E020 7F0694F0 248F0008 */  addiu $t7, $a0, 8
/* 09E024 7F0694F4 AFBF0044 */  sw    $ra, 0x44($sp)
/* 09E028 7F0694F8 AFB00040 */  sw    $s0, 0x40($sp)
/* 09E02C 7F0694FC AFAF00B8 */  sw    $t7, 0xb8($sp)
/* 09E030 7F069500 3C18BA00 */  lui   $t8, (0xBA000602 >> 16) # lui $t8, 0xba00
/* 09E034 7F069504 37180602 */  ori   $t8, (0xBA000602 & 0xFFFF) # ori $t8, $t8, 0x602
/* 09E038 7F069508 241900C0 */  li    $t9, 192
/* 09E03C 7F06950C AC990004 */  sw    $t9, 4($a0)
/* 09E040 7F069510 AC980000 */  sw    $t8, ($a0)
/* 09E044 7F069514 8FA800B8 */  lw    $t0, 0xb8($sp)
/* 09E048 7F069518 3C0ABA00 */  lui   $t2, (0xBA001301 >> 16) # lui $t2, 0xba00
/* 09E04C 7F06951C 354A1301 */  ori   $t2, (0xBA001301 & 0xFFFF) # ori $t2, $t2, 0x1301
/* 09E050 7F069520 25090008 */  addiu $t1, $t0, 8
/* 09E054 7F069524 AFA900B8 */  sw    $t1, 0xb8($sp)
/* 09E058 7F069528 AD000004 */  sw    $zero, 4($t0)
/* 09E05C 7F06952C AD0A0000 */  sw    $t2, ($t0)
/* 09E060 7F069530 8FAB00B8 */  lw    $t3, 0xb8($sp)
/* 09E064 7F069534 3C0DB900 */  lui   $t5, (0xB9000002 >> 16) # lui $t5, 0xb900
/* 09E068 7F069538 35AD0002 */  ori   $t5, (0xB9000002 & 0xFFFF) # ori $t5, $t5, 2
/* 09E06C 7F06953C 256C0008 */  addiu $t4, $t3, 8
/* 09E070 7F069540 AFAC00B8 */  sw    $t4, 0xb8($sp)
/* 09E074 7F069544 AD600004 */  sw    $zero, 4($t3)
/* 09E078 7F069548 AD6D0000 */  sw    $t5, ($t3)
/* 09E07C 7F06954C 8FAE00B8 */  lw    $t6, 0xb8($sp)
/* 09E080 7F069550 3C18BA00 */  lui   $t8, (0xBA001001 >> 16) # lui $t8, 0xba00
/* 09E084 7F069554 37181001 */  ori   $t8, (0xBA001001 & 0xFFFF) # ori $t8, $t8, 0x1001
/* 09E088 7F069558 25CF0008 */  addiu $t7, $t6, 8
/* 09E08C 7F06955C AFAF00B8 */  sw    $t7, 0xb8($sp)
/* 09E090 7F069560 ADC00004 */  sw    $zero, 4($t6)
/* 09E094 7F069564 ADD80000 */  sw    $t8, ($t6)
/* 09E098 7F069568 8FB900B8 */  lw    $t9, 0xb8($sp)
/* 09E09C 7F06956C 3C09BA00 */  lui   $t1, (0xBA000C02 >> 16) # lui $t1, 0xba00
/* 09E0A0 7F069570 35290C02 */  ori   $t1, (0xBA000C02 & 0xFFFF) # ori $t1, $t1, 0xc02
/* 09E0A4 7F069574 27280008 */  addiu $t0, $t9, 8
/* 09E0A8 7F069578 AFA800B8 */  sw    $t0, 0xb8($sp)
/* 09E0AC 7F06957C AF200004 */  sw    $zero, 4($t9)
/* 09E0B0 7F069580 AF290000 */  sw    $t1, ($t9)
/* 09E0B4 7F069584 8FAA00B8 */  lw    $t2, 0xb8($sp)
/* 09E0B8 7F069588 3C0CBA00 */  lui   $t4, (0xBA000903 >> 16) # lui $t4, 0xba00
/* 09E0BC 7F06958C 358C0903 */  ori   $t4, (0xBA000903 & 0xFFFF) # ori $t4, $t4, 0x903
/* 09E0C0 7F069590 254B0008 */  addiu $t3, $t2, 8
/* 09E0C4 7F069594 AFAB00B8 */  sw    $t3, 0xb8($sp)
/* 09E0C8 7F069598 240D0C00 */  li    $t5, 3072
/* 09E0CC 7F06959C AD4D0004 */  sw    $t5, 4($t2)
/* 09E0D0 7F0695A0 AD4C0000 */  sw    $t4, ($t2)
/* 09E0D4 7F0695A4 8FAE00B8 */  lw    $t6, 0xb8($sp)
/* 09E0D8 7F0695A8 3C18BA00 */  lui   $t8, (0xBA000E02 >> 16) # lui $t8, 0xba00
/* 09E0DC 7F0695AC 37180E02 */  ori   $t8, (0xBA000E02 & 0xFFFF) # ori $t8, $t8, 0xe02
/* 09E0E0 7F0695B0 25CF0008 */  addiu $t7, $t6, 8
/* 09E0E4 7F0695B4 AFAF00B8 */  sw    $t7, 0xb8($sp)
/* 09E0E8 7F0695B8 ADC00004 */  sw    $zero, 4($t6)
/* 09E0EC 7F0695BC ADD80000 */  sw    $t8, ($t6)
/* 09E0F0 7F0695C0 90A40004 */  lbu   $a0, 4($a1)
/* 09E0F4 7F0695C4 3C013F00 */  li    $at, 0x3F000000 # 0.500000
/* 09E0F8 7F0695C8 44867000 */  mtc1  $a2, $f14
/* 09E0FC 7F0695CC 44842000 */  mtc1  $a0, $f4
/* 09E100 7F0695D0 44876000 */  mtc1  $a3, $f12
/* 09E104 7F0695D4 44810000 */  mtc1  $at, $f0
/* 09E108 7F0695D8 00A08025 */  move  $s0, $a1
/* 09E10C 7F0695DC 04810005 */  bgez  $a0, .L7F0695F4
/* 09E110 7F0695E0 468021A0 */   cvt.s.w $f6, $f4
/* 09E114 7F0695E4 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09E118 7F0695E8 44814000 */  mtc1  $at, $f8
/* 09E11C 7F0695EC 00000000 */  nop
/* 09E120 7F0695F0 46083180 */  add.s $f6, $f6, $f8
.L7F0695F4:
/* 09E124 7F0695F4 46003282 */  mul.s $f10, $f6, $f0
/* 09E128 7F0695F8 04810003 */  bgez  $a0, .L7F069608
/* 09E12C 7F0695FC 0004C843 */   sra   $t9, $a0, 1
/* 09E130 7F069600 24810001 */  addiu $at, $a0, 1
/* 09E134 7F069604 0001C843 */  sra   $t9, $at, 1
.L7F069608:
/* 09E138 7F069608 44998000 */  mtc1  $t9, $f16
/* 09E13C 7F06960C 8FA800CC */  lw    $t0, 0xcc($sp)
/* 09E140 7F069610 3C0FE700 */  lui   $t7, 0xe700
/* 09E144 7F069614 468084A0 */  cvt.s.w $f18, $f16
/* 09E148 7F069618 27A400B8 */  addiu $a0, $sp, 0xb8
/* 09E14C 7F06961C 02002825 */  move  $a1, $s0
/* 09E150 7F069620 00003825 */  move  $a3, $zero
/* 09E154 7F069624 46125101 */  sub.s $f4, $f10, $f18
/* 09E158 7F069628 44805000 */  mtc1  $zero, $f10
/* 09E15C 7F06962C 11000003 */  beqz  $t0, .L7F06963C
/* 09E160 7F069630 E7A400B0 */   swc1  $f4, 0xb0($sp)
/* 09E164 7F069634 46002207 */  neg.s $f8, $f4
/* 09E168 7F069638 E7A800B0 */  swc1  $f8, 0xb0($sp)
.L7F06963C:
/* 09E16C 7F06963C C7A600B0 */  lwc1  $f6, 0xb0($sp)
/* 09E170 7F069640 460C503E */  c.le.s $f10, $f12
/* 09E174 7F069644 3C08BA00 */  lui   $t0, (0xBA001402 >> 16) # lui $t0, 0xba00
/* 09E178 7F069648 460E3400 */  add.s $f16, $f6, $f14
/* 09E17C 7F06964C 4500000D */  bc1f  .L7F069684
/* 09E180 7F069650 E7B000B0 */   swc1  $f16, 0xb0($sp)
/* 09E184 7F069654 92090005 */  lbu   $t1, 5($s0)
/* 09E188 7F069658 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09E18C 7F06965C 44899000 */  mtc1  $t1, $f18
/* 09E190 7F069660 05210004 */  bgez  $t1, .L7F069674
/* 09E194 7F069664 46809120 */   cvt.s.w $f4, $f18
/* 09E198 7F069668 44814000 */  mtc1  $at, $f8
/* 09E19C 7F06966C 00000000 */  nop
/* 09E1A0 7F069670 46082100 */  add.s $f4, $f4, $f8
.L7F069674:
/* 09E1A4 7F069674 46002182 */  mul.s $f6, $f4, $f0
/* 09E1A8 7F069678 46066401 */  sub.s $f16, $f12, $f6
/* 09E1AC 7F06967C 10000019 */  b     .L7F0696E4
/* 09E1B0 7F069680 E7B000B4 */   swc1  $f16, 0xb4($sp)
.L7F069684:
/* 09E1B4 7F069684 92020005 */  lbu   $v0, 5($s0)
/* 09E1B8 7F069688 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09E1BC 7F06968C 44825000 */  mtc1  $v0, $f10
/* 09E1C0 7F069690 04410004 */  bgez  $v0, .L7F0696A4
/* 09E1C4 7F069694 468054A0 */   cvt.s.w $f18, $f10
/* 09E1C8 7F069698 44814000 */  mtc1  $at, $f8
/* 09E1CC 7F06969C 00000000 */  nop
/* 09E1D0 7F0696A0 46089480 */  add.s $f18, $f18, $f8
.L7F0696A4:
/* 09E1D4 7F0696A4 46009102 */  mul.s $f4, $f18, $f0
/* 09E1D8 7F0696A8 04410003 */  bgez  $v0, .L7F0696B8
/* 09E1DC 7F0696AC 00025043 */   sra   $t2, $v0, 1
/* 09E1E0 7F0696B0 24410001 */  addiu $at, $v0, 1
/* 09E1E4 7F0696B4 00015043 */  sra   $t2, $at, 1
.L7F0696B8:
/* 09E1E8 7F0696B8 448A3000 */  mtc1  $t2, $f6
/* 09E1EC 7F0696BC C7B200C8 */  lwc1  $f18, 0xc8($sp)
/* 09E1F0 7F0696C0 46803420 */  cvt.s.w $f16, $f6
/* 09E1F4 7F0696C4 46102281 */  sub.s $f10, $f4, $f16
/* 09E1F8 7F0696C8 C7A400D0 */  lwc1  $f4, 0xd0($sp)
/* 09E1FC 7F0696CC 460A9181 */  sub.s $f6, $f18, $f10
/* 09E200 7F0696D0 46005207 */  neg.s $f8, $f10
/* 09E204 7F0696D4 46043400 */  add.s $f16, $f6, $f4
/* 09E208 7F0696D8 E7A800B4 */  swc1  $f8, 0xb4($sp)
/* 09E20C 7F0696DC E7A600B4 */  swc1  $f6, 0xb4($sp)
/* 09E210 7F0696E0 E7B000B4 */  swc1  $f16, 0xb4($sp)
.L7F0696E4:
/* 09E214 7F0696E4 920B0004 */  lbu   $t3, 4($s0)
/* 09E218 7F0696E8 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09E21C 7F0696EC 24060001 */  li    $a2, 1
/* 09E220 7F0696F0 448B4000 */  mtc1  $t3, $f8
/* 09E224 7F0696F4 05610004 */  bgez  $t3, .L7F069708
/* 09E228 7F0696F8 468044A0 */   cvt.s.w $f18, $f8
/* 09E22C 7F0696FC 44815000 */  mtc1  $at, $f10
/* 09E230 7F069700 00000000 */  nop
/* 09E234 7F069704 460A9480 */  add.s $f18, $f18, $f10
.L7F069708:
/* 09E238 7F069708 46009182 */  mul.s $f6, $f18, $f0
/* 09E23C 7F06970C 3C014F80 */  li    $at, 0x4F800000 # 4294967296.000000
/* 09E240 7F069710 E7A600A8 */  swc1  $f6, 0xa8($sp)
/* 09E244 7F069714 920C0005 */  lbu   $t4, 5($s0)
/* 09E248 7F069718 448C2000 */  mtc1  $t4, $f4
/* 09E24C 7F06971C 05810004 */  bgez  $t4, .L7F069730
/* 09E250 7F069720 46802420 */   cvt.s.w $f16, $f4
/* 09E254 7F069724 44814000 */  mtc1  $at, $f8
/* 09E258 7F069728 00000000 */  nop
/* 09E25C 7F06972C 46088400 */  add.s $f16, $f16, $f8
.L7F069730:
/* 09E260 7F069730 46008282 */  mul.s $f10, $f16, $f0
/* 09E264 7F069734 8FAD00B8 */  lw    $t5, 0xb8($sp)
/* 09E268 7F069738 35081402 */  ori   $t0, (0xBA001402 & 0xFFFF) # ori $t0, $t0, 0x1402
/* 09E26C 7F06973C 3C0BB900 */  lui   $t3, (0xB900031D >> 16) # lui $t3, 0xb900
/* 09E270 7F069740 25AE0008 */  addiu $t6, $t5, 8
/* 09E274 7F069744 AFAE00B8 */  sw    $t6, 0xb8($sp)
/* 09E278 7F069748 3C0C0050 */  lui   $t4, (0x00504240 >> 16) # lui $t4, 0x50
/* 09E27C 7F06974C E7AA00AC */  swc1  $f10, 0xac($sp)
/* 09E280 7F069750 ADA00004 */  sw    $zero, 4($t5)
/* 09E284 7F069754 ADAF0000 */  sw    $t7, ($t5)
/* 09E288 7F069758 8FB800B8 */  lw    $t8, 0xb8($sp)
/* 09E28C 7F06975C 358C4240 */  ori   $t4, (0x00504240 & 0xFFFF) # ori $t4, $t4, 0x4240
/* 09E290 7F069760 356B031D */  ori   $t3, (0xB900031D & 0xFFFF) # ori $t3, $t3, 0x31d
/* 09E294 7F069764 27190008 */  addiu $t9, $t8, 8
/* 09E298 7F069768 AFB900B8 */  sw    $t9, 0xb8($sp)
/* 09E29C 7F06976C AF000004 */  sw    $zero, 4($t8)
/* 09E2A0 7F069770 AF080000 */  sw    $t0, ($t8)
/* 09E2A4 7F069774 8FA900B8 */  lw    $t1, 0xb8($sp)
/* 09E2A8 7F069778 3C18FFFD */  lui   $t8, (0xFFFDF6FB >> 16) # lui $t8, 0xfffd
/* 09E2AC 7F06977C 3C0FFCFF */  lui   $t7, (0xFCFFFFFF >> 16) # lui $t7, 0xfcff
/* 09E2B0 7F069780 252A0008 */  addiu $t2, $t1, 8
/* 09E2B4 7F069784 AFAA00B8 */  sw    $t2, 0xb8($sp)
/* 09E2B8 7F069788 AD2C0004 */  sw    $t4, 4($t1)
/* 09E2BC 7F06978C AD2B0000 */  sw    $t3, ($t1)
/* 09E2C0 7F069790 8FAD00B8 */  lw    $t5, 0xb8($sp)
/* 09E2C4 7F069794 35EFFFFF */  ori   $t7, (0xFCFFFFFF & 0xFFFF) # ori $t7, $t7, 0xffff
/* 09E2C8 7F069798 3718F6FB */  ori   $t8, (0xFFFDF6FB & 0xFFFF) # ori $t8, $t8, 0xf6fb
/* 09E2CC 7F06979C 25AE0008 */  addiu $t6, $t5, 8
/* 09E2D0 7F0697A0 AFAE00B8 */  sw    $t6, 0xb8($sp)
/* 09E2D4 7F0697A4 ADB80004 */  sw    $t8, 4($t5)
/* 09E2D8 7F0697A8 ADAF0000 */  sw    $t7, ($t5)
/* 09E2DC 7F0697AC 8FB900B8 */  lw    $t9, 0xb8($sp)
/* 09E2E0 7F0697B0 3C09FA00 */  lui   $t1, 0xfa00
/* 09E2E4 7F0697B4 3C01F600 */  lui   $at, 0xf600
/* 09E2E8 7F0697B8 27280008 */  addiu $t0, $t9, 8
/* 09E2EC 7F0697BC AFA800B8 */  sw    $t0, 0xb8($sp)
/* 09E2F0 7F0697C0 AF200004 */  sw    $zero, 4($t9)
/* 09E2F4 7F0697C4 AF290000 */  sw    $t1, ($t9)
/* 09E2F8 7F0697C8 C7B200AC */  lwc1  $f18, 0xac($sp)
/* 09E2FC 7F0697CC C7A600B4 */  lwc1  $f6, 0xb4($sp)
/* 09E300 7F0697D0 C7AA00A8 */  lwc1  $f10, 0xa8($sp)
/* 09E304 7F0697D4 C7B000B0 */  lwc1  $f16, 0xb0($sp)
/* 09E308 7F0697D8 46069100 */  add.s $f4, $f18, $f6
/* 09E30C 7F0697DC 8FA200B8 */  lw    $v0, 0xb8($sp)
/* 09E310 7F0697E0 460A8480 */  add.s $f18, $f16, $f10
/* 09E314 7F0697E4 244B0008 */  addiu $t3, $v0, 8
/* 09E318 7F0697E8 AFAB00B8 */  sw    $t3, 0xb8($sp)
/* 09E31C 7F0697EC 4600220D */  trunc.w.s $f8, $f4
/* 09E320 7F0697F0 4600918D */  trunc.w.s $f6, $f18
/* 09E324 7F0697F4 440D4000 */  mfc1  $t5, $f8
/* 09E328 7F0697F8 44093000 */  mfc1  $t1, $f6
/* 09E32C 7F0697FC 25AE0001 */  addiu $t6, $t5, 1
/* 09E330 7F069800 31CF03FF */  andi  $t7, $t6, 0x3ff
/* 09E334 7F069804 252A0001 */  addiu $t2, $t1, 1
/* 09E338 7F069808 314B03FF */  andi  $t3, $t2, 0x3ff
/* 09E33C 7F06980C 000FC080 */  sll   $t8, $t7, 2
/* 09E340 7F069810 0301C825 */  or    $t9, $t8, $at
/* 09E344 7F069814 000B6380 */  sll   $t4, $t3, 0xe
/* 09E348 7F069818 032C6825 */  or    $t5, $t9, $t4
/* 09E34C 7F06981C AC4D0000 */  sw    $t5, ($v0)
/* 09E350 7F069820 C7A800AC */  lwc1  $f8, 0xac($sp)
/* 09E354 7F069824 C7A400B4 */  lwc1  $f4, 0xb4($sp)
/* 09E358 7F069828 C7A600A8 */  lwc1  $f6, 0xa8($sp)
/* 09E35C 7F06982C C7B200B0 */  lwc1  $f18, 0xb0($sp)
/* 09E360 7F069830 46082401 */  sub.s $f16, $f4, $f8
/* 09E364 7F069834 46069101 */  sub.s $f4, $f18, $f6
/* 09E368 7F069838 4600828D */  trunc.w.s $f10, $f16
/* 09E36C 7F06983C 4600220D */  trunc.w.s $f8, $f4
/* 09E370 7F069840 440F5000 */  mfc1  $t7, $f10
/* 09E374 7F069844 440B4000 */  mfc1  $t3, $f8
/* 09E378 7F069848 25F8FFFF */  addiu $t8, $t7, -1
/* 09E37C 7F06984C 330803FF */  andi  $t0, $t8, 0x3ff
/* 09E380 7F069850 2579FFFF */  addiu $t9, $t3, -1
/* 09E384 7F069854 332C03FF */  andi  $t4, $t9, 0x3ff
/* 09E388 7F069858 000C6B80 */  sll   $t5, $t4, 0xe
/* 09E38C 7F06985C 00084880 */  sll   $t1, $t0, 2
/* 09E390 7F069860 012D7025 */  or    $t6, $t1, $t5
/* 09E394 7F069864 AC4E0004 */  sw    $t6, 4($v0)
/* 09E398 7F069868 8FAF00D4 */  lw    $t7, 0xd4($sp)
/* 09E39C 7F06986C 11E00003 */  beqz  $t7, .L7F06987C
/* 09E3A0 7F069870 00000000 */   nop
/* 09E3A4 7F069874 10000001 */  b     .L7F06987C
/* 09E3A8 7F069878 24060002 */   li    $a2, 2
.L7F06987C:
/* 09E3AC 7F06987C 0FC1DB5A */  jal   texSelect
/* 09E3B0 7F069880 AFA00010 */   sw    $zero, 0x10($sp)
/* 09E3B4 7F069884 92180005 */  lbu   $t8, 5($s0)
/* 09E3B8 7F069888 92070004 */  lbu   $a3, 4($s0)
/* 09E3BC 7F06988C 8FAA00D8 */  lw    $t2, 0xd8($sp)
/* 09E3C0 7F069890 8FAB00DC */  lw    $t3, 0xdc($sp)
/* 09E3C4 7F069894 8FB900E0 */  lw    $t9, 0xe0($sp)
/* 09E3C8 7F069898 8FAC00E4 */  lw    $t4, 0xe4($sp)
/* 09E3CC 7F06989C 24080001 */  li    $t0, 1
/* 09E3D0 7F0698A0 AFA8001C */  sw    $t0, 0x1c($sp)
/* 09E3D4 7F0698A4 AFA00018 */  sw    $zero, 0x18($sp)
/* 09E3D8 7F0698A8 AFA00014 */  sw    $zero, 0x14($sp)
/* 09E3DC 7F0698AC AFB80010 */  sw    $t8, 0x10($sp)
/* 09E3E0 7F0698B0 AFAA0020 */  sw    $t2, 0x20($sp)
/* 09E3E4 7F0698B4 AFAB0024 */  sw    $t3, 0x24($sp)
/* 09E3E8 7F0698B8 AFB90028 */  sw    $t9, 0x28($sp)
/* 09E3EC 7F0698BC AFAC002C */  sw    $t4, 0x2c($sp)
/* 09E3F0 7F0698C0 92090006 */  lbu   $t1, 6($s0)
/* 09E3F4 7F0698C4 AFA00034 */  sw    $zero, 0x34($sp)
/* 09E3F8 7F0698C8 27A400B8 */  addiu $a0, $sp, 0xb8
/* 09E3FC 7F0698CC 0009682A */  slt   $t5, $zero, $t1
/* 09E400 7F0698D0 AFAD0030 */  sw    $t5, 0x30($sp)
/* 09E404 7F0698D4 27A500B0 */  addiu $a1, $sp, 0xb0
/* 09E408 7F0698D8 0FC1ABFA */  jal   display_image_at_position
/* 09E40C 7F0698DC 27A600A8 */   addiu $a2, $sp, 0xa8
/* 09E410 7F0698E0 8FAE00B8 */  lw    $t6, 0xb8($sp)
/* 09E414 7F0698E4 3C18E700 */  lui   $t8, 0xe700
/* 09E418 7F0698E8 3C0BBA00 */  lui   $t3, (0xBA000602 >> 16) # lui $t3, 0xba00
/* 09E41C 7F0698EC 25CF0008 */  addiu $t7, $t6, 8
/* 09E420 7F0698F0 AFAF00B8 */  sw    $t7, 0xb8($sp)
/* 09E424 7F0698F4 ADC00004 */  sw    $zero, 4($t6)
/* 09E428 7F0698F8 ADD80000 */  sw    $t8, ($t6)
/* 09E42C 7F0698FC 8FA800B8 */  lw    $t0, 0xb8($sp)
/* 09E430 7F069900 356B0602 */  ori   $t3, (0xBA000602 & 0xFFFF) # ori $t3, $t3, 0x602
/* 09E434 7F069904 24190040 */  li    $t9, 64
/* 09E438 7F069908 250A0008 */  addiu $t2, $t0, 8
/* 09E43C 7F06990C AFAA00B8 */  sw    $t2, 0xb8($sp)
/* 09E440 7F069910 AD190004 */  sw    $t9, 4($t0)
/* 09E444 7F069914 AD0B0000 */  sw    $t3, ($t0)
/* 09E448 7F069918 8FAC00B8 */  lw    $t4, 0xb8($sp)
/* 09E44C 7F06991C 3C0DBA00 */  lui   $t5, (0xBA001301 >> 16) # lui $t5, 0xba00
/* 09E450 7F069920 35AD1301 */  ori   $t5, (0xBA001301 & 0xFFFF) # ori $t5, $t5, 0x1301
/* 09E454 7F069924 25890008 */  addiu $t1, $t4, 8
/* 09E458 7F069928 AFA900B8 */  sw    $t1, 0xb8($sp)
/* 09E45C 7F06992C 3C0E0008 */  lui   $t6, 8
/* 09E460 7F069930 AD8E0004 */  sw    $t6, 4($t4)
/* 09E464 7F069934 AD8D0000 */  sw    $t5, ($t4)
/* 09E468 7F069938 8FAF00B8 */  lw    $t7, 0xb8($sp)
/* 09E46C 7F06993C 3C08B900 */  lui   $t0, (0xB9000002 >> 16) # lui $t0, 0xb900
/* 09E470 7F069940 35080002 */  ori   $t0, (0xB9000002 & 0xFFFF) # ori $t0, $t0, 2
/* 09E474 7F069944 25F80008 */  addiu $t8, $t7, 8
/* 09E478 7F069948 AFB800B8 */  sw    $t8, 0xb8($sp)
/* 09E47C 7F06994C ADE00004 */  sw    $zero, 4($t7)
/* 09E480 7F069950 ADE80000 */  sw    $t0, ($t7)
/* 09E484 7F069954 8FAA00B8 */  lw    $t2, 0xb8($sp)
/* 09E488 7F069958 3C19BA00 */  lui   $t9, (0xBA001001 >> 16) # lui $t9, 0xba00
/* 09E48C 7F06995C 37391001 */  ori   $t9, (0xBA001001 & 0xFFFF) # ori $t9, $t9, 0x1001
/* 09E490 7F069960 254B0008 */  addiu $t3, $t2, 8
/* 09E494 7F069964 AFAB00B8 */  sw    $t3, 0xb8($sp)
/* 09E498 7F069968 3C0C0001 */  lui   $t4, 1
/* 09E49C 7F06996C AD4C0004 */  sw    $t4, 4($t2)
/* 09E4A0 7F069970 AD590000 */  sw    $t9, ($t2)
/* 09E4A4 7F069974 8FA900B8 */  lw    $t1, 0xb8($sp)
/* 09E4A8 7F069978 3C0EBA00 */  lui   $t6, (0xBA000C02 >> 16) # lui $t6, 0xba00
/* 09E4AC 7F06997C 35CE0C02 */  ori   $t6, (0xBA000C02 & 0xFFFF) # ori $t6, $t6, 0xc02
/* 09E4B0 7F069980 252D0008 */  addiu $t5, $t1, 8
/* 09E4B4 7F069984 AFAD00B8 */  sw    $t5, 0xb8($sp)
/* 09E4B8 7F069988 240F2000 */  li    $t7, 8192
/* 09E4BC 7F06998C AD2F0004 */  sw    $t7, 4($t1)
/* 09E4C0 7F069990 AD2E0000 */  sw    $t6, ($t1)
/* 09E4C4 7F069994 8FB800B8 */  lw    $t8, 0xb8($sp)
/* 09E4C8 7F069998 3C0ABA00 */  lui   $t2, (0xBA000903 >> 16) # lui $t2, 0xba00
/* 09E4CC 7F06999C 354A0903 */  ori   $t2, (0xBA000903 & 0xFFFF) # ori $t2, $t2, 0x903
/* 09E4D0 7F0699A0 27080008 */  addiu $t0, $t8, 8
/* 09E4D4 7F0699A4 AFA800B8 */  sw    $t0, 0xb8($sp)
/* 09E4D8 7F0699A8 240B0C00 */  li    $t3, 3072
/* 09E4DC 7F0699AC AF0B0004 */  sw    $t3, 4($t8)
/* 09E4E0 7F0699B0 AF0A0000 */  sw    $t2, ($t8)
/* 09E4E4 7F0699B4 8FB900B8 */  lw    $t9, 0xb8($sp)
/* 09E4E8 7F0699B8 3C09BA00 */  lui   $t1, (0xBA000E02 >> 16) # lui $t1, 0xba00
/* 09E4EC 7F0699BC 35290E02 */  ori   $t1, (0xBA000E02 & 0xFFFF) # ori $t1, $t1, 0xe02
/* 09E4F0 7F0699C0 272C0008 */  addiu $t4, $t9, 8
/* 09E4F4 7F0699C4 AFAC00B8 */  sw    $t4, 0xb8($sp)
/* 09E4F8 7F0699C8 AF200004 */  sw    $zero, 4($t9)
/* 09E4FC 7F0699CC AF290000 */  sw    $t1, ($t9)
/* 09E500 7F0699D0 8FBF0044 */  lw    $ra, 0x44($sp)
/* 09E504 7F0699D4 8FB00040 */  lw    $s0, 0x40($sp)
/* 09E508 7F0699D8 8FA200B8 */  lw    $v0, 0xb8($sp)
/* 09E50C 7F0699DC 03E00008 */  jr    $ra
/* 09E510 7F0699E0 27BD00B8 */   addiu $sp, $sp, 0xb8
)
#endif





#ifdef NONMATCHING
void set_rgba_redirect_generate_microcode(? arg2, ? arg3, f32 arg4, ?32 arg5, f32 arg6, ?32 arg7) {
    // Node 0
    return microcode_generation_ammo_related(arg2, arg3, arg2, arg3, arg4, arg5, arg6, arg7, 0xff, 0xff, 0xff, 0xff);
}
#else
GLOBAL_ASM(
.text
glabel set_rgba_redirect_generate_microcode
/* 09E514 7F0699E4 27BDFFC8 */  addiu $sp, $sp, -0x38
/* 09E518 7F0699E8 44866000 */  mtc1  $a2, $f12
/* 09E51C 7F0699EC 44877000 */  mtc1  $a3, $f14
/* 09E520 7F0699F0 C7A40048 */  lwc1  $f4, 0x48($sp)
/* 09E524 7F0699F4 8FAE004C */  lw    $t6, 0x4c($sp)
/* 09E528 7F0699F8 C7A60050 */  lwc1  $f6, 0x50($sp)
/* 09E52C 7F0699FC 8FAF0054 */  lw    $t7, 0x54($sp)
/* 09E530 7F069A00 AFBF0034 */  sw    $ra, 0x34($sp)
/* 09E534 7F069A04 241800FF */  li    $t8, 255
/* 09E538 7F069A08 241900FF */  li    $t9, 255
/* 09E53C 7F069A0C 240800FF */  li    $t0, 255
/* 09E540 7F069A10 240900FF */  li    $t1, 255
/* 09E544 7F069A14 44066000 */  mfc1  $a2, $f12
/* 09E548 7F069A18 44077000 */  mfc1  $a3, $f14
/* 09E54C 7F069A1C AFA9002C */  sw    $t1, 0x2c($sp)
/* 09E550 7F069A20 AFA80028 */  sw    $t0, 0x28($sp)
/* 09E554 7F069A24 AFB90024 */  sw    $t9, 0x24($sp)
/* 09E558 7F069A28 AFB80020 */  sw    $t8, 0x20($sp)
/* 09E55C 7F069A2C E7A40010 */  swc1  $f4, 0x10($sp)
/* 09E560 7F069A30 AFAE0014 */  sw    $t6, 0x14($sp)
/* 09E564 7F069A34 E7A60018 */  swc1  $f6, 0x18($sp)
/* 09E568 7F069A38 0FC1A53A */  jal   microcode_generation_ammo_related
/* 09E56C 7F069A3C AFAF001C */   sw    $t7, 0x1c($sp)
/* 09E570 7F069A40 8FBF0034 */  lw    $ra, 0x34($sp)
/* 09E574 7F069A44 27BD0038 */  addiu $sp, $sp, 0x38
/* 09E578 7F069A48 03E00008 */  jr    $ra
/* 09E57C 7F069A4C 00000000 */   nop
)
#endif


Gfx *gunDrawHudString(Gfx *gdl, s8 *text, s32 x, s32 halign, s32 y, s32 valign, bool glow)
{
    s32 x1;
    s32 y1;
    s32 x2;
    s32 y2;
    s32 textheight;
    s32 textwidth;

    x1 = 0;
    y1 = 0;
    x2 = 0;
    y2 = 0;
    textwidth = 0;
    textheight = 0;

    textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);

    if (halign == HUDHALIGN_LEFT) { // left
		x2 = x + textwidth;
		x1 = x;
	} else if (halign == HUDHALIGN_RIGHT) { // right
		x1 = x - textwidth;
		x2 = x;
	} else if (halign == HUDHALIGN_MIDDLE) { // middle
		x2 = x + textwidth / 2;
		x1 = x2 - textwidth;
	}

    if (valign == HUDVALIGN_TOP) { // top
		y2 = y + textheight;
		y1 = y;
	} else if (valign == HUDVALIGN_BOTTOM) { // bottom
		y1 = y - textheight;
		y2 = y;
	} else if (valign == HUDVALIGN_MIDDLE) { // middle
		y2 = y + textheight / 2;
		y1 = y2 - textheight;
	}

    gdl = draw_blackbox_to_screen(gdl, &x1, &y1, &x2, &y2);

    if (glow) {
        gdl = textRenderOutlined(gdl, &x1, &y1, text, ptrFontBankGothicChars, ptrFontBankGothic, -1, 0x646464FF, (s32) viGetX(), viGetY(), 0, 0);
    } else {
        gdl = textRender(gdl, &x1, &y1, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF00B0, (s32) viGetX(), viGetY(), 0, 0);
    }

    return gdl;
}


Gfx *gunDrawHudInteger(Gfx *gdl, s32 value, s32 x, s32 halign, s32 y, s32 valign, bool glow)
{
    char buffer[12];
    sprintf(buffer, "%d\n", value);
    return gunDrawHudString(gdl, buffer, x, halign, y, valign, glow);
}

#ifdef NONMATCHING
s32 generate_ammo_total_microcode(s32 arg0) {
    void *sp28;
    s16 sp2C;
    s16 sp2E;
    f32 sp30;
    f32 sp34;
    s16 sp38;
    s32 sp3C;
    void *sp40;
    s32 sp44;
    void *sp48;
    s32 sp4C;
    s32 sp50;
    s32 sp54;
    s32 sp58;
    s32 sp5C;
    s32 sp60;
    s32 sp64;
    ? temp_ret;
    void *temp_v1;
    s32 temp_v1_3;
    s32 temp_t3;
    ? temp_ret_2;
    void *temp_v1_2;
    s32 temp_v1_4;
    s32 temp_t1;
    s32 phi_v1;
    s32 phi_t9;
    s32 phi_t0;
    s32 phi_v1_2;
    s32 phi_t3;
    s32 phi_t0_2;

    // Node 0
    if (g_CurrentPlayer->unk1064 == 0)
    {
        // Node 1
        if (g_CurrentPlayer->unk29C4 == 0)
        {
            // Node 2
            sp64 = getCurrentPlayerWeaponId(1);
            sp60 = getCurrentPlayerWeaponId(0);
            if (getPlayerCount() < 3)
            {
                // Node 3
                sp58 = 0x3b;
                sp54 = 0x3b;
            }
            else
            {
                // Node 4
                if ((get_cur_playernum() & 1) != 0)
                {
                    // Node 5
                    sp58 = 0x2b;
                    sp54 = 0x7f;
                }
                else
                {
                    // Node 6
                    sp58 = 0x3b;
                    sp54 = 0x6d;
                }
            }
            // Node 7
            if (sp60 != 0)
            {
                // Node 8
                temp_ret = get_ammo_type_for_weapon(sp60);
                sp5C = temp_ret;
                if (temp_ret != 0)
                {
                    // Node 9
                    if (g_CurrentPlayer->unk894 != 7)
                    {
                        // Node 10
                        if (g_CurrentPlayer->unk894 != 7)
                        {
                            // Node 11
                            if (bondwalkItemCheckBitflags(sp60, 0x80000) == 0)
                            {
                                // Node 12
                                temp_v1 = ((sp5C * 0xc) + &ammo_related);
                                sp44 = 5;
                                if (temp_v1->unk4 != 0)
                                {
                                    // Node 13
                                    sp28 = temp_v1;
                                    sp48 = (void *) (temp_v1->unk4 + globalbank_rdram_offset);
                                    sp30 = getPlayer_c_screenleft();
                                    sp34 = getPlayer_c_screenwidth();
                                    sp38 = viGetViewTop();
                                    arg0 = set_rgba_redirect_generate_microcode(arg0, sp48, ((sp34 + sp30) - (f32) sp54), 0xbf800000, (f32) ((viGetViewHeight() + sp38) + -0x14), 0, (f32) sp28->unk8, 1);
                                    sp44 = (s32) sp48->unk4;
                                }
                                // Node 14
                                arg0 = microcode_constructor(arg0);
                                if (bondwalkItemCheckBitflags(sp60, 0x400000) != 0)
                                {
                                    // Node 15
                                    sp4C = 0;
                                    temp_v1_3 = (g_CurrentPlayer->unk89C + (g_CurrentPlayer + (sp5C * 4))->unk1130);
                                    phi_v1 = temp_v1_3;
                                    if (sp64 == sp60)
                                    {
                                        // Node 16
                                        phi_v1 = (temp_v1_3 + g_CurrentPlayer->unkC44);
                                    }
                                    // Node 17
                                    sp50 = (s32) phi_v1;
                                }
                                else
                                {
                                    // Node 18
                                    sp4C = (s32) g_CurrentPlayer->unk89C;
                                    sp50 = (s32) (g_CurrentPlayer + (sp5C * 4))->unk1130;
                                }
                                // Node 19
                                if (bondwalkItemCheckBitflags(sp60, 0x400000) == 0)
                                {
                                    // Node 20
                                    sp2C = viGetViewLeft();
                                    sp2E = viGetViewWidth();
                                    sp38 = viGetViewTop();
                                    viGetViewHeight();
                                    phi_t9 = (sp44 >> 1);
                                    if (sp44 < 0)
                                    {
                                        // Node 21
                                        phi_t9 = ((s32) (sp44 + 1) >> 1);
                                    }
                                    // Node 22
                                    arg0 = gunDrawHudInteger(arg0, sp4C, ((((sp2E + sp2C) - sp54) - phi_t9) + -4), 0);
                                }
                                // Node 23
                                if ((sp50 > 0) || (bondwalkItemCheckBitflags(sp60, 0x400000) != 0))
                                {
                                    // Node 25
                                    sp2C = viGetViewLeft();
                                    sp2E = viGetViewWidth();
                                    sp38 = viGetViewTop();
                                    viGetViewHeight();
                                    temp_t3 = (sp44 + 1);
                                    phi_t0 = (temp_t3 >> 1);
                                    if (temp_t3 < 0)
                                    {
                                        // Node 26
                                        phi_t0 = ((s32) (temp_t3 + 1) >> 1);
                                    }
                                    // Node 27
                                    arg0 = gunDrawHudInteger(arg0, sp50, ((((sp2E + sp2C) - sp54) + phi_t0) + 3), 1);
                                }
                                else
                                {

                                }
                                // Node 28
                                arg0 = combiner_bayer_lod_perspective(arg0);
                            }
                        }
                    }
                }
            }
            // Node 29
            if (sp64 != 0)
            {
                // Node 30
                temp_ret_2 = get_ammo_type_for_weapon(sp64);
                sp5C = temp_ret_2;
                if (temp_ret_2 != 0)
                {
                    // Node 31
                    if (g_CurrentPlayer->unkC3C != 7)
                    {
                        // Node 32
                        if (g_CurrentPlayer->unkC3C != 7)
                        {
                            // Node 33
                            if (bondwalkItemCheckBitflags(sp64, 0x80000) == 0)
                            {
                                // Node 34
                                temp_v1_2 = ((sp5C * 0xc) + &ammo_related);
                                sp3C = 5;
                                if (temp_v1_2->unk4 != 0)
                                {
                                    // Node 35
                                    sp28 = temp_v1_2;
                                    sp40 = (void *) (temp_v1_2->unk4 + globalbank_rdram_offset);
                                    sp34 = getPlayer_c_screenleft();
                                    sp38 = viGetViewTop();
                                    viGetViewHeight();
                                    arg0 = set_rgba_redirect_generate_microcode(sp28->unk8, arg0, sp40, (sp34 + (f32) sp58), 0xbf800000, 1, 1);
                                    sp3C = (s32) sp40->unk4;
                                }
                                // Node 36
                                arg0 = microcode_constructor(arg0);
                                if (bondwalkItemCheckBitflags(sp64, 0x400000) != 0)
                                {
                                    // Node 37
                                    sp4C = 0;
                                    temp_v1_4 = (g_CurrentPlayer->unkC44 + (g_CurrentPlayer + (sp5C * 4))->unk1130);
                                    phi_v1_2 = temp_v1_4;
                                    if (sp64 == sp60)
                                    {
                                        // Node 38
                                        phi_v1_2 = (temp_v1_4 + g_CurrentPlayer->unk89C);
                                    }
                                    // Node 39
                                    sp50 = (s32) phi_v1_2;
                                }
                                else
                                {
                                    // Node 40
                                    sp4C = (s32) g_CurrentPlayer->unkC44;
                                    sp50 = (s32) (g_CurrentPlayer + (sp5C * 4))->unk1130;
                                }
                                // Node 41
                                if (bondwalkItemCheckBitflags(sp64, 0x400000) == 0)
                                {
                                    // Node 42
                                    sp2E = viGetViewLeft();
                                    sp38 = viGetViewTop();
                                    viGetViewHeight();
                                    phi_t3 = (sp3C >> 1);
                                    if (sp3C < 0)
                                    {
                                        // Node 43
                                        phi_t3 = ((s32) (sp3C + 1) >> 1);
                                    }
                                    // Node 44
                                    arg0 = gunDrawHudInteger(arg0, sp4C, (((sp2E + sp58) + phi_t3) + 3), 1);
                                }
                                // Node 45
                                if ((sp50 > 0) || (bondwalkItemCheckBitflags(sp64, 0x400000) != 0))
                                {
                                    // Node 47
                                    sp2E = viGetViewLeft();
                                    sp38 = viGetViewTop();
                                    viGetViewHeight();
                                    temp_t1 = (sp3C + 1);
                                    phi_t0_2 = (temp_t1 >> 1);
                                    if (temp_t1 < 0)
                                    {
                                        // Node 48
                                        phi_t0_2 = ((s32) (temp_t1 + 1) >> 1);
                                    }
                                    // Node 49
                                    arg0 = gunDrawHudInteger(arg0, sp50, (((sp2E + sp58) - phi_t0_2) + -4), 0);
                                }
                                else
                                {

                                }
                                // Node 50
                                arg0 = combiner_bayer_lod_perspective(arg0);
                            }
                        }
                    }
                }
            }
        }
    }
    // Node 51
    return arg0;
}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.text
glabel generate_ammo_total_microcode
/* 09E824 7F069CF4 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09E828 7F069CF8 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09E82C 7F069CFC 27BDFF98 */  addiu $sp, $sp, -0x68
/* 09E830 7F069D00 AFBF0024 */  sw    $ra, 0x24($sp)
/* 09E834 7F069D04 AFA40068 */  sw    $a0, 0x68($sp)
/* 09E838 7F069D08 8C4E1064 */  lw    $t6, 0x1064($v0)
/* 09E83C 7F069D0C 55C00185 */  bnezl $t6, .L7F06A324
/* 09E840 7F069D10 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09E844 7F069D14 8C4F29C4 */  lw    $t7, 0x29c4($v0)
/* 09E848 7F069D18 55E00182 */  bnezl $t7, .L7F06A324
/* 09E84C 7F069D1C 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09E850 7F069D20 0FC17674 */  jal   getCurrentPlayerWeaponId
/* 09E854 7F069D24 24040001 */   li    $a0, 1
/* 09E858 7F069D28 AFA20064 */  sw    $v0, 0x64($sp)
/* 09E85C 7F069D2C 0FC17674 */  jal   getCurrentPlayerWeaponId
/* 09E860 7F069D30 00002025 */   move  $a0, $zero
/* 09E864 7F069D34 0FC26919 */  jal   getPlayerCount
/* 09E868 7F069D38 AFA20060 */   sw    $v0, 0x60($sp)
/* 09E86C 7F069D3C 28410003 */  slti  $at, $v0, 3
/* 09E870 7F069D40 10200005 */  beqz  $at, .L7F069D58
/* 09E874 7F069D44 2418003B */   li    $t8, 59
/* 09E878 7F069D48 2419003B */  li    $t9, 59
/* 09E87C 7F069D4C AFB80058 */  sw    $t8, 0x58($sp)
/* 09E880 7F069D50 1000000E */  b     .L7F069D8C
/* 09E884 7F069D54 AFB90054 */   sw    $t9, 0x54($sp)
.L7F069D58:
/* 09E888 7F069D58 0FC26C54 */  jal   get_cur_playernum
/* 09E88C 7F069D5C 00000000 */   nop
/* 09E890 7F069D60 30480001 */  andi  $t0, $v0, 1
/* 09E894 7F069D64 11000006 */  beqz  $t0, .L7F069D80
/* 09E898 7F069D68 240B003B */   li    $t3, 59
/* 09E89C 7F069D6C 2409002B */  li    $t1, 43
/* 09E8A0 7F069D70 240A007F */  li    $t2, 127
/* 09E8A4 7F069D74 AFA90058 */  sw    $t1, 0x58($sp)
/* 09E8A8 7F069D78 10000004 */  b     .L7F069D8C
/* 09E8AC 7F069D7C AFAA0054 */   sw    $t2, 0x54($sp)
.L7F069D80:
/* 09E8B0 7F069D80 240C006D */  li    $t4, 109
/* 09E8B4 7F069D84 AFAB0058 */  sw    $t3, 0x58($sp)
/* 09E8B8 7F069D88 AFAC0054 */  sw    $t4, 0x54($sp)
.L7F069D8C:
/* 09E8BC 7F069D8C 8FAD0060 */  lw    $t5, 0x60($sp)
/* 09E8C0 7F069D90 51A000B7 */  beql  $t5, $zero, .L7F06A070
/* 09E8C4 7F069D94 8FA40064 */   lw    $a0, 0x64($sp)
/* 09E8C8 7F069D98 0FC1A50B */  jal   get_ammo_type_for_weapon
/* 09E8CC 7F069D9C 01A02025 */   move  $a0, $t5
/* 09E8D0 7F069DA0 104000B2 */  beqz  $v0, .L7F06A06C
/* 09E8D4 7F069DA4 AFA2005C */   sw    $v0, 0x5c($sp)
/* 09E8D8 7F069DA8 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer)
/* 09E8DC 7F069DAC 8DCEA0B0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 09E8E0 7F069DB0 24010006 */  li    $at, 6
/* 09E8E4 7F069DB4 8DC20894 */  lw    $v0, 0x894($t6)
/* 09E8E8 7F069DB8 104100AC */  beq   $v0, $at, .L7F06A06C
/* 09E8EC 7F069DBC 24010007 */   li    $at, 7
/* 09E8F0 7F069DC0 104100AA */  beq   $v0, $at, .L7F06A06C
/* 09E8F4 7F069DC4 8FA40060 */   lw    $a0, 0x60($sp)
/* 09E8F8 7F069DC8 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09E8FC 7F069DCC 3C050008 */   lui   $a1, 8
/* 09E900 7F069DD0 144000A6 */  bnez  $v0, .L7F06A06C
/* 09E904 7F069DD4 8FAF005C */   lw    $t7, 0x5c($sp)
/* 09E908 7F069DD8 000FC080 */  sll   $t8, $t7, 2
/* 09E90C 7F069DDC 030FC023 */  subu  $t8, $t8, $t7
/* 09E910 7F069DE0 3C198003 */  lui   $t9, %hi(ammo_related)
/* 09E914 7F069DE4 27395EF0 */  addiu $t9, %lo(ammo_related) # addiu $t9, $t9, 0x5ef0
/* 09E918 7F069DE8 0018C080 */  sll   $t8, $t8, 2
/* 09E91C 7F069DEC 03191821 */  addu  $v1, $t8, $t9
/* 09E920 7F069DF0 8C620004 */  lw    $v0, 4($v1)
/* 09E924 7F069DF4 24080005 */  li    $t0, 5
/* 09E928 7F069DF8 AFA80044 */  sw    $t0, 0x44($sp)
/* 09E92C 7F069DFC 10400028 */  beqz  $v0, .L7F069EA0
/* 09E930 7F069E00 3C098009 */   lui   $t1, %hi(globalbank_rdram_offset)
/* 09E934 7F069E04 8D29D0B0 */  lw    $t1, %lo(globalbank_rdram_offset)($t1)
/* 09E938 7F069E08 AFA30028 */  sw    $v1, 0x28($sp)
/* 09E93C 7F069E0C 00491021 */  addu  $v0, $v0, $t1
/* 09E940 7F069E10 0FC1E131 */  jal   getPlayer_c_screenleft
/* 09E944 7F069E14 AFA20048 */   sw    $v0, 0x48($sp)
/* 09E948 7F069E18 0FC1E129 */  jal   getPlayer_c_screenwidth
/* 09E94C 7F069E1C E7A00030 */   swc1  $f0, 0x30($sp)
/* 09E950 7F069E20 0C001149 */  jal   viGetViewTop
/* 09E954 7F069E24 E7A00034 */   swc1  $f0, 0x34($sp)
/* 09E958 7F069E28 0C00112B */  jal   viGetViewHeight
/* 09E95C 7F069E2C A7A20038 */   sh    $v0, 0x38($sp)
/* 09E960 7F069E30 8FAA0054 */  lw    $t2, 0x54($sp)
/* 09E964 7F069E34 87AB0038 */  lh    $t3, 0x38($sp)
/* 09E968 7F069E38 C7A40034 */  lwc1  $f4, 0x34($sp)
/* 09E96C 7F069E3C C7A60030 */  lwc1  $f6, 0x30($sp)
/* 09E970 7F069E40 448A5000 */  mtc1  $t2, $f10
/* 09E974 7F069E44 004B6021 */  addu  $t4, $v0, $t3
/* 09E978 7F069E48 46062200 */  add.s $f8, $f4, $f6
/* 09E97C 7F069E4C 258DFFEC */  addiu $t5, $t4, -0x14
/* 09E980 7F069E50 448D2000 */  mtc1  $t5, $f4
/* 09E984 7F069E54 46805420 */  cvt.s.w $f16, $f10
/* 09E988 7F069E58 8FAE0028 */  lw    $t6, 0x28($sp)
/* 09E98C 7F069E5C AFA00014 */  sw    $zero, 0x14($sp)
/* 09E990 7F069E60 240F0001 */  li    $t7, 1
/* 09E994 7F069E64 8FA40068 */  lw    $a0, 0x68($sp)
/* 09E998 7F069E68 468021A0 */  cvt.s.w $f6, $f4
/* 09E99C 7F069E6C 8FA50048 */  lw    $a1, 0x48($sp)
/* 09E9A0 7F069E70 3C07BF80 */  lui   $a3, 0xbf80
/* 09E9A4 7F069E74 46104481 */  sub.s $f18, $f8, $f16
/* 09E9A8 7F069E78 E7A60010 */  swc1  $f6, 0x10($sp)
/* 09E9AC 7F069E7C C5CA0008 */  lwc1  $f10, 8($t6)
/* 09E9B0 7F069E80 AFAF001C */  sw    $t7, 0x1c($sp)
/* 09E9B4 7F069E84 44069000 */  mfc1  $a2, $f18
/* 09E9B8 7F069E88 0FC1A679 */  jal   set_rgba_redirect_generate_microcode
/* 09E9BC 7F069E8C E7AA0018 */   swc1  $f10, 0x18($sp)
/* 09E9C0 7F069E90 8FB80048 */  lw    $t8, 0x48($sp)
/* 09E9C4 7F069E94 AFA20068 */  sw    $v0, 0x68($sp)
/* 09E9C8 7F069E98 93190004 */  lbu   $t9, 4($t8)
/* 09E9CC 7F069E9C AFB90044 */  sw    $t9, 0x44($sp)
.L7F069EA0:
/* 09E9D0 7F069EA0 0FC2B366 */  jal   microcode_constructor
/* 09E9D4 7F069EA4 8FA40068 */   lw    $a0, 0x68($sp)
/* 09E9D8 7F069EA8 AFA20068 */  sw    $v0, 0x68($sp)
/* 09E9DC 7F069EAC 8FA40060 */  lw    $a0, 0x60($sp)
/* 09E9E0 7F069EB0 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09E9E4 7F069EB4 3C050040 */   lui   $a1, 0x40
/* 09E9E8 7F069EB8 10400011 */  beqz  $v0, .L7F069F00
/* 09E9EC 7F069EBC 3C050040 */   lui   $a1, 0x40
/* 09E9F0 7F069EC0 8FA9005C */  lw    $t1, 0x5c($sp)
/* 09E9F4 7F069EC4 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09E9F8 7F069EC8 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09E9FC 7F069ECC AFA0004C */  sw    $zero, 0x4c($sp)
/* 09EA00 7F069ED0 00095080 */  sll   $t2, $t1, 2
/* 09EA04 7F069ED4 8FAD0064 */  lw    $t5, 0x64($sp)
/* 09EA08 7F069ED8 8FAE0060 */  lw    $t6, 0x60($sp)
/* 09EA0C 7F069EDC 004A5821 */  addu  $t3, $v0, $t2
/* 09EA10 7F069EE0 8D6C1130 */  lw    $t4, 0x1130($t3)
/* 09EA14 7F069EE4 8C48089C */  lw    $t0, 0x89c($v0)
/* 09EA18 7F069EE8 15AE0003 */  bne   $t5, $t6, .L7F069EF8
/* 09EA1C 7F069EEC 010C1821 */   addu  $v1, $t0, $t4
/* 09EA20 7F069EF0 8C4F0C44 */  lw    $t7, 0xc44($v0)
/* 09EA24 7F069EF4 006F1821 */  addu  $v1, $v1, $t7
.L7F069EF8:
/* 09EA28 7F069EF8 1000000A */  b     .L7F069F24
/* 09EA2C 7F069EFC AFA30050 */   sw    $v1, 0x50($sp)
.L7F069F00:
/* 09EA30 7F069F00 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09EA34 7F069F04 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09EA38 7F069F08 8FB9005C */  lw    $t9, 0x5c($sp)
/* 09EA3C 7F069F0C 8C58089C */  lw    $t8, 0x89c($v0)
/* 09EA40 7F069F10 00194880 */  sll   $t1, $t9, 2
/* 09EA44 7F069F14 00495021 */  addu  $t2, $v0, $t1
/* 09EA48 7F069F18 AFB8004C */  sw    $t8, 0x4c($sp)
/* 09EA4C 7F069F1C 8D4B1130 */  lw    $t3, 0x1130($t2)
/* 09EA50 7F069F20 AFAB0050 */  sw    $t3, 0x50($sp)
.L7F069F24:
/* 09EA54 7F069F24 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09EA58 7F069F28 8FA40060 */   lw    $a0, 0x60($sp)
/* 09EA5C 7F069F2C 54400023 */  bnezl $v0, .L7F069FBC
/* 09EA60 7F069F30 8FAD0050 */   lw    $t5, 0x50($sp)
/* 09EA64 7F069F34 0C001145 */  jal   viGetViewLeft
/* 09EA68 7F069F38 00000000 */   nop
/* 09EA6C 7F069F3C 0C001127 */  jal   viGetViewWidth
/* 09EA70 7F069F40 A7A2002C */   sh    $v0, 0x2c($sp)
/* 09EA74 7F069F44 0C001149 */  jal   viGetViewTop
/* 09EA78 7F069F48 A7A2002E */   sh    $v0, 0x2e($sp)
/* 09EA7C 7F069F4C 0C00112B */  jal   viGetViewHeight
/* 09EA80 7F069F50 A7A20038 */   sh    $v0, 0x38($sp)
/* 09EA84 7F069F54 87A8002E */  lh    $t0, 0x2e($sp)
/* 09EA88 7F069F58 87AC002C */  lh    $t4, 0x2c($sp)
/* 09EA8C 7F069F5C 8FAE0054 */  lw    $t6, 0x54($sp)
/* 09EA90 7F069F60 8FB80044 */  lw    $t8, 0x44($sp)
/* 09EA94 7F069F64 87A90038 */  lh    $t1, 0x38($sp)
/* 09EA98 7F069F68 010C6821 */  addu  $t5, $t0, $t4
/* 09EA9C 7F069F6C 01AE7823 */  subu  $t7, $t5, $t6
/* 09EAA0 7F069F70 00495021 */  addu  $t2, $v0, $t1
/* 09EAA4 7F069F74 254BFFEE */  addiu $t3, $t2, -0x12
/* 09EAA8 7F069F78 240C0001 */  li    $t4, 1
/* 09EAAC 7F069F7C 24080002 */  li    $t0, 2
/* 09EAB0 7F069F80 AFA80014 */  sw    $t0, 0x14($sp)
/* 09EAB4 7F069F84 AFAC0018 */  sw    $t4, 0x18($sp)
/* 09EAB8 7F069F88 AFAB0010 */  sw    $t3, 0x10($sp)
/* 09EABC 7F069F8C 8FA40068 */  lw    $a0, 0x68($sp)
/* 09EAC0 7F069F90 8FA5004C */  lw    $a1, 0x4c($sp)
/* 09EAC4 7F069F94 07010003 */  bgez  $t8, .L7F069FA4
/* 09EAC8 7F069F98 0018C843 */   sra   $t9, $t8, 1
/* 09EACC 7F069F9C 27010001 */  addiu $at, $t8, 1
/* 09EAD0 7F069FA0 0001C843 */  sra   $t9, $at, 1
.L7F069FA4:
/* 09EAD4 7F069FA4 01F93023 */  subu  $a2, $t7, $t9
/* 09EAD8 7F069FA8 24C6FFFC */  addiu $a2, $a2, -4
/* 09EADC 7F069FAC 0FC1A723 */  jal   gunDrawHudInteger
/* 09EAE0 7F069FB0 00003825 */   move  $a3, $zero
/* 09EAE4 7F069FB4 AFA20068 */  sw    $v0, 0x68($sp)
/* 09EAE8 7F069FB8 8FAD0050 */  lw    $t5, 0x50($sp)
.L7F069FBC:
/* 09EAEC 7F069FBC 8FA40060 */  lw    $a0, 0x60($sp)
/* 09EAF0 7F069FC0 1DA00005 */  bgtz  $t5, .L7F069FD8
/* 09EAF4 7F069FC4 00000000 */   nop
/* 09EAF8 7F069FC8 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09EAFC 7F069FCC 3C050040 */   lui   $a1, 0x40
/* 09EB00 7F069FD0 10400023 */  beqz  $v0, .L7F06A060
/* 09EB04 7F069FD4 00000000 */   nop
.L7F069FD8:
/* 09EB08 7F069FD8 0C001145 */  jal   viGetViewLeft
/* 09EB0C 7F069FDC 00000000 */   nop
/* 09EB10 7F069FE0 0C001127 */  jal   viGetViewWidth
/* 09EB14 7F069FE4 A7A2002C */   sh    $v0, 0x2c($sp)
/* 09EB18 7F069FE8 0C001149 */  jal   viGetViewTop
/* 09EB1C 7F069FEC A7A2002E */   sh    $v0, 0x2e($sp)
/* 09EB20 7F069FF0 0C00112B */  jal   viGetViewHeight
/* 09EB24 7F069FF4 A7A20038 */   sh    $v0, 0x38($sp)
/* 09EB28 7F069FF8 87AE002E */  lh    $t6, 0x2e($sp)
/* 09EB2C 7F069FFC 87B8002C */  lh    $t8, 0x2c($sp)
/* 09EB30 7F06A000 8FB90054 */  lw    $t9, 0x54($sp)
/* 09EB34 7F06A004 8FAA0044 */  lw    $t2, 0x44($sp)
/* 09EB38 7F06A008 87AC0038 */  lh    $t4, 0x38($sp)
/* 09EB3C 7F06A00C 01D87821 */  addu  $t7, $t6, $t8
/* 09EB40 7F06A010 01F94823 */  subu  $t1, $t7, $t9
/* 09EB44 7F06A014 254B0001 */  addiu $t3, $t2, 1
/* 09EB48 7F06A018 004C6821 */  addu  $t5, $v0, $t4
/* 09EB4C 7F06A01C 25AEFFEE */  addiu $t6, $t5, -0x12
/* 09EB50 7F06A020 240F0001 */  li    $t7, 1
/* 09EB54 7F06A024 24180002 */  li    $t8, 2
/* 09EB58 7F06A028 AFB80014 */  sw    $t8, 0x14($sp)
/* 09EB5C 7F06A02C AFAF0018 */  sw    $t7, 0x18($sp)
/* 09EB60 7F06A030 AFAE0010 */  sw    $t6, 0x10($sp)
/* 09EB64 7F06A034 8FA40068 */  lw    $a0, 0x68($sp)
/* 09EB68 7F06A038 8FA50050 */  lw    $a1, 0x50($sp)
/* 09EB6C 7F06A03C 05610003 */  bgez  $t3, .L7F06A04C
/* 09EB70 7F06A040 000B4043 */   sra   $t0, $t3, 1
/* 09EB74 7F06A044 25610001 */  addiu $at, $t3, 1
/* 09EB78 7F06A048 00014043 */  sra   $t0, $at, 1
.L7F06A04C:
/* 09EB7C 7F06A04C 01283021 */  addu  $a2, $t1, $t0
/* 09EB80 7F06A050 24C60003 */  addiu $a2, $a2, 3
/* 09EB84 7F06A054 0FC1A723 */  jal   gunDrawHudInteger
/* 09EB88 7F06A058 24070001 */   li    $a3, 1
/* 09EB8C 7F06A05C AFA20068 */  sw    $v0, 0x68($sp)
.L7F06A060:
/* 09EB90 7F06A060 0FC2B3BC */  jal   combiner_bayer_lod_perspective
/* 09EB94 7F06A064 8FA40068 */   lw    $a0, 0x68($sp)
/* 09EB98 7F06A068 AFA20068 */  sw    $v0, 0x68($sp)
.L7F06A06C:
/* 09EB9C 7F06A06C 8FA40064 */  lw    $a0, 0x64($sp)
.L7F06A070:
/* 09EBA0 7F06A070 508000AC */  beql  $a0, $zero, .L7F06A324
/* 09EBA4 7F06A074 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09EBA8 7F06A078 0FC1A50B */  jal   get_ammo_type_for_weapon
/* 09EBAC 7F06A07C 00000000 */   nop
/* 09EBB0 7F06A080 104000A7 */  beqz  $v0, .L7F06A320
/* 09EBB4 7F06A084 AFA2005C */   sw    $v0, 0x5c($sp)
/* 09EBB8 7F06A088 3C198008 */  lui   $t9, %hi(g_CurrentPlayer)
/* 09EBBC 7F06A08C 8F39A0B0 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 09EBC0 7F06A090 24010006 */  li    $at, 6
/* 09EBC4 7F06A094 8F220C3C */  lw    $v0, 0xc3c($t9)
/* 09EBC8 7F06A098 104100A1 */  beq   $v0, $at, .L7F06A320
/* 09EBCC 7F06A09C 24010007 */   li    $at, 7
/* 09EBD0 7F06A0A0 1041009F */  beq   $v0, $at, .L7F06A320
/* 09EBD4 7F06A0A4 8FA40064 */   lw    $a0, 0x64($sp)
/* 09EBD8 7F06A0A8 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09EBDC 7F06A0AC 3C050008 */   lui   $a1, 8
/* 09EBE0 7F06A0B0 1440009B */  bnez  $v0, .L7F06A320
/* 09EBE4 7F06A0B4 8FAA005C */   lw    $t2, 0x5c($sp)
/* 09EBE8 7F06A0B8 000A5880 */  sll   $t3, $t2, 2
/* 09EBEC 7F06A0BC 016A5823 */  subu  $t3, $t3, $t2
/* 09EBF0 7F06A0C0 3C098003 */  lui   $t1, %hi(ammo_related)
/* 09EBF4 7F06A0C4 25295EF0 */  addiu $t1, %lo(ammo_related) # addiu $t1, $t1, 0x5ef0
/* 09EBF8 7F06A0C8 000B5880 */  sll   $t3, $t3, 2
/* 09EBFC 7F06A0CC 01691821 */  addu  $v1, $t3, $t1
/* 09EC00 7F06A0D0 8C620004 */  lw    $v0, 4($v1)
/* 09EC04 7F06A0D4 24080005 */  li    $t0, 5
/* 09EC08 7F06A0D8 AFA8003C */  sw    $t0, 0x3c($sp)
/* 09EC0C 7F06A0DC 10400025 */  beqz  $v0, .L7F06A174
/* 09EC10 7F06A0E0 3C0C8009 */   lui   $t4, %hi(globalbank_rdram_offset)
/* 09EC14 7F06A0E4 8D8CD0B0 */  lw    $t4, %lo(globalbank_rdram_offset)($t4)
/* 09EC18 7F06A0E8 AFA30028 */  sw    $v1, 0x28($sp)
/* 09EC1C 7F06A0EC 004C1021 */  addu  $v0, $v0, $t4
/* 09EC20 7F06A0F0 0FC1E131 */  jal   getPlayer_c_screenleft
/* 09EC24 7F06A0F4 AFA20040 */   sw    $v0, 0x40($sp)
/* 09EC28 7F06A0F8 0C001149 */  jal   viGetViewTop
/* 09EC2C 7F06A0FC E7A00034 */   swc1  $f0, 0x34($sp)
/* 09EC30 7F06A100 0C00112B */  jal   viGetViewHeight
/* 09EC34 7F06A104 A7A20038 */   sh    $v0, 0x38($sp)
/* 09EC38 7F06A108 8FAD0058 */  lw    $t5, 0x58($sp)
/* 09EC3C 7F06A10C 87AE0038 */  lh    $t6, 0x38($sp)
/* 09EC40 7F06A110 C7B20034 */  lwc1  $f18, 0x34($sp)
/* 09EC44 7F06A114 448D8000 */  mtc1  $t5, $f16
/* 09EC48 7F06A118 8FAA0028 */  lw    $t2, 0x28($sp)
/* 09EC4C 7F06A11C 004EC021 */  addu  $t8, $v0, $t6
/* 09EC50 7F06A120 46808420 */  cvt.s.w $f16, $f16
/* 09EC54 7F06A124 270FFFEC */  addiu $t7, $t8, -0x14
/* 09EC58 7F06A128 448F7000 */  mtc1  $t7, $f14
/* 09EC5C 7F06A12C 24190001 */  li    $t9, 1
/* 09EC60 7F06A130 AFB90014 */  sw    $t9, 0x14($sp)
/* 09EC64 7F06A134 468073A0 */  cvt.s.w $f14, $f14
/* 09EC68 7F06A138 240B0001 */  li    $t3, 1
/* 09EC6C 7F06A13C 8FA40068 */  lw    $a0, 0x68($sp)
/* 09EC70 7F06A140 8FA50040 */  lw    $a1, 0x40($sp)
/* 09EC74 7F06A144 3C07BF80 */  lui   $a3, 0xbf80
/* 09EC78 7F06A148 46109400 */  add.s $f16, $f18, $f16
/* 09EC7C 7F06A14C E7AE0010 */  swc1  $f14, 0x10($sp)
/* 09EC80 7F06A150 C54E0008 */  lwc1  $f14, 8($t2)
/* 09EC84 7F06A154 AFAB001C */  sw    $t3, 0x1c($sp)
/* 09EC88 7F06A158 44068000 */  mfc1  $a2, $f16
/* 09EC8C 7F06A15C 0FC1A679 */  jal   set_rgba_redirect_generate_microcode
/* 09EC90 7F06A160 E7AE0018 */   swc1  $f14, 0x18($sp)
/* 09EC94 7F06A164 8FA90040 */  lw    $t1, 0x40($sp)
/* 09EC98 7F06A168 AFA20068 */  sw    $v0, 0x68($sp)
/* 09EC9C 7F06A16C 91280004 */  lbu   $t0, 4($t1)
/* 09ECA0 7F06A170 AFA8003C */  sw    $t0, 0x3c($sp)
.L7F06A174:
/* 09ECA4 7F06A174 0FC2B366 */  jal   microcode_constructor
/* 09ECA8 7F06A178 8FA40068 */   lw    $a0, 0x68($sp)
/* 09ECAC 7F06A17C AFA20068 */  sw    $v0, 0x68($sp)
/* 09ECB0 7F06A180 8FA40064 */  lw    $a0, 0x64($sp)
/* 09ECB4 7F06A184 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09ECB8 7F06A188 3C050040 */   lui   $a1, 0x40
/* 09ECBC 7F06A18C 10400011 */  beqz  $v0, .L7F06A1D4
/* 09ECC0 7F06A190 3C050040 */   lui   $a1, 0x40
/* 09ECC4 7F06A194 8FAD005C */  lw    $t5, 0x5c($sp)
/* 09ECC8 7F06A198 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09ECCC 7F06A19C 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09ECD0 7F06A1A0 AFA0004C */  sw    $zero, 0x4c($sp)
/* 09ECD4 7F06A1A4 000D7080 */  sll   $t6, $t5, 2
/* 09ECD8 7F06A1A8 8FB90064 */  lw    $t9, 0x64($sp)
/* 09ECDC 7F06A1AC 8FAA0060 */  lw    $t2, 0x60($sp)
/* 09ECE0 7F06A1B0 004EC021 */  addu  $t8, $v0, $t6
/* 09ECE4 7F06A1B4 8F0F1130 */  lw    $t7, 0x1130($t8)
/* 09ECE8 7F06A1B8 8C4C0C44 */  lw    $t4, 0xc44($v0)
/* 09ECEC 7F06A1BC 172A0003 */  bne   $t9, $t2, .L7F06A1CC
/* 09ECF0 7F06A1C0 018F1821 */   addu  $v1, $t4, $t7
/* 09ECF4 7F06A1C4 8C4B089C */  lw    $t3, 0x89c($v0)
/* 09ECF8 7F06A1C8 006B1821 */  addu  $v1, $v1, $t3
.L7F06A1CC:
/* 09ECFC 7F06A1CC 1000000A */  b     .L7F06A1F8
/* 09ED00 7F06A1D0 AFA30050 */   sw    $v1, 0x50($sp)
.L7F06A1D4:
/* 09ED04 7F06A1D4 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09ED08 7F06A1D8 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09ED0C 7F06A1DC 8FA8005C */  lw    $t0, 0x5c($sp)
/* 09ED10 7F06A1E0 8C490C44 */  lw    $t1, 0xc44($v0)
/* 09ED14 7F06A1E4 00086880 */  sll   $t5, $t0, 2
/* 09ED18 7F06A1E8 004D7021 */  addu  $t6, $v0, $t5
/* 09ED1C 7F06A1EC AFA9004C */  sw    $t1, 0x4c($sp)
/* 09ED20 7F06A1F0 8DD81130 */  lw    $t8, 0x1130($t6)
/* 09ED24 7F06A1F4 AFB80050 */  sw    $t8, 0x50($sp)
.L7F06A1F8:
/* 09ED28 7F06A1F8 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09ED2C 7F06A1FC 8FA40064 */   lw    $a0, 0x64($sp)
/* 09ED30 7F06A200 5440001F */  bnezl $v0, .L7F06A280
/* 09ED34 7F06A204 8FAC0050 */   lw    $t4, 0x50($sp)
/* 09ED38 7F06A208 0C001145 */  jal   viGetViewLeft
/* 09ED3C 7F06A20C 00000000 */   nop
/* 09ED40 7F06A210 0C001149 */  jal   viGetViewTop
/* 09ED44 7F06A214 A7A2002E */   sh    $v0, 0x2e($sp)
/* 09ED48 7F06A218 0C00112B */  jal   viGetViewHeight
/* 09ED4C 7F06A21C A7A20038 */   sh    $v0, 0x38($sp)
/* 09ED50 7F06A220 87AC002E */  lh    $t4, 0x2e($sp)
/* 09ED54 7F06A224 8FAF0058 */  lw    $t7, 0x58($sp)
/* 09ED58 7F06A228 8FAA003C */  lw    $t2, 0x3c($sp)
/* 09ED5C 7F06A22C 87A90038 */  lh    $t1, 0x38($sp)
/* 09ED60 7F06A230 018FC821 */  addu  $t9, $t4, $t7
/* 09ED64 7F06A234 240E0002 */  li    $t6, 2
/* 09ED68 7F06A238 00494021 */  addu  $t0, $v0, $t1
/* 09ED6C 7F06A23C 250DFFEE */  addiu $t5, $t0, -0x12
/* 09ED70 7F06A240 24180001 */  li    $t8, 1
/* 09ED74 7F06A244 AFB80018 */  sw    $t8, 0x18($sp)
/* 09ED78 7F06A248 AFAD0010 */  sw    $t5, 0x10($sp)
/* 09ED7C 7F06A24C AFAE0014 */  sw    $t6, 0x14($sp)
/* 09ED80 7F06A250 8FA40068 */  lw    $a0, 0x68($sp)
/* 09ED84 7F06A254 8FA5004C */  lw    $a1, 0x4c($sp)
/* 09ED88 7F06A258 05410003 */  bgez  $t2, .L7F06A268
/* 09ED8C 7F06A25C 000A5843 */   sra   $t3, $t2, 1
/* 09ED90 7F06A260 25410001 */  addiu $at, $t2, 1
/* 09ED94 7F06A264 00015843 */  sra   $t3, $at, 1
.L7F06A268:
/* 09ED98 7F06A268 032B3021 */  addu  $a2, $t9, $t3
/* 09ED9C 7F06A26C 24C60003 */  addiu $a2, $a2, 3
/* 09EDA0 7F06A270 0FC1A723 */  jal   gunDrawHudInteger
/* 09EDA4 7F06A274 24070001 */   li    $a3, 1
/* 09EDA8 7F06A278 AFA20068 */  sw    $v0, 0x68($sp)
/* 09EDAC 7F06A27C 8FAC0050 */  lw    $t4, 0x50($sp)
.L7F06A280:
/* 09EDB0 7F06A280 8FA40064 */  lw    $a0, 0x64($sp)
/* 09EDB4 7F06A284 1D800005 */  bgtz  $t4, .L7F06A29C
/* 09EDB8 7F06A288 00000000 */   nop
/* 09EDBC 7F06A28C 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09EDC0 7F06A290 3C050040 */   lui   $a1, 0x40
/* 09EDC4 7F06A294 1040001F */  beqz  $v0, .L7F06A314
/* 09EDC8 7F06A298 00000000 */   nop
.L7F06A29C:
/* 09EDCC 7F06A29C 0C001145 */  jal   viGetViewLeft
/* 09EDD0 7F06A2A0 00000000 */   nop
/* 09EDD4 7F06A2A4 0C001149 */  jal   viGetViewTop
/* 09EDD8 7F06A2A8 A7A2002E */   sh    $v0, 0x2e($sp)
/* 09EDDC 7F06A2AC 0C00112B */  jal   viGetViewHeight
/* 09EDE0 7F06A2B0 A7A20038 */   sh    $v0, 0x38($sp)
/* 09EDE4 7F06A2B4 87AF002E */  lh    $t7, 0x2e($sp)
/* 09EDE8 7F06A2B8 8FAA0058 */  lw    $t2, 0x58($sp)
/* 09EDEC 7F06A2BC 8FAB003C */  lw    $t3, 0x3c($sp)
/* 09EDF0 7F06A2C0 87AD0038 */  lh    $t5, 0x38($sp)
/* 09EDF4 7F06A2C4 01EAC821 */  addu  $t9, $t7, $t2
/* 09EDF8 7F06A2C8 25690001 */  addiu $t1, $t3, 1
/* 09EDFC 7F06A2CC 004D7021 */  addu  $t6, $v0, $t5
/* 09EE00 7F06A2D0 25D8FFEE */  addiu $t8, $t6, -0x12
/* 09EE04 7F06A2D4 240F0001 */  li    $t7, 1
/* 09EE08 7F06A2D8 240C0002 */  li    $t4, 2
/* 09EE0C 7F06A2DC AFAC0014 */  sw    $t4, 0x14($sp)
/* 09EE10 7F06A2E0 AFAF0018 */  sw    $t7, 0x18($sp)
/* 09EE14 7F06A2E4 AFB80010 */  sw    $t8, 0x10($sp)
/* 09EE18 7F06A2E8 8FA40068 */  lw    $a0, 0x68($sp)
/* 09EE1C 7F06A2EC 8FA50050 */  lw    $a1, 0x50($sp)
/* 09EE20 7F06A2F0 05210003 */  bgez  $t1, .L7F06A300
/* 09EE24 7F06A2F4 00094043 */   sra   $t0, $t1, 1
/* 09EE28 7F06A2F8 25210001 */  addiu $at, $t1, 1
/* 09EE2C 7F06A2FC 00014043 */  sra   $t0, $at, 1
.L7F06A300:
/* 09EE30 7F06A300 03283023 */  subu  $a2, $t9, $t0
/* 09EE34 7F06A304 24C6FFFC */  addiu $a2, $a2, -4
/* 09EE38 7F06A308 0FC1A723 */  jal   gunDrawHudInteger
/* 09EE3C 7F06A30C 00003825 */   move  $a3, $zero
/* 09EE40 7F06A310 AFA20068 */  sw    $v0, 0x68($sp)
.L7F06A314:
/* 09EE44 7F06A314 0FC2B3BC */  jal   combiner_bayer_lod_perspective
/* 09EE48 7F06A318 8FA40068 */   lw    $a0, 0x68($sp)
/* 09EE4C 7F06A31C AFA20068 */  sw    $v0, 0x68($sp)
.L7F06A320:
/* 09EE50 7F06A320 8FBF0024 */  lw    $ra, 0x24($sp)
.L7F06A324:
/* 09EE54 7F06A324 8FA20068 */  lw    $v0, 0x68($sp)
/* 09EE58 7F06A328 27BD0068 */  addiu $sp, $sp, 0x68
/* 09EE5C 7F06A32C 03E00008 */  jr    $ra
/* 09EE60 7F06A330 00000000 */   nop
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.text
glabel generate_ammo_total_microcode
/* 09CE78 7F06A488 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09CE7C 7F06A48C 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09CE80 7F06A490 27BDFF98 */  addiu $sp, $sp, -0x68
/* 09CE84 7F06A494 AFBF0024 */  sw    $ra, 0x24($sp)
/* 09CE88 7F06A498 AFA40068 */  sw    $a0, 0x68($sp)
/* 09CE8C 7F06A49C 8C4E105C */  lw    $t6, 0x105c($v0)
/* 09CE90 7F06A4A0 55C00185 */  bnezl $t6, .L7F06AAB8
/* 09CE94 7F06A4A4 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09CE98 7F06A4A8 8C4F29BC */  lw    $t7, 0x29bc($v0)
/* 09CE9C 7F06A4AC 55E00182 */  bnezl $t7, .L7F06AAB8
/* 09CEA0 7F06A4B0 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09CEA4 7F06A4B4 0FC177A2 */  jal   getCurrentPlayerWeaponId
/* 09CEA8 7F06A4B8 24040001 */   li    $a0, 1
/* 09CEAC 7F06A4BC AFA20064 */  sw    $v0, 0x64($sp)
/* 09CEB0 7F06A4C0 0FC177A2 */  jal   getCurrentPlayerWeaponId
/* 09CEB4 7F06A4C4 00002025 */   move  $a0, $zero
/* 09CEB8 7F06A4C8 0FC26669 */  jal   getPlayerCount
/* 09CEBC 7F06A4CC AFA20060 */   sw    $v0, 0x60($sp)
/* 09CEC0 7F06A4D0 28410003 */  slti  $at, $v0, 3
/* 09CEC4 7F06A4D4 10200005 */  beqz  $at, .L7F06A4EC
/* 09CEC8 7F06A4D8 2418003B */   li    $t8, 59
/* 09CECC 7F06A4DC 2419003B */  li    $t9, 59
/* 09CED0 7F06A4E0 AFB80058 */  sw    $t8, 0x58($sp)
/* 09CED4 7F06A4E4 1000000E */  b     .L7F06A520
/* 09CED8 7F06A4E8 AFB90054 */   sw    $t9, 0x54($sp)
.L7F06A4EC:
/* 09CEDC 7F06A4EC 0FC269A4 */  jal   get_cur_playernum
/* 09CEE0 7F06A4F0 00000000 */   nop
/* 09CEE4 7F06A4F4 30480001 */  andi  $t0, $v0, 1
/* 09CEE8 7F06A4F8 11000006 */  beqz  $t0, .L7F06A514
/* 09CEEC 7F06A4FC 240B003B */   li    $t3, 59
/* 09CEF0 7F06A500 2409002B */  li    $t1, 43
/* 09CEF4 7F06A504 240A007F */  li    $t2, 127
/* 09CEF8 7F06A508 AFA90058 */  sw    $t1, 0x58($sp)
/* 09CEFC 7F06A50C 10000004 */  b     .L7F06A520
/* 09CF00 7F06A510 AFAA0054 */   sw    $t2, 0x54($sp)
.L7F06A514:
/* 09CF04 7F06A514 240C006D */  li    $t4, 109
/* 09CF08 7F06A518 AFAB0058 */  sw    $t3, 0x58($sp)
/* 09CF0C 7F06A51C AFAC0054 */  sw    $t4, 0x54($sp)
.L7F06A520:
/* 09CF10 7F06A520 8FAD0060 */  lw    $t5, 0x60($sp)
/* 09CF14 7F06A524 51A000B7 */  beql  $t5, $zero, .L7F06A804
/* 09CF18 7F06A528 8FA40064 */   lw    $a0, 0x64($sp)
/* 09CF1C 7F06A52C 0FC1A6F0 */  jal   get_ammo_type_for_weapon
/* 09CF20 7F06A530 01A02025 */   move  $a0, $t5
/* 09CF24 7F06A534 104000B2 */  beqz  $v0, .L7F06A800
/* 09CF28 7F06A538 AFA2005C */   sw    $v0, 0x5c($sp)
/* 09CF2C 7F06A53C 3C0E8007 */  lui   $t6, %hi(g_CurrentPlayer) # $t6, 0x8007
/* 09CF30 7F06A540 8DCE8BC0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 09CF34 7F06A544 24010006 */  li    $at, 6
/* 09CF38 7F06A548 8DC2088C */  lw    $v0, 0x88c($t6)
/* 09CF3C 7F06A54C 104100AC */  beq   $v0, $at, .L7F06A800
/* 09CF40 7F06A550 24010007 */   li    $at, 7
/* 09CF44 7F06A554 104100AA */  beq   $v0, $at, .L7F06A800
/* 09CF48 7F06A558 8FA40060 */   lw    $a0, 0x60($sp)
/* 09CF4C 7F06A55C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09CF50 7F06A560 3C050008 */   lui   $a1, 8
/* 09CF54 7F06A564 144000A6 */  bnez  $v0, .L7F06A800
/* 09CF58 7F06A568 8FAF005C */   lw    $t7, 0x5c($sp)
/* 09CF5C 7F06A56C 000FC080 */  sll   $t8, $t7, 2
/* 09CF60 7F06A570 030FC023 */  subu  $t8, $t8, $t7
/* 09CF64 7F06A574 3C198003 */  lui   $t9, %hi(ammo_related) # $t9, 0x8003
/* 09CF68 7F06A578 27391440 */  addiu $t9, %lo(ammo_related) # addiu $t9, $t9, 0x1440
/* 09CF6C 7F06A57C 0018C080 */  sll   $t8, $t8, 2
/* 09CF70 7F06A580 03191821 */  addu  $v1, $t8, $t9
/* 09CF74 7F06A584 8C620004 */  lw    $v0, 4($v1)
/* 09CF78 7F06A588 24080005 */  li    $t0, 5
/* 09CF7C 7F06A58C AFA80044 */  sw    $t0, 0x44($sp)
/* 09CF80 7F06A590 10400028 */  beqz  $v0, .L7F06A634
/* 09CF84 7F06A594 3C098007 */   lui   $t1, %hi(globalbank_rdram_offset) # $t1, 0x8007
/* 09CF88 7F06A598 8D294490 */  lw    $t1, %lo(globalbank_rdram_offset)($t1)
/* 09CF8C 7F06A59C AFA30028 */  sw    $v1, 0x28($sp)
/* 09CF90 7F06A5A0 00491021 */  addu  $v0, $v0, $t1
/* 09CF94 7F06A5A4 0FC1E151 */  jal   getPlayer_c_screenleft
/* 09CF98 7F06A5A8 AFA20048 */   sw    $v0, 0x48($sp)
/* 09CF9C 7F06A5AC 0FC1E149 */  jal   getPlayer_c_screenwidth
/* 09CFA0 7F06A5B0 E7A00030 */   swc1  $f0, 0x30($sp)
/* 09CFA4 7F06A5B4 0C000FDD */  jal   viGetViewTop
/* 09CFA8 7F06A5B8 E7A00034 */   swc1  $f0, 0x34($sp)
/* 09CFAC 7F06A5BC 0C000FBF */  jal   viGetViewHeight
/* 09CFB0 7F06A5C0 A7A20038 */   sh    $v0, 0x38($sp)
/* 09CFB4 7F06A5C4 8FAA0054 */  lw    $t2, 0x54($sp)
/* 09CFB8 7F06A5C8 87AB0038 */  lh    $t3, 0x38($sp)
/* 09CFBC 7F06A5CC C7A40034 */  lwc1  $f4, 0x34($sp)
/* 09CFC0 7F06A5D0 C7A60030 */  lwc1  $f6, 0x30($sp)
/* 09CFC4 7F06A5D4 448A5000 */  mtc1  $t2, $f10
/* 09CFC8 7F06A5D8 004B6021 */  addu  $t4, $v0, $t3
/* 09CFCC 7F06A5DC 46062200 */  add.s $f8, $f4, $f6
/* 09CFD0 7F06A5E0 258DFFE2 */  addiu $t5, $t4, -0x1e
/* 09CFD4 7F06A5E4 448D2000 */  mtc1  $t5, $f4
/* 09CFD8 7F06A5E8 46805420 */  cvt.s.w $f16, $f10
/* 09CFDC 7F06A5EC 8FAE0028 */  lw    $t6, 0x28($sp)
/* 09CFE0 7F06A5F0 AFA00014 */  sw    $zero, 0x14($sp)
/* 09CFE4 7F06A5F4 240F0001 */  li    $t7, 1
/* 09CFE8 7F06A5F8 8FA40068 */  lw    $a0, 0x68($sp)
/* 09CFEC 7F06A5FC 468021A0 */  cvt.s.w $f6, $f4
/* 09CFF0 7F06A600 8FA50048 */  lw    $a1, 0x48($sp)
/* 09CFF4 7F06A604 3C07BF80 */  lui   $a3, 0xbf80
/* 09CFF8 7F06A608 46104481 */  sub.s $f18, $f8, $f16
/* 09CFFC 7F06A60C E7A60010 */  swc1  $f6, 0x10($sp)
/* 09D000 7F06A610 C5CA0008 */  lwc1  $f10, 8($t6)
/* 09D004 7F06A614 AFAF001C */  sw    $t7, 0x1c($sp)
/* 09D008 7F06A618 44069000 */  mfc1  $a2, $f18
/* 09D00C 7F06A61C 0FC1A85E */  jal   set_rgba_redirect_generate_microcode
/* 09D010 7F06A620 E7AA0018 */   swc1  $f10, 0x18($sp)
/* 09D014 7F06A624 8FB80048 */  lw    $t8, 0x48($sp)
/* 09D018 7F06A628 AFA20068 */  sw    $v0, 0x68($sp)
/* 09D01C 7F06A62C 93190004 */  lbu   $t9, 4($t8)
/* 09D020 7F06A630 AFB90044 */  sw    $t9, 0x44($sp)
.L7F06A634:
/* 09D024 7F06A634 0FC2B016 */  jal   microcode_constructor
/* 09D028 7F06A638 8FA40068 */   lw    $a0, 0x68($sp)
/* 09D02C 7F06A63C AFA20068 */  sw    $v0, 0x68($sp)
/* 09D030 7F06A640 8FA40060 */  lw    $a0, 0x60($sp)
/* 09D034 7F06A644 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D038 7F06A648 3C050040 */   lui   $a1, 0x40
/* 09D03C 7F06A64C 10400011 */  beqz  $v0, .L7F06A694
/* 09D040 7F06A650 3C050040 */   lui   $a1, 0x40
/* 09D044 7F06A654 8FA9005C */  lw    $t1, 0x5c($sp)
/* 09D048 7F06A658 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09D04C 7F06A65C 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D050 7F06A660 AFA0004C */  sw    $zero, 0x4c($sp)
/* 09D054 7F06A664 00095080 */  sll   $t2, $t1, 2
/* 09D058 7F06A668 8FAD0064 */  lw    $t5, 0x64($sp)
/* 09D05C 7F06A66C 8FAE0060 */  lw    $t6, 0x60($sp)
/* 09D060 7F06A670 004A5821 */  addu  $t3, $v0, $t2
/* 09D064 7F06A674 8D6C1128 */  lw    $t4, 0x1128($t3)
/* 09D068 7F06A678 8C480894 */  lw    $t0, 0x894($v0)
/* 09D06C 7F06A67C 15AE0003 */  bne   $t5, $t6, .L7F06A68C
/* 09D070 7F06A680 010C1821 */   addu  $v1, $t0, $t4
/* 09D074 7F06A684 8C4F0C3C */  lw    $t7, 0xc3c($v0)
/* 09D078 7F06A688 006F1821 */  addu  $v1, $v1, $t7
.L7F06A68C:
/* 09D07C 7F06A68C 1000000A */  b     .L7F06A6B8
/* 09D080 7F06A690 AFA30050 */   sw    $v1, 0x50($sp)
.L7F06A694:
/* 09D084 7F06A694 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09D088 7F06A698 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D08C 7F06A69C 8FB9005C */  lw    $t9, 0x5c($sp)
/* 09D090 7F06A6A0 8C580894 */  lw    $t8, 0x894($v0)
/* 09D094 7F06A6A4 00194880 */  sll   $t1, $t9, 2
/* 09D098 7F06A6A8 00495021 */  addu  $t2, $v0, $t1
/* 09D09C 7F06A6AC AFB8004C */  sw    $t8, 0x4c($sp)
/* 09D0A0 7F06A6B0 8D4B1128 */  lw    $t3, 0x1128($t2)
/* 09D0A4 7F06A6B4 AFAB0050 */  sw    $t3, 0x50($sp)
.L7F06A6B8:
/* 09D0A8 7F06A6B8 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D0AC 7F06A6BC 8FA40060 */   lw    $a0, 0x60($sp)
/* 09D0B0 7F06A6C0 54400023 */  bnezl $v0, .L7F06A750
/* 09D0B4 7F06A6C4 8FAD0050 */   lw    $t5, 0x50($sp)
/* 09D0B8 7F06A6C8 0C000FD9 */  jal   viGetViewLeft
/* 09D0BC 7F06A6CC 00000000 */   nop
/* 09D0C0 7F06A6D0 0C000FBB */  jal   viGetViewWidth
/* 09D0C4 7F06A6D4 A7A2002C */   sh    $v0, 0x2c($sp)
/* 09D0C8 7F06A6D8 0C000FDD */  jal   viGetViewTop
/* 09D0CC 7F06A6DC A7A2002E */   sh    $v0, 0x2e($sp)
/* 09D0D0 7F06A6E0 0C000FBF */  jal   viGetViewHeight
/* 09D0D4 7F06A6E4 A7A20038 */   sh    $v0, 0x38($sp)
/* 09D0D8 7F06A6E8 87A8002E */  lh    $t0, 0x2e($sp)
/* 09D0DC 7F06A6EC 87AC002C */  lh    $t4, 0x2c($sp)
/* 09D0E0 7F06A6F0 8FAE0054 */  lw    $t6, 0x54($sp)
/* 09D0E4 7F06A6F4 8FB80044 */  lw    $t8, 0x44($sp)
/* 09D0E8 7F06A6F8 87A90038 */  lh    $t1, 0x38($sp)
/* 09D0EC 7F06A6FC 010C6821 */  addu  $t5, $t0, $t4
/* 09D0F0 7F06A700 01AE7823 */  subu  $t7, $t5, $t6
/* 09D0F4 7F06A704 00495021 */  addu  $t2, $v0, $t1
/* 09D0F8 7F06A708 254BFFE4 */  addiu $t3, $t2, -0x1c
/* 09D0FC 7F06A70C 240C0001 */  li    $t4, 1
/* 09D100 7F06A710 24080002 */  li    $t0, 2
/* 09D104 7F06A714 AFA80014 */  sw    $t0, 0x14($sp)
/* 09D108 7F06A718 AFAC0018 */  sw    $t4, 0x18($sp)
/* 09D10C 7F06A71C AFAB0010 */  sw    $t3, 0x10($sp)
/* 09D110 7F06A720 8FA40068 */  lw    $a0, 0x68($sp)
/* 09D114 7F06A724 8FA5004C */  lw    $a1, 0x4c($sp)
/* 09D118 7F06A728 07010003 */  bgez  $t8, .L7F06A738
/* 09D11C 7F06A72C 0018C843 */   sra   $t9, $t8, 1
/* 09D120 7F06A730 27010001 */  addiu $at, $t8, 1
/* 09D124 7F06A734 0001C843 */  sra   $t9, $at, 1
.L7F06A738:
/* 09D128 7F06A738 01F93023 */  subu  $a2, $t7, $t9
/* 09D12C 7F06A73C 24C6FFFC */  addiu $a2, $a2, -4
/* 09D130 7F06A740 0FC1A908 */  jal   gunDrawHudInteger
/* 09D134 7F06A744 00003825 */   move  $a3, $zero
/* 09D138 7F06A748 AFA20068 */  sw    $v0, 0x68($sp)
/* 09D13C 7F06A74C 8FAD0050 */  lw    $t5, 0x50($sp)
.L7F06A750:
/* 09D140 7F06A750 8FA40060 */  lw    $a0, 0x60($sp)
/* 09D144 7F06A754 1DA00005 */  bgtz  $t5, .L7F06A76C
/* 09D148 7F06A758 00000000 */   nop
/* 09D14C 7F06A75C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D150 7F06A760 3C050040 */   lui   $a1, 0x40
/* 09D154 7F06A764 10400023 */  beqz  $v0, .L7F06A7F4
/* 09D158 7F06A768 00000000 */   nop
.L7F06A76C:
/* 09D15C 7F06A76C 0C000FD9 */  jal   viGetViewLeft
/* 09D160 7F06A770 00000000 */   nop
/* 09D164 7F06A774 0C000FBB */  jal   viGetViewWidth
/* 09D168 7F06A778 A7A2002C */   sh    $v0, 0x2c($sp)
/* 09D16C 7F06A77C 0C000FDD */  jal   viGetViewTop
/* 09D170 7F06A780 A7A2002E */   sh    $v0, 0x2e($sp)
/* 09D174 7F06A784 0C000FBF */  jal   viGetViewHeight
/* 09D178 7F06A788 A7A20038 */   sh    $v0, 0x38($sp)
/* 09D17C 7F06A78C 87AE002E */  lh    $t6, 0x2e($sp)
/* 09D180 7F06A790 87B8002C */  lh    $t8, 0x2c($sp)
/* 09D184 7F06A794 8FB90054 */  lw    $t9, 0x54($sp)
/* 09D188 7F06A798 8FAA0044 */  lw    $t2, 0x44($sp)
/* 09D18C 7F06A79C 87AC0038 */  lh    $t4, 0x38($sp)
/* 09D190 7F06A7A0 01D87821 */  addu  $t7, $t6, $t8
/* 09D194 7F06A7A4 01F94823 */  subu  $t1, $t7, $t9
/* 09D198 7F06A7A8 254B0001 */  addiu $t3, $t2, 1
/* 09D19C 7F06A7AC 004C6821 */  addu  $t5, $v0, $t4
/* 09D1A0 7F06A7B0 25AEFFE4 */  addiu $t6, $t5, -0x1c
/* 09D1A4 7F06A7B4 240F0001 */  li    $t7, 1
/* 09D1A8 7F06A7B8 24180002 */  li    $t8, 2
/* 09D1AC 7F06A7BC AFB80014 */  sw    $t8, 0x14($sp)
/* 09D1B0 7F06A7C0 AFAF0018 */  sw    $t7, 0x18($sp)
/* 09D1B4 7F06A7C4 AFAE0010 */  sw    $t6, 0x10($sp)
/* 09D1B8 7F06A7C8 8FA40068 */  lw    $a0, 0x68($sp)
/* 09D1BC 7F06A7CC 8FA50050 */  lw    $a1, 0x50($sp)
/* 09D1C0 7F06A7D0 05610003 */  bgez  $t3, .L7F06A7E0
/* 09D1C4 7F06A7D4 000B4043 */   sra   $t0, $t3, 1
/* 09D1C8 7F06A7D8 25610001 */  addiu $at, $t3, 1
/* 09D1CC 7F06A7DC 00014043 */  sra   $t0, $at, 1
.L7F06A7E0:
/* 09D1D0 7F06A7E0 01283021 */  addu  $a2, $t1, $t0
/* 09D1D4 7F06A7E4 24C60003 */  addiu $a2, $a2, 3
/* 09D1D8 7F06A7E8 0FC1A908 */  jal   gunDrawHudInteger
/* 09D1DC 7F06A7EC 24070001 */   li    $a3, 1
/* 09D1E0 7F06A7F0 AFA20068 */  sw    $v0, 0x68($sp)
.L7F06A7F4:
/* 09D1E4 7F06A7F4 0FC2B06C */  jal   combiner_bayer_lod_perspective
/* 09D1E8 7F06A7F8 8FA40068 */   lw    $a0, 0x68($sp)
/* 09D1EC 7F06A7FC AFA20068 */  sw    $v0, 0x68($sp)
.L7F06A800:
/* 09D1F0 7F06A800 8FA40064 */  lw    $a0, 0x64($sp)
.L7F06A804:
/* 09D1F4 7F06A804 508000AC */  beql  $a0, $zero, .L7F06AAB8
/* 09D1F8 7F06A808 8FBF0024 */   lw    $ra, 0x24($sp)
/* 09D1FC 7F06A80C 0FC1A6F0 */  jal   get_ammo_type_for_weapon
/* 09D200 7F06A810 00000000 */   nop
/* 09D204 7F06A814 104000A7 */  beqz  $v0, .L7F06AAB4
/* 09D208 7F06A818 AFA2005C */   sw    $v0, 0x5c($sp)
/* 09D20C 7F06A81C 3C198007 */  lui   $t9, %hi(g_CurrentPlayer) # $t9, 0x8007
/* 09D210 7F06A820 8F398BC0 */  lw    $t9, %lo(g_CurrentPlayer)($t9)
/* 09D214 7F06A824 24010006 */  li    $at, 6
/* 09D218 7F06A828 8F220C34 */  lw    $v0, 0xc34($t9)
/* 09D21C 7F06A82C 104100A1 */  beq   $v0, $at, .L7F06AAB4
/* 09D220 7F06A830 24010007 */   li    $at, 7
/* 09D224 7F06A834 1041009F */  beq   $v0, $at, .L7F06AAB4
/* 09D228 7F06A838 8FA40064 */   lw    $a0, 0x64($sp)
/* 09D22C 7F06A83C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D230 7F06A840 3C050008 */   lui   $a1, 8
/* 09D234 7F06A844 1440009B */  bnez  $v0, .L7F06AAB4
/* 09D238 7F06A848 8FAA005C */   lw    $t2, 0x5c($sp)
/* 09D23C 7F06A84C 000A5880 */  sll   $t3, $t2, 2
/* 09D240 7F06A850 016A5823 */  subu  $t3, $t3, $t2
/* 09D244 7F06A854 3C098003 */  lui   $t1, %hi(ammo_related) # $t1, 0x8003
/* 09D248 7F06A858 25291440 */  addiu $t1, %lo(ammo_related) # addiu $t1, $t1, 0x1440
/* 09D24C 7F06A85C 000B5880 */  sll   $t3, $t3, 2
/* 09D250 7F06A860 01691821 */  addu  $v1, $t3, $t1
/* 09D254 7F06A864 8C620004 */  lw    $v0, 4($v1)
/* 09D258 7F06A868 24080005 */  li    $t0, 5
/* 09D25C 7F06A86C AFA8003C */  sw    $t0, 0x3c($sp)
/* 09D260 7F06A870 10400025 */  beqz  $v0, .L7F06A908
/* 09D264 7F06A874 3C0C8007 */   lui   $t4, %hi(globalbank_rdram_offset) # $t4, 0x8007
/* 09D268 7F06A878 8D8C4490 */  lw    $t4, %lo(globalbank_rdram_offset)($t4)
/* 09D26C 7F06A87C AFA30028 */  sw    $v1, 0x28($sp)
/* 09D270 7F06A880 004C1021 */  addu  $v0, $v0, $t4
/* 09D274 7F06A884 0FC1E151 */  jal   getPlayer_c_screenleft
/* 09D278 7F06A888 AFA20040 */   sw    $v0, 0x40($sp)
/* 09D27C 7F06A88C 0C000FDD */  jal   viGetViewTop
/* 09D280 7F06A890 E7A00034 */   swc1  $f0, 0x34($sp)
/* 09D284 7F06A894 0C000FBF */  jal   viGetViewHeight
/* 09D288 7F06A898 A7A20038 */   sh    $v0, 0x38($sp)
/* 09D28C 7F06A89C 8FAD0058 */  lw    $t5, 0x58($sp)
/* 09D290 7F06A8A0 87AE0038 */  lh    $t6, 0x38($sp)
/* 09D294 7F06A8A4 C7B20034 */  lwc1  $f18, 0x34($sp)
/* 09D298 7F06A8A8 448D8000 */  mtc1  $t5, $f16
/* 09D29C 7F06A8AC 8FAA0028 */  lw    $t2, 0x28($sp)
/* 09D2A0 7F06A8B0 004EC021 */  addu  $t8, $v0, $t6
/* 09D2A4 7F06A8B4 46808420 */  cvt.s.w $f16, $f16
/* 09D2A8 7F06A8B8 270FFFE2 */  addiu $t7, $t8, -0x1e
/* 09D2AC 7F06A8BC 448F7000 */  mtc1  $t7, $f14
/* 09D2B0 7F06A8C0 24190001 */  li    $t9, 1
/* 09D2B4 7F06A8C4 AFB90014 */  sw    $t9, 0x14($sp)
/* 09D2B8 7F06A8C8 468073A0 */  cvt.s.w $f14, $f14
/* 09D2BC 7F06A8CC 240B0001 */  li    $t3, 1
/* 09D2C0 7F06A8D0 8FA40068 */  lw    $a0, 0x68($sp)
/* 09D2C4 7F06A8D4 8FA50040 */  lw    $a1, 0x40($sp)
/* 09D2C8 7F06A8D8 3C07BF80 */  lui   $a3, 0xbf80
/* 09D2CC 7F06A8DC 46109400 */  add.s $f16, $f18, $f16
/* 09D2D0 7F06A8E0 E7AE0010 */  swc1  $f14, 0x10($sp)
/* 09D2D4 7F06A8E4 C54E0008 */  lwc1  $f14, 8($t2)
/* 09D2D8 7F06A8E8 AFAB001C */  sw    $t3, 0x1c($sp)
/* 09D2DC 7F06A8EC 44068000 */  mfc1  $a2, $f16
/* 09D2E0 7F06A8F0 0FC1A85E */  jal   set_rgba_redirect_generate_microcode
/* 09D2E4 7F06A8F4 E7AE0018 */   swc1  $f14, 0x18($sp)
/* 09D2E8 7F06A8F8 8FA90040 */  lw    $t1, 0x40($sp)
/* 09D2EC 7F06A8FC AFA20068 */  sw    $v0, 0x68($sp)
/* 09D2F0 7F06A900 91280004 */  lbu   $t0, 4($t1)
/* 09D2F4 7F06A904 AFA8003C */  sw    $t0, 0x3c($sp)
.L7F06A908:
/* 09D2F8 7F06A908 0FC2B016 */  jal   microcode_constructor
/* 09D2FC 7F06A90C 8FA40068 */   lw    $a0, 0x68($sp)
/* 09D300 7F06A910 AFA20068 */  sw    $v0, 0x68($sp)
/* 09D304 7F06A914 8FA40064 */  lw    $a0, 0x64($sp)
/* 09D308 7F06A918 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D30C 7F06A91C 3C050040 */   lui   $a1, 0x40
/* 09D310 7F06A920 10400011 */  beqz  $v0, .L7F06A968
/* 09D314 7F06A924 3C050040 */   lui   $a1, 0x40
/* 09D318 7F06A928 8FAD005C */  lw    $t5, 0x5c($sp)
/* 09D31C 7F06A92C 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09D320 7F06A930 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D324 7F06A934 AFA0004C */  sw    $zero, 0x4c($sp)
/* 09D328 7F06A938 000D7080 */  sll   $t6, $t5, 2
/* 09D32C 7F06A93C 8FB90064 */  lw    $t9, 0x64($sp)
/* 09D330 7F06A940 8FAA0060 */  lw    $t2, 0x60($sp)
/* 09D334 7F06A944 004EC021 */  addu  $t8, $v0, $t6
/* 09D338 7F06A948 8F0F1128 */  lw    $t7, 0x1128($t8)
/* 09D33C 7F06A94C 8C4C0C3C */  lw    $t4, 0xc3c($v0)
/* 09D340 7F06A950 172A0003 */  bne   $t9, $t2, .L7F06A960
/* 09D344 7F06A954 018F1821 */   addu  $v1, $t4, $t7
/* 09D348 7F06A958 8C4B0894 */  lw    $t3, 0x894($v0)
/* 09D34C 7F06A95C 006B1821 */  addu  $v1, $v1, $t3
.L7F06A960:
/* 09D350 7F06A960 1000000A */  b     .L7F06A98C
/* 09D354 7F06A964 AFA30050 */   sw    $v1, 0x50($sp)
.L7F06A968:
/* 09D358 7F06A968 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09D35C 7F06A96C 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D360 7F06A970 8FA8005C */  lw    $t0, 0x5c($sp)
/* 09D364 7F06A974 8C490C3C */  lw    $t1, 0xc3c($v0)
/* 09D368 7F06A978 00086880 */  sll   $t5, $t0, 2
/* 09D36C 7F06A97C 004D7021 */  addu  $t6, $v0, $t5
/* 09D370 7F06A980 AFA9004C */  sw    $t1, 0x4c($sp)
/* 09D374 7F06A984 8DD81128 */  lw    $t8, 0x1128($t6)
/* 09D378 7F06A988 AFB80050 */  sw    $t8, 0x50($sp)
.L7F06A98C:
/* 09D37C 7F06A98C 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D380 7F06A990 8FA40064 */   lw    $a0, 0x64($sp)
/* 09D384 7F06A994 5440001F */  bnezl $v0, .L7F06AA14
/* 09D388 7F06A998 8FAC0050 */   lw    $t4, 0x50($sp)
/* 09D38C 7F06A99C 0C000FD9 */  jal   viGetViewLeft
/* 09D390 7F06A9A0 00000000 */   nop
/* 09D394 7F06A9A4 0C000FDD */  jal   viGetViewTop
/* 09D398 7F06A9A8 A7A2002E */   sh    $v0, 0x2e($sp)
/* 09D39C 7F06A9AC 0C000FBF */  jal   viGetViewHeight
/* 09D3A0 7F06A9B0 A7A20038 */   sh    $v0, 0x38($sp)
/* 09D3A4 7F06A9B4 87AC002E */  lh    $t4, 0x2e($sp)
/* 09D3A8 7F06A9B8 8FAF0058 */  lw    $t7, 0x58($sp)
/* 09D3AC 7F06A9BC 8FAA003C */  lw    $t2, 0x3c($sp)
/* 09D3B0 7F06A9C0 87A90038 */  lh    $t1, 0x38($sp)
/* 09D3B4 7F06A9C4 018FC821 */  addu  $t9, $t4, $t7
/* 09D3B8 7F06A9C8 240E0002 */  li    $t6, 2
/* 09D3BC 7F06A9CC 00494021 */  addu  $t0, $v0, $t1
/* 09D3C0 7F06A9D0 250DFFE4 */  addiu $t5, $t0, -0x1c
/* 09D3C4 7F06A9D4 24180001 */  li    $t8, 1
/* 09D3C8 7F06A9D8 AFB80018 */  sw    $t8, 0x18($sp)
/* 09D3CC 7F06A9DC AFAD0010 */  sw    $t5, 0x10($sp)
/* 09D3D0 7F06A9E0 AFAE0014 */  sw    $t6, 0x14($sp)
/* 09D3D4 7F06A9E4 8FA40068 */  lw    $a0, 0x68($sp)
/* 09D3D8 7F06A9E8 8FA5004C */  lw    $a1, 0x4c($sp)
/* 09D3DC 7F06A9EC 05410003 */  bgez  $t2, .L7F06A9FC
/* 09D3E0 7F06A9F0 000A5843 */   sra   $t3, $t2, 1
/* 09D3E4 7F06A9F4 25410001 */  addiu $at, $t2, 1
/* 09D3E8 7F06A9F8 00015843 */  sra   $t3, $at, 1
.L7F06A9FC:
/* 09D3EC 7F06A9FC 032B3021 */  addu  $a2, $t9, $t3
/* 09D3F0 7F06AA00 24C60003 */  addiu $a2, $a2, 3
/* 09D3F4 7F06AA04 0FC1A908 */  jal   gunDrawHudInteger
/* 09D3F8 7F06AA08 24070001 */   li    $a3, 1
/* 09D3FC 7F06AA0C AFA20068 */  sw    $v0, 0x68($sp)
/* 09D400 7F06AA10 8FAC0050 */  lw    $t4, 0x50($sp)
.L7F06AA14:
/* 09D404 7F06AA14 8FA40064 */  lw    $a0, 0x64($sp)
/* 09D408 7F06AA18 1D800005 */  bgtz  $t4, .L7F06AA30
/* 09D40C 7F06AA1C 00000000 */   nop
/* 09D410 7F06AA20 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D414 7F06AA24 3C050040 */   lui   $a1, 0x40
/* 09D418 7F06AA28 1040001F */  beqz  $v0, .L7F06AAA8
/* 09D41C 7F06AA2C 00000000 */   nop
.L7F06AA30:
/* 09D420 7F06AA30 0C000FD9 */  jal   viGetViewLeft
/* 09D424 7F06AA34 00000000 */   nop
/* 09D428 7F06AA38 0C000FDD */  jal   viGetViewTop
/* 09D42C 7F06AA3C A7A2002E */   sh    $v0, 0x2e($sp)
/* 09D430 7F06AA40 0C000FBF */  jal   viGetViewHeight
/* 09D434 7F06AA44 A7A20038 */   sh    $v0, 0x38($sp)
/* 09D438 7F06AA48 87AF002E */  lh    $t7, 0x2e($sp)
/* 09D43C 7F06AA4C 8FAA0058 */  lw    $t2, 0x58($sp)
/* 09D440 7F06AA50 8FAB003C */  lw    $t3, 0x3c($sp)
/* 09D444 7F06AA54 87AD0038 */  lh    $t5, 0x38($sp)
/* 09D448 7F06AA58 01EAC821 */  addu  $t9, $t7, $t2
/* 09D44C 7F06AA5C 25690001 */  addiu $t1, $t3, 1
/* 09D450 7F06AA60 004D7021 */  addu  $t6, $v0, $t5
/* 09D454 7F06AA64 25D8FFE4 */  addiu $t8, $t6, -0x1c
/* 09D458 7F06AA68 240F0001 */  li    $t7, 1
/* 09D45C 7F06AA6C 240C0002 */  li    $t4, 2
/* 09D460 7F06AA70 AFAC0014 */  sw    $t4, 0x14($sp)
/* 09D464 7F06AA74 AFAF0018 */  sw    $t7, 0x18($sp)
/* 09D468 7F06AA78 AFB80010 */  sw    $t8, 0x10($sp)
/* 09D46C 7F06AA7C 8FA40068 */  lw    $a0, 0x68($sp)
/* 09D470 7F06AA80 8FA50050 */  lw    $a1, 0x50($sp)
/* 09D474 7F06AA84 05210003 */  bgez  $t1, .L7F06AA94
/* 09D478 7F06AA88 00094043 */   sra   $t0, $t1, 1
/* 09D47C 7F06AA8C 25210001 */  addiu $at, $t1, 1
/* 09D480 7F06AA90 00014043 */  sra   $t0, $at, 1
.L7F06AA94:
/* 09D484 7F06AA94 03283023 */  subu  $a2, $t9, $t0
/* 09D488 7F06AA98 24C6FFFC */  addiu $a2, $a2, -4
/* 09D48C 7F06AA9C 0FC1A908 */  jal   gunDrawHudInteger
/* 09D490 7F06AAA0 00003825 */   move  $a3, $zero
/* 09D494 7F06AAA4 AFA20068 */  sw    $v0, 0x68($sp)
.L7F06AAA8:
/* 09D498 7F06AAA8 0FC2B06C */  jal   combiner_bayer_lod_perspective
/* 09D49C 7F06AAAC 8FA40068 */   lw    $a0, 0x68($sp)
/* 09D4A0 7F06AAB0 AFA20068 */  sw    $v0, 0x68($sp)
.L7F06AAB4:
/* 09D4A4 7F06AAB4 8FBF0024 */  lw    $ra, 0x24($sp)
.L7F06AAB8:
/* 09D4A8 7F06AAB8 8FA20068 */  lw    $v0, 0x68($sp)
/* 09D4AC 7F06AABC 27BD0068 */  addiu $sp, $sp, 0x68
/* 09D4B0 7F06AAC0 03E00008 */  jr    $ra
/* 09D4B4 7F06AAC4 00000000 */   nop
)
#endif
#endif





#ifdef NONMATCHING
s32 sub_GAME_7F06A334(s32 arg0) {
    void *sp30;
    s16 sp34;
    s32 sp3C;
    void *sp40;
    s32 sp44;
    s32 sp48;
    s32 sp4C;
    s32 sp50;
    s32 sp54;
    ? temp_ret;
    ? temp_ret_2;
    void *temp_v0;
    s32 temp_s0;
    s32 temp_v1;
    s32 temp_a2;
    s32 phi_s0;
    s32 phi_v1;
    s32 phi_t4;
    s32 phi_s0_2;
    s32 phi_t9;
    s32 phi_s0_3;
    s32 phi_s0_4;

    // Node 0
    sp54 = getCurrentPlayerWeaponId(1);
    temp_ret = getCurrentPlayerWeaponId(0);
    phi_s0_4 = arg0;
    if (temp_ret != 0)
    {
        // Node 1
        sp50 = temp_ret;
        temp_ret_2 = get_ammo_type_for_weapon(temp_ret);
        sp4C = temp_ret_2;
        phi_s0_4 = arg0;
        if (temp_ret_2 != 0)
        {
            // Node 2
            phi_s0_4 = arg0;
            if (g_CurrentPlayer->unk894 != 7)
            {
                // Node 3
                phi_s0_4 = arg0;
                if (g_CurrentPlayer->unk894 != 7)
                {
                    // Node 4
                    phi_s0_4 = arg0;
                    if (bondwalkItemCheckBitflags(sp50, 0x80000) == 0)
                    {
                        // Node 5
                        temp_v0 = ((sp4C * 0xc) + &ammo_related);
                        sp3C = 5;
                        sp30 = temp_v0;
                        sp40 = (void *) temp_v0->unk4;
                        get_ptr_item_statistics(sp50);
                        phi_s0 = arg0;
                        if (sp40 != 0)
                        {
                            // Node 6
                            sp40 = (void *) (sp40 + globalbank_rdram_offset);
                            sp34 = viGetViewTop();
                            sp3C = (s32) sp40->unk4;
                            phi_s0 = set_rgba_redirect_generate_microcode(arg0, sp40, 0x43480000, 0x43340000, (f32) ((viGetViewHeight() + sp34) + -0x14), 0, (f32) sp30->unk8, 1);
                        }
                        // Node 7
                        temp_s0 = microcode_constructor(phi_s0);
                        if (bondwalkItemCheckBitflags(sp50, 0x400000) != 0)
                        {
                            // Node 8
                            sp44 = 0;
                            temp_v1 = (g_CurrentPlayer->unk89C + (g_CurrentPlayer + (sp4C * 4))->unk1130);
                            phi_v1 = temp_v1;
                            if (sp54 == sp50)
                            {
                                // Node 9
                                phi_v1 = (temp_v1 + g_CurrentPlayer->unkC44);
                            }
                            // Node 10
                            sp48 = (s32) phi_v1;
                        }
                        else
                        {
                            // Node 11
                            sp44 = (s32) g_CurrentPlayer->unk89C;
                            sp48 = (s32) (g_CurrentPlayer + (sp4C * 4))->unk1130;
                        }
                        // Node 12
                        phi_s0_2 = temp_s0;
                        if (bondwalkItemCheckBitflags(sp50, 0x400000) == 0)
                        {
                            // Node 13
                            phi_t4 = (sp3C >> 1);
                            if (sp3C < 0)
                            {
                                // Node 14
                                phi_t4 = ((s32) (sp3C + 1) >> 1);
                            }
                            // Node 15
                            phi_s0_2 = gunDrawHudInteger(temp_s0, sp44, (0xc4 - phi_t4), 0, 0);
                        }
                        // Node 16
                        if ((sp48 > 0) || (bondwalkItemCheckBitflags(sp50, 0x400000) != 0))
                        {
                            // Node 18
                            temp_a2 = (sp3C + 1);
                            phi_t9 = (temp_a2 >> 1);
                            if (temp_a2 < 0)
                            {
                                // Node 19
                                phi_t9 = ((s32) (temp_a2 + 1) >> 1);
                            }
                            // Node 20
                            phi_s0_3 = gunDrawHudInteger(phi_s0_2, sp48, (phi_t9 + 0xcb), 1, 0);
                        }
                        else
                        {

                        }
                        // Node 21
                        phi_s0_4 = combiner_bayer_lod_perspective(phi_s0_3);
                    }
                }
            }
        }
    }
    // Node 22
    return phi_s0_4;
}
#else

#if defined(VERSION_US) || defined(VERSION_JP)
GLOBAL_ASM(
.text
glabel sub_GAME_7F06A334
/* 09EE64 7F06A334 27BDFFA8 */  addiu $sp, $sp, -0x58
/* 09EE68 7F06A338 AFB00028 */  sw    $s0, 0x28($sp)
/* 09EE6C 7F06A33C 00808025 */  move  $s0, $a0
/* 09EE70 7F06A340 AFBF002C */  sw    $ra, 0x2c($sp)
/* 09EE74 7F06A344 0FC17674 */  jal   getCurrentPlayerWeaponId
/* 09EE78 7F06A348 24040001 */   li    $a0, 1
/* 09EE7C 7F06A34C AFA20054 */  sw    $v0, 0x54($sp)
/* 09EE80 7F06A350 0FC17674 */  jal   getCurrentPlayerWeaponId
/* 09EE84 7F06A354 00002025 */   move  $a0, $zero
/* 09EE88 7F06A358 1040008E */  beqz  $v0, .L7F06A594
/* 09EE8C 7F06A35C 00402025 */   move  $a0, $v0
/* 09EE90 7F06A360 0FC1A50B */  jal   get_ammo_type_for_weapon
/* 09EE94 7F06A364 AFA20050 */   sw    $v0, 0x50($sp)
/* 09EE98 7F06A368 1040008A */  beqz  $v0, .L7F06A594
/* 09EE9C 7F06A36C AFA2004C */   sw    $v0, 0x4c($sp)
/* 09EEA0 7F06A370 3C0E8008 */  lui   $t6, %hi(g_CurrentPlayer)
/* 09EEA4 7F06A374 8DCEA0B0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 09EEA8 7F06A378 24010006 */  li    $at, 6
/* 09EEAC 7F06A37C 8DC20894 */  lw    $v0, 0x894($t6)
/* 09EEB0 7F06A380 10410084 */  beq   $v0, $at, .L7F06A594
/* 09EEB4 7F06A384 24010007 */   li    $at, 7
/* 09EEB8 7F06A388 10410082 */  beq   $v0, $at, .L7F06A594
/* 09EEBC 7F06A38C 8FA40050 */   lw    $a0, 0x50($sp)
/* 09EEC0 7F06A390 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09EEC4 7F06A394 3C050008 */   lui   $a1, 8
/* 09EEC8 7F06A398 1440007E */  bnez  $v0, .L7F06A594
/* 09EECC 7F06A39C 8FAF004C */   lw    $t7, 0x4c($sp)
/* 09EED0 7F06A3A0 000FC080 */  sll   $t8, $t7, 2
/* 09EED4 7F06A3A4 030FC023 */  subu  $t8, $t8, $t7
/* 09EED8 7F06A3A8 3C198003 */  lui   $t9, %hi(ammo_related)
/* 09EEDC 7F06A3AC 27395EF0 */  addiu $t9, %lo(ammo_related) # addiu $t9, $t9, 0x5ef0
/* 09EEE0 7F06A3B0 0018C080 */  sll   $t8, $t8, 2
/* 09EEE4 7F06A3B4 03191021 */  addu  $v0, $t8, $t9
/* 09EEE8 7F06A3B8 8C480004 */  lw    $t0, 4($v0)
/* 09EEEC 7F06A3BC 24090005 */  li    $t1, 5
/* 09EEF0 7F06A3C0 AFA9003C */  sw    $t1, 0x3c($sp)
/* 09EEF4 7F06A3C4 AFA20030 */  sw    $v0, 0x30($sp)
/* 09EEF8 7F06A3C8 8FA40050 */  lw    $a0, 0x50($sp)
/* 09EEFC 7F06A3CC 0FC1722D */  jal   get_ptr_item_statistics
/* 09EF00 7F06A3D0 AFA80040 */   sw    $t0, 0x40($sp)
/* 09EF04 7F06A3D4 8FA30040 */  lw    $v1, 0x40($sp)
/* 09EF08 7F06A3D8 3C0A8009 */  lui   $t2, %hi(globalbank_rdram_offset)
/* 09EF0C 7F06A3DC 1060001C */  beqz  $v1, .L7F06A450
/* 09EF10 7F06A3E0 00000000 */   nop
/* 09EF14 7F06A3E4 8D4AD0B0 */  lw    $t2, %lo(globalbank_rdram_offset)($t2)
/* 09EF18 7F06A3E8 006A1821 */  addu  $v1, $v1, $t2
/* 09EF1C 7F06A3EC 0C001149 */  jal   viGetViewTop
/* 09EF20 7F06A3F0 AFA30040 */   sw    $v1, 0x40($sp)
/* 09EF24 7F06A3F4 0C00112B */  jal   viGetViewHeight
/* 09EF28 7F06A3F8 A7A20034 */   sh    $v0, 0x34($sp)
/* 09EF2C 7F06A3FC 87AB0034 */  lh    $t3, 0x34($sp)
/* 09EF30 7F06A400 8FAE0030 */  lw    $t6, 0x30($sp)
/* 09EF34 7F06A404 AFA00014 */  sw    $zero, 0x14($sp)
/* 09EF38 7F06A408 004B6021 */  addu  $t4, $v0, $t3
/* 09EF3C 7F06A40C 258DFFEC */  addiu $t5, $t4, -0x14
/* 09EF40 7F06A410 448D2000 */  mtc1  $t5, $f4
/* 09EF44 7F06A414 240F0001 */  li    $t7, 1
/* 09EF48 7F06A418 02002025 */  move  $a0, $s0
/* 09EF4C 7F06A41C 468021A0 */  cvt.s.w $f6, $f4
/* 09EF50 7F06A420 8FA50040 */  lw    $a1, 0x40($sp)
/* 09EF54 7F06A424 3C064348 */  lui   $a2, 0x4348
/* 09EF58 7F06A428 3C074334 */  lui   $a3, 0x4334
/* 09EF5C 7F06A42C E7A60010 */  swc1  $f6, 0x10($sp)
/* 09EF60 7F06A430 C5C80008 */  lwc1  $f8, 8($t6)
/* 09EF64 7F06A434 AFAF001C */  sw    $t7, 0x1c($sp)
/* 09EF68 7F06A438 0FC1A679 */  jal   set_rgba_redirect_generate_microcode
/* 09EF6C 7F06A43C E7A80018 */   swc1  $f8, 0x18($sp)
/* 09EF70 7F06A440 8FB80040 */  lw    $t8, 0x40($sp)
/* 09EF74 7F06A444 00408025 */  move  $s0, $v0
/* 09EF78 7F06A448 93190004 */  lbu   $t9, 4($t8)
/* 09EF7C 7F06A44C AFB9003C */  sw    $t9, 0x3c($sp)
.L7F06A450:
/* 09EF80 7F06A450 0FC2B366 */  jal   microcode_constructor
/* 09EF84 7F06A454 02002025 */   move  $a0, $s0
/* 09EF88 7F06A458 00408025 */  move  $s0, $v0
/* 09EF8C 7F06A45C 8FA40050 */  lw    $a0, 0x50($sp)
/* 09EF90 7F06A460 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09EF94 7F06A464 3C050040 */   lui   $a1, 0x40
/* 09EF98 7F06A468 10400011 */  beqz  $v0, .L7F06A4B0
/* 09EF9C 7F06A46C 3C050040 */   lui   $a1, 0x40
/* 09EFA0 7F06A470 8FA9004C */  lw    $t1, 0x4c($sp)
/* 09EFA4 7F06A474 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09EFA8 7F06A478 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09EFAC 7F06A47C AFA00044 */  sw    $zero, 0x44($sp)
/* 09EFB0 7F06A480 00095080 */  sll   $t2, $t1, 2
/* 09EFB4 7F06A484 8FAD0054 */  lw    $t5, 0x54($sp)
/* 09EFB8 7F06A488 8FAE0050 */  lw    $t6, 0x50($sp)
/* 09EFBC 7F06A48C 004A5821 */  addu  $t3, $v0, $t2
/* 09EFC0 7F06A490 8D6C1130 */  lw    $t4, 0x1130($t3)
/* 09EFC4 7F06A494 8C48089C */  lw    $t0, 0x89c($v0)
/* 09EFC8 7F06A498 15AE0003 */  bne   $t5, $t6, .L7F06A4A8
/* 09EFCC 7F06A49C 010C1821 */   addu  $v1, $t0, $t4
/* 09EFD0 7F06A4A0 8C4F0C44 */  lw    $t7, 0xc44($v0)
/* 09EFD4 7F06A4A4 006F1821 */  addu  $v1, $v1, $t7
.L7F06A4A8:
/* 09EFD8 7F06A4A8 1000000A */  b     .L7F06A4D4
/* 09EFDC 7F06A4AC AFA30048 */   sw    $v1, 0x48($sp)
.L7F06A4B0:
/* 09EFE0 7F06A4B0 3C028008 */  lui   $v0, %hi(g_CurrentPlayer)
/* 09EFE4 7F06A4B4 8C42A0B0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09EFE8 7F06A4B8 8FB9004C */  lw    $t9, 0x4c($sp)
/* 09EFEC 7F06A4BC 8C58089C */  lw    $t8, 0x89c($v0)
/* 09EFF0 7F06A4C0 00194880 */  sll   $t1, $t9, 2
/* 09EFF4 7F06A4C4 00495021 */  addu  $t2, $v0, $t1
/* 09EFF8 7F06A4C8 AFB80044 */  sw    $t8, 0x44($sp)
/* 09EFFC 7F06A4CC 8D4B1130 */  lw    $t3, 0x1130($t2)
/* 09F000 7F06A4D0 AFAB0048 */  sw    $t3, 0x48($sp)
.L7F06A4D4:
/* 09F004 7F06A4D4 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09F008 7F06A4D8 8FA40050 */   lw    $a0, 0x50($sp)
/* 09F00C 7F06A4DC 14400011 */  bnez  $v0, .L7F06A524
/* 09F010 7F06A4E0 02002025 */   move  $a0, $s0
/* 09F014 7F06A4E4 8FA8003C */  lw    $t0, 0x3c($sp)
/* 09F018 7F06A4E8 240D00C4 */  li    $t5, 196
/* 09F01C 7F06A4EC 240E00B1 */  li    $t6, 177
/* 09F020 7F06A4F0 240F0002 */  li    $t7, 2
/* 09F024 7F06A4F4 AFAF0014 */  sw    $t7, 0x14($sp)
/* 09F028 7F06A4F8 AFAE0010 */  sw    $t6, 0x10($sp)
/* 09F02C 7F06A4FC 8FA50044 */  lw    $a1, 0x44($sp)
/* 09F030 7F06A500 05010003 */  bgez  $t0, .L7F06A510
/* 09F034 7F06A504 00086043 */   sra   $t4, $t0, 1
/* 09F038 7F06A508 25010001 */  addiu $at, $t0, 1
/* 09F03C 7F06A50C 00016043 */  sra   $t4, $at, 1
.L7F06A510:
/* 09F040 7F06A510 01AC3023 */  subu  $a2, $t5, $t4
/* 09F044 7F06A514 00003825 */  move  $a3, $zero
/* 09F048 7F06A518 0FC1A723 */  jal   gunDrawHudInteger
/* 09F04C 7F06A51C AFA00018 */   sw    $zero, 0x18($sp)
/* 09F050 7F06A520 00408025 */  move  $s0, $v0
.L7F06A524:
/* 09F054 7F06A524 8FB80048 */  lw    $t8, 0x48($sp)
/* 09F058 7F06A528 8FA40050 */  lw    $a0, 0x50($sp)
/* 09F05C 7F06A52C 5F000006 */  bgtzl $t8, .L7F06A548
/* 09F060 7F06A530 8FA6003C */   lw    $a2, 0x3c($sp)
/* 09F064 7F06A534 0FC1782D */  jal   bondwalkItemCheckBitflags
/* 09F068 7F06A538 3C050040 */   lui   $a1, 0x40
/* 09F06C 7F06A53C 10400012 */  beqz  $v0, .L7F06A588
/* 09F070 7F06A540 00000000 */   nop
/* 09F074 7F06A544 8FA6003C */  lw    $a2, 0x3c($sp)
.L7F06A548:
/* 09F078 7F06A548 240900B1 */  li    $t1, 177
/* 09F07C 7F06A54C 240A0002 */  li    $t2, 2
/* 09F080 7F06A550 24C60001 */  addiu $a2, $a2, 1
/* 09F084 7F06A554 AFAA0014 */  sw    $t2, 0x14($sp)
/* 09F088 7F06A558 AFA90010 */  sw    $t1, 0x10($sp)
/* 09F08C 7F06A55C 02002025 */  move  $a0, $s0
/* 09F090 7F06A560 8FA50048 */  lw    $a1, 0x48($sp)
/* 09F094 7F06A564 04C10003 */  bgez  $a2, .L7F06A574
/* 09F098 7F06A568 0006C843 */   sra   $t9, $a2, 1
/* 09F09C 7F06A56C 24C10001 */  addiu $at, $a2, 1
/* 09F0A0 7F06A570 0001C843 */  sra   $t9, $at, 1
.L7F06A574:
/* 09F0A4 7F06A574 272600CB */  addiu $a2, $t9, 0xcb
/* 09F0A8 7F06A578 24070001 */  li    $a3, 1
/* 09F0AC 7F06A57C 0FC1A723 */  jal   gunDrawHudInteger
/* 09F0B0 7F06A580 AFA00018 */   sw    $zero, 0x18($sp)
/* 09F0B4 7F06A584 00408025 */  move  $s0, $v0
.L7F06A588:
/* 09F0B8 7F06A588 0FC2B3BC */  jal   combiner_bayer_lod_perspective
/* 09F0BC 7F06A58C 02002025 */   move  $a0, $s0
/* 09F0C0 7F06A590 00408025 */  move  $s0, $v0
.L7F06A594:
/* 09F0C4 7F06A594 8FBF002C */  lw    $ra, 0x2c($sp)
/* 09F0C8 7F06A598 02001025 */  move  $v0, $s0
/* 09F0CC 7F06A59C 8FB00028 */  lw    $s0, 0x28($sp)
/* 09F0D0 7F06A5A0 03E00008 */  jr    $ra
/* 09F0D4 7F06A5A4 27BD0058 */   addiu $sp, $sp, 0x58
)
#endif

#if defined(VERSION_EU)
GLOBAL_ASM(
.text
glabel sub_GAME_7F06A334
/* 09D4B8 7F06AAC8 27BDFFA8 */  addiu $sp, $sp, -0x58
/* 09D4BC 7F06AACC AFB00028 */  sw    $s0, 0x28($sp)
/* 09D4C0 7F06AAD0 00808025 */  move  $s0, $a0
/* 09D4C4 7F06AAD4 AFBF002C */  sw    $ra, 0x2c($sp)
/* 09D4C8 7F06AAD8 0FC177A2 */  jal   getCurrentPlayerWeaponId
/* 09D4CC 7F06AADC 24040001 */   li    $a0, 1
/* 09D4D0 7F06AAE0 AFA20054 */  sw    $v0, 0x54($sp)
/* 09D4D4 7F06AAE4 0FC177A2 */  jal   getCurrentPlayerWeaponId
/* 09D4D8 7F06AAE8 00002025 */   move  $a0, $zero
/* 09D4DC 7F06AAEC 1040008E */  beqz  $v0, .L7F06AD28
/* 09D4E0 7F06AAF0 00402025 */   move  $a0, $v0
/* 09D4E4 7F06AAF4 0FC1A6F0 */  jal   get_ammo_type_for_weapon
/* 09D4E8 7F06AAF8 AFA20050 */   sw    $v0, 0x50($sp)
/* 09D4EC 7F06AAFC 1040008A */  beqz  $v0, .L7F06AD28
/* 09D4F0 7F06AB00 AFA2004C */   sw    $v0, 0x4c($sp)
/* 09D4F4 7F06AB04 3C0E8007 */  lui   $t6, %hi(g_CurrentPlayer) # $t6, 0x8007
/* 09D4F8 7F06AB08 8DCE8BC0 */  lw    $t6, %lo(g_CurrentPlayer)($t6)
/* 09D4FC 7F06AB0C 24010006 */  li    $at, 6
/* 09D500 7F06AB10 8DC2088C */  lw    $v0, 0x88c($t6)
/* 09D504 7F06AB14 10410084 */  beq   $v0, $at, .L7F06AD28
/* 09D508 7F06AB18 24010007 */   li    $at, 7
/* 09D50C 7F06AB1C 10410082 */  beq   $v0, $at, .L7F06AD28
/* 09D510 7F06AB20 8FA40050 */   lw    $a0, 0x50($sp)
/* 09D514 7F06AB24 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D518 7F06AB28 3C050008 */   lui   $a1, 8
/* 09D51C 7F06AB2C 1440007E */  bnez  $v0, .L7F06AD28
/* 09D520 7F06AB30 8FAF004C */   lw    $t7, 0x4c($sp)
/* 09D524 7F06AB34 000FC080 */  sll   $t8, $t7, 2
/* 09D528 7F06AB38 030FC023 */  subu  $t8, $t8, $t7
/* 09D52C 7F06AB3C 3C198003 */  lui   $t9, %hi(ammo_related) # $t9, 0x8003
/* 09D530 7F06AB40 27391440 */  addiu $t9, %lo(ammo_related) # addiu $t9, $t9, 0x1440
/* 09D534 7F06AB44 0018C080 */  sll   $t8, $t8, 2
/* 09D538 7F06AB48 03191021 */  addu  $v0, $t8, $t9
/* 09D53C 7F06AB4C 8C480004 */  lw    $t0, 4($v0)
/* 09D540 7F06AB50 24090005 */  li    $t1, 5
/* 09D544 7F06AB54 AFA9003C */  sw    $t1, 0x3c($sp)
/* 09D548 7F06AB58 AFA20030 */  sw    $v0, 0x30($sp)
/* 09D54C 7F06AB5C 8FA40050 */  lw    $a0, 0x50($sp)
/* 09D550 7F06AB60 0FC17359 */  jal   get_ptr_item_statistics
/* 09D554 7F06AB64 AFA80040 */   sw    $t0, 0x40($sp)
/* 09D558 7F06AB68 8FA30040 */  lw    $v1, 0x40($sp)
/* 09D55C 7F06AB6C 3C0A8007 */  lui   $t2, %hi(globalbank_rdram_offset) # $t2, 0x8007
/* 09D560 7F06AB70 1060001C */  beqz  $v1, .L7F06ABE4
/* 09D564 7F06AB74 00000000 */   nop
/* 09D568 7F06AB78 8D4A4490 */  lw    $t2, %lo(globalbank_rdram_offset)($t2)
/* 09D56C 7F06AB7C 006A1821 */  addu  $v1, $v1, $t2
/* 09D570 7F06AB80 0C000FDD */  jal   viGetViewTop
/* 09D574 7F06AB84 AFA30040 */   sw    $v1, 0x40($sp)
/* 09D578 7F06AB88 0C000FBF */  jal   viGetViewHeight
/* 09D57C 7F06AB8C A7A20034 */   sh    $v0, 0x34($sp)
/* 09D580 7F06AB90 87AB0034 */  lh    $t3, 0x34($sp)
/* 09D584 7F06AB94 8FAE0030 */  lw    $t6, 0x30($sp)
/* 09D588 7F06AB98 AFA00014 */  sw    $zero, 0x14($sp)
/* 09D58C 7F06AB9C 004B6021 */  addu  $t4, $v0, $t3
/* 09D590 7F06ABA0 258DFFE2 */  addiu $t5, $t4, -0x1e
/* 09D594 7F06ABA4 448D2000 */  mtc1  $t5, $f4
/* 09D598 7F06ABA8 240F0001 */  li    $t7, 1
/* 09D59C 7F06ABAC 02002025 */  move  $a0, $s0
/* 09D5A0 7F06ABB0 468021A0 */  cvt.s.w $f6, $f4
/* 09D5A4 7F06ABB4 8FA50040 */  lw    $a1, 0x40($sp)
/* 09D5A8 7F06ABB8 3C064348 */  lui   $a2, 0x4348
/* 09D5AC 7F06ABBC 3C074350 */  lui   $a3, 0x4350
/* 09D5B0 7F06ABC0 E7A60010 */  swc1  $f6, 0x10($sp)
/* 09D5B4 7F06ABC4 C5C80008 */  lwc1  $f8, 8($t6)
/* 09D5B8 7F06ABC8 AFAF001C */  sw    $t7, 0x1c($sp)
/* 09D5BC 7F06ABCC 0FC1A85E */  jal   set_rgba_redirect_generate_microcode
/* 09D5C0 7F06ABD0 E7A80018 */   swc1  $f8, 0x18($sp)
/* 09D5C4 7F06ABD4 8FB80040 */  lw    $t8, 0x40($sp)
/* 09D5C8 7F06ABD8 00408025 */  move  $s0, $v0
/* 09D5CC 7F06ABDC 93190004 */  lbu   $t9, 4($t8)
/* 09D5D0 7F06ABE0 AFB9003C */  sw    $t9, 0x3c($sp)
.L7F06ABE4:
/* 09D5D4 7F06ABE4 0FC2B016 */  jal   microcode_constructor
/* 09D5D8 7F06ABE8 02002025 */   move  $a0, $s0
/* 09D5DC 7F06ABEC 00408025 */  move  $s0, $v0
/* 09D5E0 7F06ABF0 8FA40050 */  lw    $a0, 0x50($sp)
/* 09D5E4 7F06ABF4 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D5E8 7F06ABF8 3C050040 */   lui   $a1, 0x40
/* 09D5EC 7F06ABFC 10400011 */  beqz  $v0, .L7F06AC44
/* 09D5F0 7F06AC00 3C050040 */   lui   $a1, 0x40
/* 09D5F4 7F06AC04 8FA9004C */  lw    $t1, 0x4c($sp)
/* 09D5F8 7F06AC08 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09D5FC 7F06AC0C 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D600 7F06AC10 AFA00044 */  sw    $zero, 0x44($sp)
/* 09D604 7F06AC14 00095080 */  sll   $t2, $t1, 2
/* 09D608 7F06AC18 8FAD0054 */  lw    $t5, 0x54($sp)
/* 09D60C 7F06AC1C 8FAE0050 */  lw    $t6, 0x50($sp)
/* 09D610 7F06AC20 004A5821 */  addu  $t3, $v0, $t2
/* 09D614 7F06AC24 8D6C1128 */  lw    $t4, 0x1128($t3)
/* 09D618 7F06AC28 8C480894 */  lw    $t0, 0x894($v0)
/* 09D61C 7F06AC2C 15AE0003 */  bne   $t5, $t6, .L7F06AC3C
/* 09D620 7F06AC30 010C1821 */   addu  $v1, $t0, $t4
/* 09D624 7F06AC34 8C4F0C3C */  lw    $t7, 0xc3c($v0)
/* 09D628 7F06AC38 006F1821 */  addu  $v1, $v1, $t7
.L7F06AC3C:
/* 09D62C 7F06AC3C 1000000A */  b     .L7F06AC68
/* 09D630 7F06AC40 AFA30048 */   sw    $v1, 0x48($sp)
.L7F06AC44:
/* 09D634 7F06AC44 3C028007 */  lui   $v0, %hi(g_CurrentPlayer) # $v0, 0x8007
/* 09D638 7F06AC48 8C428BC0 */  lw    $v0, %lo(g_CurrentPlayer)($v0)
/* 09D63C 7F06AC4C 8FB9004C */  lw    $t9, 0x4c($sp)
/* 09D640 7F06AC50 8C580894 */  lw    $t8, 0x894($v0)
/* 09D644 7F06AC54 00194880 */  sll   $t1, $t9, 2
/* 09D648 7F06AC58 00495021 */  addu  $t2, $v0, $t1
/* 09D64C 7F06AC5C AFB80044 */  sw    $t8, 0x44($sp)
/* 09D650 7F06AC60 8D4B1128 */  lw    $t3, 0x1128($t2)
/* 09D654 7F06AC64 AFAB0048 */  sw    $t3, 0x48($sp)
.L7F06AC68:
/* 09D658 7F06AC68 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D65C 7F06AC6C 8FA40050 */   lw    $a0, 0x50($sp)
/* 09D660 7F06AC70 14400011 */  bnez  $v0, .L7F06ACB8
/* 09D664 7F06AC74 02002025 */   move  $a0, $s0
/* 09D668 7F06AC78 8FA8003C */  lw    $t0, 0x3c($sp)
/* 09D66C 7F06AC7C 240D00C4 */  li    $t5, 196
/* 09D670 7F06AC80 240E00CD */  li    $t6, 205
/* 09D674 7F06AC84 240F0002 */  li    $t7, 2
/* 09D678 7F06AC88 AFAF0014 */  sw    $t7, 0x14($sp)
/* 09D67C 7F06AC8C AFAE0010 */  sw    $t6, 0x10($sp)
/* 09D680 7F06AC90 8FA50044 */  lw    $a1, 0x44($sp)
/* 09D684 7F06AC94 05010003 */  bgez  $t0, .L7F06ACA4
/* 09D688 7F06AC98 00086043 */   sra   $t4, $t0, 1
/* 09D68C 7F06AC9C 25010001 */  addiu $at, $t0, 1
/* 09D690 7F06ACA0 00016043 */  sra   $t4, $at, 1
.L7F06ACA4:
/* 09D694 7F06ACA4 01AC3023 */  subu  $a2, $t5, $t4
/* 09D698 7F06ACA8 00003825 */  move  $a3, $zero
/* 09D69C 7F06ACAC 0FC1A908 */  jal   gunDrawHudInteger
/* 09D6A0 7F06ACB0 AFA00018 */   sw    $zero, 0x18($sp)
/* 09D6A4 7F06ACB4 00408025 */  move  $s0, $v0
.L7F06ACB8:
/* 09D6A8 7F06ACB8 8FB80048 */  lw    $t8, 0x48($sp)
/* 09D6AC 7F06ACBC 8FA40050 */  lw    $a0, 0x50($sp)
/* 09D6B0 7F06ACC0 5F000006 */  bgtzl $t8, .L7F06ACDC
/* 09D6B4 7F06ACC4 8FA6003C */   lw    $a2, 0x3c($sp)
/* 09D6B8 7F06ACC8 0FC1795B */  jal   bondwalkItemCheckBitflags
/* 09D6BC 7F06ACCC 3C050040 */   lui   $a1, 0x40
/* 09D6C0 7F06ACD0 10400012 */  beqz  $v0, .L7F06AD1C
/* 09D6C4 7F06ACD4 00000000 */   nop
/* 09D6C8 7F06ACD8 8FA6003C */  lw    $a2, 0x3c($sp)
.L7F06ACDC:
/* 09D6CC 7F06ACDC 240900CD */  li    $t1, 205
/* 09D6D0 7F06ACE0 240A0002 */  li    $t2, 2
/* 09D6D4 7F06ACE4 24C60001 */  addiu $a2, $a2, 1
/* 09D6D8 7F06ACE8 AFAA0014 */  sw    $t2, 0x14($sp)
/* 09D6DC 7F06ACEC AFA90010 */  sw    $t1, 0x10($sp)
/* 09D6E0 7F06ACF0 02002025 */  move  $a0, $s0
/* 09D6E4 7F06ACF4 8FA50048 */  lw    $a1, 0x48($sp)
/* 09D6E8 7F06ACF8 04C10003 */  bgez  $a2, .L7F06AD08
/* 09D6EC 7F06ACFC 0006C843 */   sra   $t9, $a2, 1
/* 09D6F0 7F06AD00 24C10001 */  addiu $at, $a2, 1
/* 09D6F4 7F06AD04 0001C843 */  sra   $t9, $at, 1
.L7F06AD08:
/* 09D6F8 7F06AD08 272600CB */  addiu $a2, $t9, 0xcb
/* 09D6FC 7F06AD0C 24070001 */  li    $a3, 1
/* 09D700 7F06AD10 0FC1A908 */  jal   gunDrawHudInteger
/* 09D704 7F06AD14 AFA00018 */   sw    $zero, 0x18($sp)
/* 09D708 7F06AD18 00408025 */  move  $s0, $v0
.L7F06AD1C:
/* 09D70C 7F06AD1C 0FC2B06C */  jal   combiner_bayer_lod_perspective
/* 09D710 7F06AD20 02002025 */   move  $a0, $s0
/* 09D714 7F06AD24 00408025 */  move  $s0, $v0
.L7F06AD28:
/* 09D718 7F06AD28 8FBF002C */  lw    $ra, 0x2c($sp)
/* 09D71C 7F06AD2C 02001025 */  move  $v0, $s0
/* 09D720 7F06AD30 8FB00028 */  lw    $s0, 0x28($sp)
/* 09D724 7F06AD34 03E00008 */  jr    $ra
/* 09D728 7F06AD38 27BD0058 */   addiu $sp, $sp, 0x58
)
#endif
#endif




void gunSetSightVisible(s32 reason, bool visible)
{
    if (visible)
    {
        g_CurrentPlayer->gunsightmode &= ~reason;
        return;
    }

    g_CurrentPlayer->gunsightmode |= reason;
}


void gunDrawSight(s32 *gdl) {

    s32 sp54;
    f32 xypos[2];
    f32 halfedxy[2];

    if ((g_CurrentPlayer->gunsightmode == 0) && (g_CurrentPlayer->mpmenuon == FALSE)) {
        sp54 = *gdl;
        texSelect(&sp54, crosshairimage, 4, 0, 0);

        xypos[0] = g_CurrentPlayer->crosshair_angle.f[0];
        xypos[1] = g_CurrentPlayer->crosshair_angle.f[1];
        halfedxy[0] = 16.0f;
        halfedxy[1] = 16.0f;

        if (get_screen_ratio() == SCREEN_RATIO_16_9) {
            halfedxy[0] = halfedxy[0] * 0.75f;
        }
#ifdef VERSION_EU
        halfedxy[1] = halfedxy[1] * 1.19047617912f;
#endif
        display_image_at_position(&sp54, &xypos, &halfedxy, 0x20, 0x20, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0x6E, (crosshairimage->level > 0), 0);
        *gdl = sp54;
    }
}



void inc_curplayer_hitcount_with_weapon(ITEM_IDS item, SHOT_REGISTER shot_register) {

    if (bondwalkItemCheckBitflags(item, WEAPONSTATBITFLAG_PLAYER_STAT_HIT)) {
        g_playerPerm->shot_count[shot_register] = g_playerPerm->shot_count[shot_register]+1;
    }
}

s32 get_curplayer_shot_register(SHOT_REGISTER shot_register)
{
  return g_playerPerm->shot_count[shot_register];
}

void inc_cur_civilian_casualties(void)
{
    g_playerPerm->killed_civilians++;
}

s32 get_civilian_casualties(void)
{
    return g_playerPerm->killed_civilians;
}


//D:80053BF8
const char aSD[] = "%s: %d\n";

void increment_num_kills_display_text_in_MP(void)
{
    s8 buffer[256];
    s32 time_since_kill;
    s32 recent_kill_count;
    s32 mission_time;
    s32 unused; // needed this variable to match

    g_playerPerm->kill_count += 1;
    g_CurrentPlayer->field_29F8 += 1;

    if (getPlayerCount() < 2) { return; }

    mission_time = getMissiontimer();
    sprintf(&buffer, aSD, langGet(getStringID(LGUN, GUN_STR_DA_KILLCOUNT)), g_playerPerm->kill_count); // "kill count"

#if defined(VERSION_US)
    hudmsgBottomShow(&buffer);
#elif defined(VERSION_JP) || defined(VERSION_EU)
    jp_hudmsgBottomShow(&buffer);
#endif

    if (g_playerPerm->kill_count >= 2)
    {
        time_since_kill = mission_time - g_CurrentPlayer->last_kill_time[0];
        if (g_playerPerm->max_time_between_kills < time_since_kill)
        {
            g_playerPerm->max_time_between_kills = time_since_kill;
        }

        if (time_since_kill < g_playerPerm->min_time_between_kills)
        {
            g_playerPerm->min_time_between_kills = time_since_kill;
        }
    }

    recent_kill_count = 1;
    g_CurrentPlayer->last_kill_time[3] = g_CurrentPlayer->last_kill_time[2];
    g_CurrentPlayer->last_kill_time[2] = g_CurrentPlayer->last_kill_time[1];
    g_CurrentPlayer->last_kill_time[1] = g_CurrentPlayer->last_kill_time[0];
    g_CurrentPlayer->last_kill_time[0] = mission_time;

    // I tried to turn this into a loop but it didn't match
    if (g_CurrentPlayer->last_kill_time[1] != -1 && (g_CurrentPlayer->last_kill_time[0] - g_CurrentPlayer->last_kill_time[1]) < 0x78)
    {
        recent_kill_count++;
        if ((g_CurrentPlayer->last_kill_time[2] != -1) && ((g_CurrentPlayer->last_kill_time[0] - g_CurrentPlayer->last_kill_time[2]) < 0x78))
        {
            recent_kill_count++;
            if ((g_CurrentPlayer->last_kill_time[3] != -1) && ((g_CurrentPlayer->last_kill_time[0] - g_CurrentPlayer->last_kill_time[3]) < 0x78))
            {
                recent_kill_count++;
            }
        }
    }

    if (g_playerPerm->most_killed_one_time < recent_kill_count)
    {
        g_playerPerm->most_killed_one_time = recent_kill_count;
    }
}



s32 get_curplay_killcount(void) {
    return g_playerPerm->kill_count;
}

void increment_num_times_killed_MwtGC(void){
    g_playerPerm->killed_gg_owner_count++;
}

s32 get_times_killed_mwtgx(void) {
    return g_playerPerm->killed_gg_owner_count;
}


void increment_num_deaths(void)
{
	char buffer[256];
    g_CurrentPlayer->deathcount = (s32) (g_CurrentPlayer->deathcount + 1);
    if (getPlayerCount() >= 2)
    {
        if (g_CurrentPlayer->deathcount == 1)
        {
            sprintf(buffer, langGet(getStringID(LGUN, GUN_STR_DB_DIEDONCE_LF))); //died once
        }
        else
        {
            sprintf(buffer, "%s %d %s\n", langGet(getStringID(LGUN, GUN_STR_DC_DIED)), g_CurrentPlayer->deathcount, langGet(getStringID(LGUN, GUN_STR_DD_TIMES))); //died times
        }
#if defined(VERSION_JP) || defined(VERSION_EU)
		jp_hudmsgBottomShow(buffer);
#else
		hudmsgBottomShow(buffer);
#endif
    }
}


s32 get_curplayer_numdeaths(void) {
    return g_CurrentPlayer->deathcount;
}

//D:80053C0C
const char aSD_0[] = "%s: %d\n";

void increment_num_suicides_display_MP(void) {
    char buffer[256];
    s32 time_diff;
    s32 recent_kill_count;
    s32 currentTime;

    g_CurrentPlayer->num_suicides += 1;
    if (getPlayerCount() >= 2) {

        currentTime = getMissiontimer();

        sprintf(&buffer, &aSD_0, langGet(getStringID(LGUN, GUN_STR_DE_SUICIDECOUNT)), g_CurrentPlayer->num_suicides); // "suicide count"

#if defined(VERSION_JP) || defined(VERSION_EU)
		jp_hudmsgBottomShow(&buffer);
#else
		hudmsgBottomShow(&buffer);
#endif

        if (g_playerPerm->kill_count >= 2) {
            time_diff = currentTime - g_CurrentPlayer->last_kill_time[0];
            if (g_playerPerm->max_time_between_kills < time_diff) {
                g_playerPerm->max_time_between_kills = time_diff;
            }
            if (time_diff < g_playerPerm->min_time_between_kills) {
                g_playerPerm->min_time_between_kills = time_diff;
            }
        }
        recent_kill_count = 1;
        g_CurrentPlayer->last_kill_time[3] = g_CurrentPlayer->last_kill_time[2];
        g_CurrentPlayer->last_kill_time[2] = g_CurrentPlayer->last_kill_time[1];
        g_CurrentPlayer->last_kill_time[1] = g_CurrentPlayer->last_kill_time[0];
        g_CurrentPlayer->last_kill_time[0] = currentTime;

        if ( g_CurrentPlayer->last_kill_time[1] != -1) {

            if ((g_CurrentPlayer->last_kill_time[0] - g_CurrentPlayer->last_kill_time[1]) < 0x78) {

                recent_kill_count += 1;

                if ((g_CurrentPlayer->last_kill_time[2] != -1) && ((g_CurrentPlayer->last_kill_time[0] - g_CurrentPlayer->last_kill_time[2]) < 0x78)) {

                    recent_kill_count += 1;

                    if ((g_CurrentPlayer->last_kill_time[3] != -1) && ((g_CurrentPlayer->last_kill_time[0] - g_CurrentPlayer->last_kill_time[3]) < 0x78)) {
                        recent_kill_count += 1;
                    }
                }
            }
        }

        if (g_playerPerm->most_killed_one_time < recent_kill_count) {
            g_playerPerm->most_killed_one_time = recent_kill_count;
        }
    }
}

s32 get_curplayer_numsuicides(void) {
    return g_CurrentPlayer->num_suicides;
}
