/*
 * pc_protos.h - PC-port-only prototypes for implicitly declared functions.
 *
 * D38 (docs/internals.md section F): the decomp has ~72 translation units
 * that call ~400 functions without any visible prototype (missing #include of
 * the declaring header). Under C11 an implicit declaration assumes `int f()`,
 * which on N64 (MIPS, 32-bit pointers) is harmless, but on x86-64 it silently
 * truncates every pointer (and 64-bit scalar) return value to 32 bits - e.g.
 * tokenFind() in set_mt_tex_alloc() returned a low-32-bit "pointer" that then
 * faulted in strtol().
 *
 * This header declares each of those functions with its TRUE return type and
 * an empty parameter list (no argument checking, no dependency on the
 * parameter types' headers). Empty-paren declarations are compatible with the
 * real prototypes elsewhere, so this is purely additive: it changes only the
 * width of the returned value, restoring N64-correct semantics on x86-64.
 *
 * Anchored in port/shim/PR/ucode.h (PC-only shim): ucode.h is the LAST include
 * of <ultra64.h>, so by the time this header runs every PR type libaudio.h /
 * gbi.h needs is already defined. (Anchoring earlier, e.g. in gbi.h, poisons
 * libaudio.h: the bondtypes chain reaches snd.h -> <PR/libaudio.h> while
 * ultra64.h is still mid-parse.) Inert in the N64 build (no -DPORT).
 *
 * C TUs only: C++ translation units (port/fast3d/*.cpp) also reach this header
 * (SDL_stdinc.h -> <stdarg.h> -> port/shim/stdarg.h -> include/stdarg.h ->
 * <ultra64.h>), but pulling the bondtypes/bondconstants chain into C++ breaks
 * on `struct ALSoundState*` in src/bondtypes.h (C forbids nothing, C++ does:
 * "using typedef-name after 'struct'"), and no C++ TU has an implicit
 * declaration needing a fix here. C++ TUs keep their pre-D38 header exposure.
 */
#ifndef _PC_PROTOS_H_
#define _PC_PROTOS_H_

#if defined(PORT) && defined(__x86_64__) && !defined(__cplusplus)

#include <PR/ultratypes.h> /* u8..s32, f32, size_t (host on PC) */
#include <PR/gbi.h>        /* Gfx, Mtx, Vtx, Light (shimmed on PC) */
#include "bondtypes.h"   /* coord3d, PropRecord, ObjectRecord, ModelFileHeader, bool, ITEM_IDS */
#include "bondconstants.h" /* MPSCENARIOS, OBJECTIVESTATUS, PROP, DIFFICULTY, TICKOP */
#include "game/file.h"   /* save_data */
#include "game/ob.h"     /* FILELOADMETHOD (for _fileNameLoadToBank below) */

#pragma push_macro("assert")
#undef assert
void assert();
#pragma pop_macro("assert")

/* D38: host byte-order functions replacing the CharArrayTo16/32 macros that
 * src/bondconstants.h used to define (neutralized in port/shim/bondconstants.h
 * because they break <winsock.h> parsing; see D38). Declared only when
 * winsock has not already declared them, so TUs that include <windows.h>
 * before <ultra64.h> don't get a dllimport redeclaration warning. Defined in
 * port/src/pc_netorder.c (little-endian byte swap == CharArrayTo16/32). */
#if !defined(__WINSOCK_H) && !defined(_WINSOCK2_H)
/* Match winsock's own signatures (u_short/u_long) so TUs that parse
 * <windows.h> later redeclare compatibly; on LLP64 u_long is 64-bit and the
 * high bits are unused for the 16/32-bit values game code passes. */
unsigned short ntohs(unsigned short);
unsigned long ntohl(unsigned long);
#endif

