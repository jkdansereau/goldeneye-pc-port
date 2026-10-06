#ifndef PORT_WATCHSETTINGS_H
#define PORT_WATCHSETTINGS_H

/* D349: one source of truth for Bond-file-backed GE settings. All GE state and
 * EEPROM access runs on the game thread. F10 sends requests to that thread. */
enum WatchSettingField {
    WATCH_SETTING_MUSIC,
    WATCH_SETTING_FX,
    WATCH_SETTING_LOOK,
    WATCH_SETTING_AUTOAIM,
    WATCH_SETTING_AIMCONTROL,
    WATCH_SETTING_SIGHT,
    WATCH_SETTING_LOOKAHEAD,
    WATCH_SETTING_AMMO,
    WATCH_SETTING_CONTROL,   /* D516: control style 0..7 (save options bits 8-10), per seat; stage only */
    WATCH_SETTING_COUNT
};

int watchSettingsFieldForKey(const char *key); /* -1 for non-watch row */
int watchSettingsFolder(void);                /* explicit front chooser; -1 = none */
/* D356: the chooser cycles folders only -- "none" is retired from the UI
 * surface (plan §5.5): the front target is always a folder (frontFolder()
 * resolves -1 to a valid folder lazily, D354; the auto-init fallback builds
 * file 1 if none exists). The UI shows an unavailable state until the
 * target's file actually exists instead of a number the write path cannot
 * use. */
void watchSettingsChooseFile(int dir);        /* game thread only */
int watchSettingsAvailable(void);
int watchSettingsRead(enum WatchSettingField field); /* front: saved file, F10: snapshot */
void watchSettingsSet(enum WatchSettingField field, int value, int commit);
void watchSettingsCommit(enum WatchSettingField field); /* slider release */
void watchSettingsGameTick(void);             /* game thread; after gfxFrameMsgQ receive */

/* D356: context-aware, READ-ONLY active-file accessor (never mutates the
 * stored front target): in stage -> the stage's active file; front
 * contexts -> the resolved front target (frontFolder()'s lazy default).
 * -1 when nothing usable (e.g. the auto-init fallback not yet applied).
 * Drives the "(File N)" title annotations in the F10 overlay and the front
 * options screen (plan §5.5). */
int watchSettingsActiveFolder(void);
/* D356: the BLANKSAVEDATA value of a field -- the per-file "default" the
 * reset-to-defaults rows restore (plan §5.3/§5.6). */
int watchSettingsBlankValue(enum WatchSettingField field);
/* D352: queued field commands awaiting the game thread (0 in front
 * contexts -- nothing is queued there). Drives the in-stage reset probe's
 * dispatch check. */
int watchSettingsQueueCount(void);
/* D516: which controller seat WATCH_SETTING_CONTROL reads/edits (the F10
 * Controller page's player). Any thread. WATCH_SETTING_CONTROL reads -1 when
 * the seat has no live player (front end, seat beyond the player count). */
void watchSettingsSetControlSeat(int seat);

#endif
