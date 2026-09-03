/*
 * INI configuration (ge007.ini).
 *
 * A tiny key=value store. Options register themselves (usually via a
 * PD_CONSTRUCTOR) and are loaded from / saved to "$S/ge007.ini".
 *
 * Modelled on the PD port's port/src/config.c (~300 lines) but trimmed:
 * Int + String options only (GE's config surface is small so far).
 *
 * File format: keys are dotted ("Input.MouseAimSpeed"). On disk they are
 * grouped into `[Section]` blocks by the part before the last '.', e.g.
 *
 *   [Input]
 *   MouseAimSpeed = 50
 *   MouseInvertY = 0
 *
 * A bare `Key = value` with no preceding section, or a fully dotted
 * `Section.Key = value` line, are both accepted on read.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "system.h"
#include "config.h"

/*
 * [Debug] knobs mirroring the dev env vars. The env var always wins so
 * existing scripts (tools_pc/*.sh) are unaffected; the ini is a fallback for
 * interactive runs. GE_DETERM is design-only (D117) and has no code path, so
 * it is not mirrored.
 */
static char dbgFrameDump[128] = "";   /* mirrors GE_PCDUMP ("lo-hi[:step]") */
static int  dbgInputLog       = 0;    /* mirrors GE_INPUTLOG                */

PD_CONSTRUCTOR static void configDebugInit(void)
{
    configRegisterString("Debug.FrameDump", dbgFrameDump, sizeof(dbgFrameDump));
    configRegisterInt("Debug.InputLog", &dbgInputLog, 0, 1);
}

const char *configGetFrameDump(void)
{
    const char *e = getenv("GE_PCDUMP");
    if (e && *e) return e;
    return dbgFrameDump[0] ? dbgFrameDump : NULL;
}

int configGetInputLog(void)
{
    return getenv("GE_INPUTLOG") ? 1 : dbgInputLog;
}

#define MAX_OPTIONS 256
#define INI_PATH    "$S/ge007.ini"

struct IntOption   { char key[64]; int *value; int min, max; int seen; };
struct UIntOption  { char key[64]; unsigned int *value; unsigned int min, max; int seen; };
struct FloatOption { char key[64]; float *value; float min, max; int seen; };
struct StrOption   { char key[64]; char *value; int bufSize; int seen; };

static struct IntOption   intOpts[MAX_OPTIONS];
static int                numIntOpts = 0;
static struct UIntOption  uintOpts[MAX_OPTIONS];
static int                numUIntOpts = 0;
static struct FloatOption floatOpts[MAX_OPTIONS];
static int                numFloatOpts = 0;
static struct StrOption   strOpts[MAX_OPTIONS];
static int                numStrOpts = 0;

void configRegisterInt(const char *key, int *value, int min, int max)
{
    if (numIntOpts >= MAX_OPTIONS) return;
    struct IntOption *o = &intOpts[numIntOpts++];
    strncpy(o->key, key, sizeof(o->key) - 1);
    o->value = value;
    o->min = min;
    o->max = max;
}

void configRegisterUInt(const char *key, unsigned int *value, unsigned int min, unsigned int max)
{
    if (numUIntOpts >= MAX_OPTIONS) return;
    struct UIntOption *o = &uintOpts[numUIntOpts++];
    strncpy(o->key, key, sizeof(o->key) - 1);
    o->value = value;
    o->min = min;
    o->max = max;
}