int GetCurrentThreadStackLimits();
int _Printf();
s32 __scSchedule();
s32 __scTaskComplete();
void * _fileNameLoadToBank(char *filename, FILELOADMETHOD loadMethod, s32 size, u8 bank);
void abort();
void add_ammo_to_inventory();
void ai();
void alloc_false_GUARDdata_to_exec_global_action();
void alloc_init_GUARDdata_entries();
void alloc_load_expand_ani_table();
void assert();
char * bgDebPrintROOMID();
void bgFindRoomsAlongSegment();
s32 bgGetConnectedRooms();
bool bgProjectRoomCoordToScreen();
Gfx* bgScissorCurrentPlayerViewDefault();
bool bgTestRayIntersectsBbox();
int bondinvAddPropToInv();
void bondinvAddTextOverride();
int bondinvAddWeaponByProp();
bool bondinvCheckHasKeyFlags();
s32 bondinvCountTotalItemsInInv();
void bondinvDetermineEquippedItem();
u8 * bondinvGetActivatedTextObject();
u8 * bondinvGetActivatedTextWeapon();
s32 bondinvGetAllGunsFlag();
int bondinvGetCurEquippedItem();
u16 * bondinvGetFirstTitlebyIndex();
u16 * bondinvGetLongNameByIndex();
u16 * bondinvGetSecondTitlebyIndex();
s32 bondinvGetTextbyInvIndex();
s32 bondinvGetWeaponOfChoice();
bool bondinvHasGEKey();
bool bondinvHasPropInInv();
void bondinvIncrementHeldTime();
void bondinvSetAllGunsFlag();
void bondinvSetCurEquippedItem();
u32 bondviewGetCameraMode();
coord3d * bondviewGetCurrentPlayersPosition3();
s32 bondviewGetVisibleToGuardsFlag();
void bondviewKillCurrentPlayer();
void bondviewRemovePlayerBody();
void bondviewSetCameraMode();
void bondviewSetCurrentPlayerPosition();
void bondviewSetVisibleToGuardsFlag();
void bondviewUpdateCameraMatrices();
void bondviewUpdateFrustumPlanes();
void bossEntry();
void bullet_path_from_screen_center();
void * calloc();
void casingsInit();
bool cheatIsActive();
s32 check_cur_player_ammo_amount_in_inventory();
bool check_if_imageID_is_light();
u32 check_ramrom_flags();
s32 chrGetNumFree();
bool chrHasStageFlag();
void chrObjRandomSetSeed();
void chrSetWeaponFlag4();
s32 chrTick();
void chrobjSndCreatePostEventDefault();
void chrpropDeregisterRoom(PropRecord *, s16);
void chrpropDetach();
PropRecord * chrpropGetActiveTail();
void cleanupAlarms();
void cleanupExplosions();
void cleanupGuardData();
void cleanupObjectSounds();
void cleanupObjectives();
void cleanupSFXRelated();
void cleanup_REMOVED_();
void cleanup_rooms();
void cleanup_window_pieces();
void cleanupplayersoundrelated();
void clearChrGunModelInstances();
void clear_light_fixturetable_in_room();
void clear_ramrom_block_buffer_heading_ptrs();
void crashDumpThreads();
void cur_player_set_aim_control();
void cur_player_set_ammo_onscreen_setting();
void cur_player_set_lookahead();
void cur_player_set_screen_setting();
void cur_player_set_sight_onscreen_control();
void currentPlayerCreateRocket();
s32 currentPlayerGetCrouchPos();
Mtx * currentPlayerGetMatrix10C8();
bool currentPlayerGetXAutoAimEnabled();
bool currentPlayerGetYAutoAimEnabled();
void currentPlayerSetCameraMode();
void currentPlayerSetMatrix10C4();
void currentPlayerSetMatrix10C8();
void * currentPlayerSetMatrix10CC();
void currentPlayerSetProjectionMatrix();
void currentPlayerSetProjectionMatrixF();
void currentPlayerSetViewToWorldMtxf();
void currentPlayerSetXAutoAimEnabled();
void currentPlayerSetYAutoAimEnabled();
void currentPlayerUnEquipWeaponWrapper();
s32 darkened_light_table_contains_vertex();
void debTryAdd();
void debmenuPrintString();
void debmenuResetBuffer();
void debmenuSetFgColour();
void debmenuSetMenu();
void debmenuSetPos();
void debug_object_load_all_models();
void debug_weapon_load_table();
void default_player_perspective_and_height();
Gfx * display_red_blue_on_radar();
void display_text_for_weapon_in_lower_left_corner();
void doorDeactivatePortal();
PropRecord* doorInit();
bool doorIsClosed();
bool doorIsPadlockFree();
bool doorTestForInteract();
void * dynAllocate();
Light * dynAllocateLights();
Mtx * dynAllocateMatrix();
Vtx * dynAllocateVertices();
s32 dynGetFreeGfx();
s32 dynGetFreeVtx();
void explosionClearBulletImpactRoom();
void explosionClearBulletImpactRoomByFlag(PropRecord *, s8);
void fileClearSavefileForFolder();
void fileCopyDemoSaveToRamRomSave();
void fileCopyFolderToFirstFree();
void fileCopySave();
void fileGenerateCRC();
save_data * fileGetSaveForFoldernum();
s32 fileGetSaveStageDifficultyTime();
s32 fileIs007ModeUnlocked();
bool fileIsAztecCompletedOnSecretOr00ForFolder();
bool fileIsCradleCompletedAnyFolder();
bool fileIsCradleCompletedForFolder();
bool fileIsEgyptCompletedOn00ForFolder();
void fileLoad();
void fileResetRamRomSave();
void fileUpdateSelectedBondInSave();
s32 fogGetPropDistColor();
void fogLoadLevelEnvironment();
void free();
int frontGetPlayersFavoriteWeaponInHand();
void generate_player_thrown_grenade();
void generate_player_thrown_knife();
void generate_player_thrown_object();
PropRecord* getCurrentPlayerProp();
ITEM_IDS getCurrentPlayerWeaponId();
int getCurrentWeaponOrItem();
s32 getIndexOfPORTALID();
s32 getMPWeaponSet();
s32 getMaxNumRooms();
s32 getMissiontimer();
s32 getPlayerCount();
u8 * getPlayerWeaponBufferForHand();
PROP getPropForHeldItem();
void getRoomPositionScaledByIndex();
u32 getSizeBufferWeaponInHand();
s32 get_ammo_type_for_weapon();
u8 get_bondata_invincible_flag();
s32 get_cur_playernum();
s32 get_curplay_killcount();
s32 get_curplayer_shot_register();
s32 get_debug_007_unlock_flag();
s32 get_debug_all_obj_complete_flag();
s32 get_debug_explosioninfo_flag();
s32 get_debug_gunwatchpos_flag();
f32 get_depth_offset_solo_watch_menu_inventory_page_for_item();
f32 get_depth_on_solo_watch_menu_page_for_item();
s32 get_difficulty_for_objective();
s32 get_highlighted_debug_option();
f32 get_horizontal_offset_on_solo_watch_menu_for_item();
s32 get_is_ramrom_flag();
s32 get_itemtype_in_hand();
s32 get_keyanalyzer_flag();
f32 get_lateral_position_solo_watch_menu_main_page_for_item();
s32 get_obj_collision_flag();
s32 get_pc_buffer_remaining_value();
u32 get_player_control_style();
u16 get_player_mp_char_body();
u16 get_player_mp_char_head();
s32 get_players_team_or_scenario_item_flag();
u16 * get_ptr_first_title_line_item();
ModelFileHeader * get_ptr_itemheader_in_hand();
u16 * get_ptr_long_watch_text_for_item();
u16 * get_ptr_second_title_line_item();
u8 * get_ptr_text_for_watch_breifing_page();
s32 get_recording_ramrom_flag();
MPSCENARIOS get_scenario();
s32 get_show_patrols_flag();
OBJECTIVESTATUS get_status_of_objective();
u8 * get_text_for_objective();
f32 get_vertical_offset_on_solo_watch_menu_for_item();
f32 get_vertical_position_solo_watch_menu_main_page_for_item();
f32 get_xrotation_solo_watch_menu_for_item();
f32 get_yrotation_solo_watch_menu_for_item();
s32 getmusictrack_or_randomtrack();
void gotoAboveDebugOption();
void gotoBelowDebugOption();
void gotoLeftDebugOption();
void gotoRightDebugOption();
Gfx * gunDrawWatchAmmoDisplay();
void gunSpawnGLGrenade();
void gunUpdateAttachedRocket();
void handle_alarm_gas_timer_calldamage();
void hatAssignToChr();
PropRecord * hatCreateForChr();
void image_entries_load();
void increment_num_deaths();
void increment_num_suicides_display_MP();
void increment_num_times_killed_MwtGC();
u8 * indycommHostCheckFileExists();
void indycommHostRamRomLoad();
void indycommHostSaveFile();
void initAnimationsBuffer();
void initGameData();
void initWeaponAnimGroups();
void init_player_gait_object();
void init_watch_at_start_of_stage();
void init_weapon_animation_groups_maybe();
void initializeDebugCameraPosition();
void initializeGunBarrelIntro();
void initializeRoomData();
s32 interface_menu0B_runstage();
bool intersectRayTriangle();
s32 isGunBarrelInMode2();
s32 isGunBarrelInMode9();
bool is_clock_drawn_onscreen();
/* Functions whose true prototype has default-promoted (s8/u16/char) params
 * CANNOT be declared with empty parentheses (C11 6.7.6.3p16: a promoted arg
 * type cannot match an empty parameter name list). For those, emit the full
 * prototype with the TRUE parameter types (int would be an incompatible
 * adjusted type, not a compatible one). */
