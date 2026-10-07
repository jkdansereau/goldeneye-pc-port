#ifndef _FILE2_H_
#define _FILE2_H_
#include <ultra64.h>

#include <bondconstants.h>
#include <bondtypes.h>
#include "file.h"



/* EEPROM masks for in-game settings */
#define OPTION_INVERTLOOK    0x0001
#define OPTION_AUTOAIM       0x0002
#define OPTION_AIMCONTROL    0x0004
#define OPTION_SIGHTONSCREEN 0x0008
#define OPTION_LOOKAHEAD     0x0010
#define OPTION_DISPLAYAMMO   0x0020
#define OPTION_SCREENWIDE    0x0040
#define OPTION_SCREENRATIO   0x0080
#define OPTION_CONTROLTYPE   0x0700
#define OPTION_SCREENCINEMA  0x0800

#ifdef PORT
/* D557 (Rule-2 sign-off: maintainer 2026-10-06): new/blank profiles start with
 * Look ahead OFF on PC -- its pitch auto-centring fights mouse and right-stick
 * look (PD's PC port likewise keeps it on only for N64). Existing saves keep
 * their stored option bits; only BLANKSAVEDATA-initialised profiles change. */
#define DEFAULT_OPTIONS (OPTION_AUTOAIM | OPTION_SIGHTONSCREEN | OPTION_DISPLAYAMMO)
#else
#define DEFAULT_OPTIONS (OPTION_AUTOAIM | OPTION_SIGHTONSCREEN | OPTION_LOOKAHEAD | OPTION_DISPLAYAMMO)
#endif

extern ChrRecord *g_CurModelChr;

u8 fileGetBondForFolder(u32 folder);
void fileValidateSaves(void);
bool fileGetIsCheatUnlocked(save_data *save, s32 cheat);
STAGESTATUS fileIsStageUnlockedAtDifficulty(s32 foldernum, LEVEL_SOLO_SEQUENCE levelid, DIFFICULTY difficulty);
void fileUnlockStageInFolderAtDifficulty(s32 foldernum, LEVEL_SOLO_SEQUENCE levelid, DIFFICULTY difficulty, s32 newtime);
void fileSaveFolderUnlockCheat(s32 foldernum, s32 cheat);
void fileLoadSettingsForFolder(u32 folder);
void fileDeleteSaveForFolder(s32 foldernum);

void fileGetHighestStageDifficultyCompletedForFolder(s32 foldernum, LEVEL_SOLO_SEQUENCE *levelid, DIFFICULTY *difficulty);
bool check_aztec_completed_any_folder_secret_00(void);
bool fileIsEgyptCompletedOn00AnyFolder(void);
LEVEL_SOLO_SEQUENCE fileGetHighestStageUnlockedAnyFolder(void);

#endif