void configRegisterFloat(const char *key, float *value, float min, float max)
{
    if (numFloatOpts >= MAX_OPTIONS) return;
    struct FloatOption *o = &floatOpts[numFloatOpts++];
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

/* ------------------------------------------------------------------------
 * Option metadata side table + enumeration (F10 options overlay). config.c
 * gains no knowledge of specific keys: the overlay calls configSetOptionMeta()
 * for each row it draws, and configForEachOption() joins that against the
 * registered options.
 * ---------------------------------------------------------------------- */
struct OptionMeta {
    char key[64];
    const char *label;
    double step;
    const char *const *enumNames;
};
static struct OptionMeta metaOpts[MAX_OPTIONS];
static int               numMetaOpts = 0;

static const struct OptionMeta *findMeta(const char *key)
{
    for (int i = 0; i < numMetaOpts; i++) {
        if (strcmp(metaOpts[i].key, key) == 0) {
            return &metaOpts[i];
        }
    }
    return NULL;
}

void configSetOptionMeta(const char *key, const char *label, double step,
                         const char *const *enumNames)
{
    struct OptionMeta *m = NULL;
    for (int i = 0; i < numMetaOpts; i++) {
        if (strcmp(metaOpts[i].key, key) == 0) {
            m = &metaOpts[i];
            break;
        }
    }
    if (!m) {
        if (numMetaOpts >= MAX_OPTIONS) return;
        m = &metaOpts[numMetaOpts++];
        strncpy(m->key, key, sizeof(m->key) - 1);
        m->key[sizeof(m->key) - 1] = 0;
    }
    m->label = label;
    m->step = step;
    m->enumNames = enumNames;
}

void configForEachOption(ConfigOptionCb cb, void *ctx)
{
    if (!cb) return;
    for (int i = 0; i < numIntOpts; i++) {
        const struct OptionMeta *m = findMeta(intOpts[i].key);
        cb(intOpts[i].key, CONFIG_OPT_INT, intOpts[i].value,
           (double)intOpts[i].min, (double)intOpts[i].max,
           m ? m->step : 0.0, m ? m->label : NULL, m ? m->enumNames : NULL, ctx);
    }
    for (int i = 0; i < numUIntOpts; i++) {
        const struct OptionMeta *m = findMeta(uintOpts[i].key);
        cb(uintOpts[i].key, CONFIG_OPT_UINT, uintOpts[i].value,
           (double)uintOpts[i].min, (double)uintOpts[i].max,
           m ? m->step : 0.0, m ? m->label : NULL, m ? m->enumNames : NULL, ctx);
    }
    for (int i = 0; i < numFloatOpts; i++) {
        const struct OptionMeta *m = findMeta(floatOpts[i].key);
        cb(floatOpts[i].key, CONFIG_OPT_FLOAT, floatOpts[i].value,
           (double)floatOpts[i].min, (double)floatOpts[i].max,
           m ? m->step : 0.0, m ? m->label : NULL, m ? m->enumNames : NULL, ctx);
    }
    for (int i = 0; i < numStrOpts; i++) {
        const struct OptionMeta *m = findMeta(strOpts[i].key);
        cb(strOpts[i].key, CONFIG_OPT_STR, strOpts[i].value,
           0.0, 0.0, m ? m->step : 0.0, m ? m->label : NULL,
           m ? m->enumNames : NULL, ctx);
    }
}

static int parseInt(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sign;
}

static float parseFloat(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return (float)atof(s);
}

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int isws(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (lc((unsigned char)*a) != lc((unsigned char)*b))
            return 0;
    }
    return *a == *b;
}