s8 joy7000C174(s8);
s8 joy7000C284(s8);
void joyConsumeSamplesWrapper();
u16 joyGetButtons(s8, u16);
u16 joyGetButtonsPressedThisFrame(s8, u16);
s8 joyGetControllerCount();
s8 joyGetStickX(s8);
s32 joyGetStickXInRange(s8, s32, s32);
s8 joyGetStickY(s8);
s32 joyGetStickYInRange(s8, s32, s32);
void langClearBank();
u8 * langGet();
void langInit();
void lightFixtureBreak();
void lightFixtureInitTables();
void load_ramrom_from_devtool();
void loop_set_sound_effect_all_slots();
s32 lvlGetCurrentStageToLoad();
DIFFICULTY lvlGetSelectedDifficulty();
void * malloc();
void matrix_4x4_7F058C64();
void matrix_4x4_7F058C88();
void matrix_4x4_invert_affine();
void * memaAlloc();
void memaFree();
s32 memaGetLongestFree();
int memcmp();
void * memcpy();
u32 modelFindNextProjectileHitCandidate();
void modelGetXYExtents();
s32 modelLoad();
bool modelTestRayIntersectsNodeBBox();
void modelmgrAllocateAnimModelSlots();
void modelmgrAllocateModelSlots();
void mpwatchMenuTick();
void mtxLoadRandomRotation();
void null_init_main_1();
void null_init_main_2();
void null_init_main_3();
void obInit();
void obLoadBGFileBytesAtOffset();
bool objGetOnscreenRenderBounds();
void objHit();
bool objIsHealthy();
bool objTestForInteract();
s32 objTick();
s32 objectiveGetCount();
bool objectiveIsAllComplete();
void objectivestatusCheckRoomEntered();
void piCreateManager();
s32 playerGetCrouchPos();
s32 playerTick();
Gfx * process_monitor_animation_microcode();
s32 propDoorGetCdTypes();
TICKOP propdoorInteract();
bool propobjFindHit();
u32 randomGetNext();
void redarken_lights_in_room();
void removed_debug_roomblocks_feature();
Gfx * renderGunbarrelEyeIntroSequence();
void replay_recorded_ramrom_from_indy();
void resetDebugCameraToPlayerPosition();
void reset_counter_rand_body_head();
Gfx * retrieve_display_rareware_logo();
void rle_expand_8bit();
s32 rmonStatus();
void romCopy();
void save_img_index_to_obj_ani_slot();
void save_ramrom_to_devtool();
void select_ramrom_to_play();
void setMPWeaponSet();
void setRamRomRecordSlot();
void * setSPToEnd();
void setSixExplosionAndSmokeEntries();
void setTextOrientation();
void set_BONDdata_field_10E0();
void set_BONDdata_outside_watch_menu_flag();
void set_bondata_invincible_flag();
void set_cur_player();
void set_cur_player_look_vertical_inverted();
Gfx * set_enviro_fog_for_items_in_solo_watch_menu();
void set_favorite_weapon_for_every_player();
void set_gu_scale();
void set_max_ammo_for_cur_player();
void set_missionstate();
void set_missionstate_zero();
void set_obj_collision_flag();
void set_players_team_or_scenario_item_flag();
void set_show_patrols_flag();
void set_sound_effect_for_weapontype_collection();
ObjectRecord * setupFindObjForReuse();
void setupRarewareLogoData();
void setupUpdateObjectRoomPosition();
s32 sizepropdef();
Gfx * skyRender();
void skySetStageNum();
void skyTick();
/* Exact match of GCC's built-in (and MSVCRT) prototype; size_t is
 * `unsigned long long` on this MinGW toolchain. */
