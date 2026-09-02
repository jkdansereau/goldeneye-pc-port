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

#ifdef __cplusplus
}
#endif

#endif /* PORT_CONFIG_H */