static char *trim(char *s)
{
    while (*s && isws((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isws((unsigned char)end[-1])) *--end = 0;
    return s;
}

static void applyKV(const char *dottedKey, const char *val)
{
    for (int i = 0; i < numIntOpts; i++) {
        if (ci_eq(intOpts[i].key, dottedKey)) {
            intOpts[i].seen = 1;
            int v = parseInt(val);
            if (intOpts[i].min != intOpts[i].max) {
                if (v < intOpts[i].min) v = intOpts[i].min;
                if (v > intOpts[i].max) v = intOpts[i].max;
            }
            *intOpts[i].value = v;
            return;
        }
    }
    for (int i = 0; i < numUIntOpts; i++) {
        if (ci_eq(uintOpts[i].key, dottedKey)) {
            uintOpts[i].seen = 1;
            long p = parseInt(val);
            unsigned int v = (p < 0) ? 0u : (unsigned int)p;
            if (uintOpts[i].min != uintOpts[i].max) {
                if (v < uintOpts[i].min) v = uintOpts[i].min;
                if (v > uintOpts[i].max) v = uintOpts[i].max;
            }
            *uintOpts[i].value = v;
            return;
        }
    }
    for (int i = 0; i < numFloatOpts; i++) {
        if (ci_eq(floatOpts[i].key, dottedKey)) {
            floatOpts[i].seen = 1;
            float v = parseFloat(val);
            if (floatOpts[i].min != floatOpts[i].max) {
                if (v < floatOpts[i].min) v = floatOpts[i].min;
                if (v > floatOpts[i].max) v = floatOpts[i].max;
            }
            *floatOpts[i].value = v;
            return;
        }
    }
    for (int i = 0; i < numStrOpts; i++) {
        if (ci_eq(strOpts[i].key, dottedKey)) {
            strOpts[i].seen = 1;
            strncpy(strOpts[i].value, val, strOpts[i].bufSize - 1);
            strOpts[i].value[strOpts[i].bufSize - 1] = 0;
            return;
        }
    }
    sysLogPrintf(LOG_NOTE, "config: unknown key '%s' (ignored)", dottedKey);
}

void configLoad(void)
{
    const char *path = sysResolvePath(INI_PATH);
    FILE *f = fopen(path, "r");
    if (!f) {
        sysLogPrintf(LOG_INFO, "config: no %s yet; writing defaults", path);
        configSave();
        return;
    }

    char line[512];
    char section[64] = "";
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#' || *p == ';')
            continue;
        if (*p == '[') {
            char *close = strchr(p, ']');
            if (close) {
                *close = 0;
                strncpy(section, trim(p + 1), sizeof(section) - 1);
                section[sizeof(section) - 1] = 0;
            }
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = trim(p);
        char *val = trim(eq + 1);

        char dotted[128];
        if (strchr(key, '.') || !section[0])
            snprintf(dotted, sizeof(dotted), "%s", key);
        else
            snprintf(dotted, sizeof(dotted), "%s.%s", section, key);
        applyKV(dotted, val);
    }
    fclose(f);
    sysLogPrintf(LOG_INFO, "config: loaded %s", path);

    /* Migration: if the running build registers options the on-disk file
     * never mentioned (a new [Video] knob, say), rewrite the file once so the
     * defaults are visible and editable. Only triggers when something is
     * genuinely missing -- an up-to-date file is left untouched (comments and
     * all). */
    int missing = 0;
    for (int i = 0; i < numIntOpts && !missing; i++) missing = !intOpts[i].seen;
    for (int i = 0; i < numUIntOpts && !missing; i++) missing = !uintOpts[i].seen;
    for (int i = 0; i < numFloatOpts && !missing; i++) missing = !floatOpts[i].seen;
    for (int i = 0; i < numStrOpts && !missing; i++) missing = !strOpts[i].seen;
    if (missing) {
        sysLogPrintf(LOG_INFO, "config: adding newly-registered keys to %s", path);
        configSave();
    }
}

/* Section name = everything before the last '.'; "" if the key isn't dotted. */
static void splitKey(const char *dotted, char *sec, int secSize, const char **leaf)
{
    const char *dot = strrchr(dotted, '.');
    if (dot) {
        int n = (int)(dot - dotted);
        if (n > secSize - 1) n = secSize - 1;
        memcpy(sec, dotted, n);
        sec[n] = 0;
        *leaf = dot + 1;
    } else {
        sec[0] = 0;
        *leaf = dotted;
    }
}

void configSave(void)
{
    const char *path = sysResolvePath(INI_PATH);
    FILE *f = fopen(path, "w");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "config: cannot write %s", path);
        return;
    }

    fputs("# GoldenEye 007 PC port config. Edit while the game is closed;\n"
          "# rewritten (comments dropped) on a clean exit.\n", f);

    /* Flatten every registered option into one list (type-tagged), then emit
     * section-grouped in registration order so mixed int/float/string keys in
     * the same [Section] stay together. O(n^2) over a handful of options. */
    enum { T_INT, T_UINT, T_FLOAT, T_STR };
    struct { const char *key; int type, idx; } all[MAX_OPTIONS * 4];
    int n = 0;
    for (int i = 0; i < numIntOpts; i++)   all[n].key = intOpts[i].key,   all[n].type = T_INT,   all[n++].idx = i;
    for (int i = 0; i < numUIntOpts; i++)  all[n].key = uintOpts[i].key,  all[n].type = T_UINT,  all[n++].idx = i;
    for (int i = 0; i < numFloatOpts; i++) all[n].key = floatOpts[i].key, all[n].type = T_FLOAT, all[n++].idx = i;
    for (int i = 0; i < numStrOpts; i++)   all[n].key = strOpts[i].key,   all[n].type = T_STR,   all[n++].idx = i;

    char done[MAX_OPTIONS * 4] = {0};
    for (int i = 0; i < n; i++) {
        if (done[i]) continue;
        char sec[64];
        const char *leaf;
        splitKey(all[i].key, sec, sizeof(sec), &leaf);
        (void)leaf;
        fprintf(f, "\n[%s]\n", sec[0] ? sec : "General");
        for (int j = i; j < n; j++) {
            char sj[64];
            const char *lj;
            splitKey(all[j].key, sj, sizeof(sj), &lj);
            if (!ci_eq(sj, sec)) continue;
            done[j] = 1;
            switch (all[j].type) {
            case T_INT:   fprintf(f, "%s = %d\n",  lj, *intOpts[all[j].idx].value);   break;
            case T_UINT:  fprintf(f, "%s = %u\n",  lj, *uintOpts[all[j].idx].value);  break;
            case T_FLOAT: fprintf(f, "%s = %g\n",  lj, (double)*floatOpts[all[j].idx].value); break;
            case T_STR:   fprintf(f, "%s = %s\n",  lj, strOpts[all[j].idx].value);    break;
            }
        }
    }

    fclose(f);
    sysLogPrintf(LOG_INFO, "config: wrote %s", path);
}
