/*
 * INI configuration (ge007.ini).
 *
 * A tiny key=value store. Options register themselves (usually via a
 * PD_CONSTRUCTOR) and are loaded from / saved to the ini in the data dir.
 *
 * Modelled on the PD port's port/src/config.c (~300 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "system.h"
#include "config.h"

#define MAX_OPTIONS 256

struct IntOption   { char key[64]; int *value; int min, max; };
struct StrOption   { char key[64]; char *value; int bufSize; };

static struct IntOption intOpts[MAX_OPTIONS];
static int              numIntOpts = 0;
static struct StrOption strOpts[MAX_OPTIONS];
static int              numStrOpts = 0;

void configRegisterInt(const char *key, int *value, int min, int max)
{
    if (numIntOpts >= MAX_OPTIONS) return;
    struct IntOption *o = &intOpts[numIntOpts++];
    strncpy(o->key, key, sizeof(o->key) - 1);
    o->value = value;
    o->min = min;
    o->max = max;
}

void configRegisterString(const char *key, char *value, int bufSize)
{
    if (numStrOpts >= MAX_OPTIONS) return;
    struct StrOption *o = &strOpts[numStrOpts++];
    strncpy(o->key, key, sizeof(o->key) - 1);
    o->value = value;
    o->bufSize = bufSize;
}

void configLoad(void)
{
    /* TODO(Phase 1): parse "$S/ge007.ini", apply to registered options. */
    sysLogPrintf(LOG_INFO, "configLoad: TODO (Phase 1)");
}

void configSave(void)
{
    /* TODO(Phase 1): write all registered options to "$S/ge007.ini". */
}
