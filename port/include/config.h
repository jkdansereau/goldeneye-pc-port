#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/*
 * INI configuration (ge007.ini).
 * Modelled on the PD port's port/include/config.h.
 *
 * A tiny key=value store. Options register themselves (usually via a
 * PD_CONSTRUCTOR) and are loaded from / saved to the ini in the data dir.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load the config file (no-op if it doesn't exist yet). */
void configLoad(void);
/* Save the current values back to the config file. */
void configSave(void);

/* Register an integer option. min==max disables clamping. */
void configRegisterInt(const char *key, int *value, int min, int max);
/* Register an unsigned option. min==max disables clamping. */
void configRegisterUInt(const char *key, unsigned int *value, unsigned int min, unsigned int max);
/* Register a float option. min==max disables clamping. */
void configRegisterFloat(const char *key, float *value, float min, float max);
/* Register a string option (buf must live for the program's lifetime). */
void configRegisterString(const char *key, char *value, int bufSize);

/* [Debug] knobs, env-var-or-ini. GE_PCDUMP / GE_INPUTLOG override the ini. */
const char *configGetFrameDump(void);   /* "lo-hi[:step]" or NULL if unset */
int         configGetInputLog(void);

/* ------------------------------------------------------------------------
 * Option enumeration (for the F10 port-layer options overlay).
 *
 * configForEachOption() walks every registered option; configSetOptionMeta()
 * attaches display metadata (label / adjust step / enum-value names) keyed by
 * the dotted key. Meta is an optional side table -- config.c stays ignorant of
 * which specific keys exist; the overlay populates it at init time.
 * ---------------------------------------------------------------------- */
enum {
    CONFIG_OPT_INT = 0,
    CONFIG_OPT_UINT,
    CONFIG_OPT_FLOAT,
    CONFIG_OPT_STR,
};

/* key      : dotted option key
 * type     : CONFIG_OPT_*
 * ptr      : &int / &unsigned / &float / char* (the live variable)
 * min,max  : registered clamp bounds (min==max -> unclamped)
 * step     : adjust step from the meta side table (0 if none)
 * label    : display label from the meta side table (NULL if none)
 * enumNames: NULL-terminated value-name list from the meta side table (or NULL)
 * ctx      : opaque pointer passed straight through
 */
typedef void (*ConfigOptionCb)(const char *key, int type, void *ptr,
                               double min, double max, double step,
                               const char *label, const char *const *enumNames,
                               void *ctx);

void configForEachOption(ConfigOptionCb cb, void *ctx);
void configSetOptionMeta(const char *key, const char *label, double step,
                         const char *const *enumNames);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CONFIG_H */