int snprintf(char *, unsigned long long, const char *, ...);
PropRecord * something_with_generating_object();
void speedgraphInit();
void speedgraphMarkerUpdate();
void stanDetermineEOF();
void stanLoadFile();
bool stanTileHasZeroArea();
void stop_recording_ramrom();
void store_favorite_weapon_current_player();
void store_osgetcount();
char * strcat();
int strcmp();
char * strcpy();
size_t strlen();
char * strncpy();
long int strtol();
void sub_GAME_7F008DE4();
s32 sub_GAME_7F03DB70();
void sub_GAME_7F04E9BC();
void sub_GAME_7F057DF8();
void sub_GAME_7F059334();
void sub_GAME_7F05C614();
void sub_GAME_7F05D690();
void sub_GAME_7F05E6B4();
void sub_GAME_7F05E83C();
void sub_GAME_7F05E978();
Gfx * sub_GAME_7F061E18();
u16 sub_GAME_7F06D2E4();
s32 sub_GAME_7F074CAC();
void sub_GAME_7F078464();
Gfx* sub_GAME_7F09343C();
Gfx* sub_GAME_7F09365C();
void sub_GAME_7F0A6A80();
int sub_GAME_7F0AC0E8();
/* Region-dependent signature (bg.c): EU is void/4-arg, US/JP is s32/5-arg. */
#if defined(VERSION_EU)
void sub_GAME_7F0B7F84();
#else
s32 sub_GAME_7F0B7F84();
#endif
s32 sub_GAME_7F0B993C();
s32 sub_GAME_7F0B9F14();
void sub_GAME_7F0BD8FC();
void sub_GAME_7F0C1310();
s32 texBuildLookup();
s32 texChannelsToPixels();
void texCopyGdls();
s32 texGetWidthAtLod();
s32 texInflateLookup();
s32 texInflateLookupFromBuffer();
void texLoadFromDisplayList();
s32 texLoadFromGdl();
s32 texReadUncompressed();
void texSelect();
s32 texShrinkNonPaletted();
long long time();
const char * tokenFind();
s32 tokenReadIo();
void transform3Dto2DWithZScaling();
void updateDebugCameraWorldPosition();
void updateFrameCounters();
void used_to_load_1st_person_model_on_demand();
void viDebugRemoved();
u16 viGetPerspNorm();
s16 viGetViewHeight();
s16 viGetViewLeft();
s16 viGetViewTop();
s16 viGetViewWidth();
s16 viGetX();
s16 viGetY();
void viSetColorMode16Bit();
void viSetColorMode32Bit();
s32 vtxstore_allocate();
Gfx* watchRenderController();
Gfx * watchRenderControllerOpaque();
u32 weaponLoadProjectileModels();
void zbufSetBuffer();

#endif /* PORT && __x86_64__ && !__cplusplus */
#endif /* _PC_PROTOS_H_ */
