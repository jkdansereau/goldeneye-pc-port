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

#define MAX_OPTIONS 256
#define INI_PATH    "$S/ge007.ini"

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
            int v = parseInt(val);
            if (intOpts[i].min != intOpts[i].max) {
                if (v < intOpts[i].min) v = intOpts[i].min;
                if (v > intOpts[i].max) v = intOpts[i].max;
            }
            *intOpts[i].value = v;
            return;
        }
    }
    for (int i = 0; i < numStrOpts; i++) {
        if (ci_eq(strOpts[i].key, dottedKey)) {
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

    /* Emit grouped by section, preserving registration order within each.
     * O(n^2) over a handful of options -- fine. */
    char done[MAX_OPTIONS] = {0};
    for (int i = 0; i < numIntOpts; i++) {
        if (done[i]) continue;
        char sec[64];
        const char *leaf;
        splitKey(intOpts[i].key, sec, sizeof(sec), &leaf);
        (void)leaf;
        fprintf(f, "\n[%s]\n", sec[0] ? sec : "General");
        for (int j = i; j < numIntOpts; j++) {
            char sj[64];
            const char *lj;
            splitKey(intOpts[j].key, sj, sizeof(sj), &lj);
            if (ci_eq(sj, sec)) {
                fprintf(f, "%s = %d\n", lj, *intOpts[j].value);
                done[j] = 1;
            }
        }
    }

    char sdone[MAX_OPTIONS] = {0};
    for (int i = 0; i < numStrOpts; i++) {
        if (sdone[i]) continue;
        char sec[64];
        const char *leaf;
        splitKey(strOpts[i].key, sec, sizeof(sec), &leaf);
        (void)leaf;
        fprintf(f, "\n[%s]\n", sec[0] ? sec : "General");
        for (int j = i; j < numStrOpts; j++) {
            char sj[64];
            const char *lj;
            splitKey(strOpts[j].key, sj, sizeof(sj), &lj);
            if (ci_eq(sj, sec)) {
                fprintf(f, "%s = %s\n", lj, strOpts[j].value);
                sdone[j] = 1;
            }
        }
    }

    fclose(f);
    sysLogPrintf(LOG_INFO, "config: wrote %s", path);
}
