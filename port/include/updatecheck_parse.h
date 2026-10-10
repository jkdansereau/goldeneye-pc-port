/*
 * Parsing helpers for the opt-in update check (see port/src/updatecheck.c).
 * Header-only (static inline) so tools_pc/updatecheck_test.c can exercise
 * them offline without pulling in SDL or the rest of the port.
 */
#ifndef UPDATECHECK_PARSE_H
#define UPDATECHECK_PARSE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Parse "vMAJOR.MINOR.PATCH" (missing parts = 0, suffix ignored). */
static inline int parseVer(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    if (*s < '0' || *s > '9') return 0;
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') break;
        v[i] = (int)strtol(s, (char **)&s, 10);
        if (*s != '.') break;
        s++;
    }
    return 1;
}

static inline int versionNewer(const char *tag, const char *running)
{
    int a[3], b[3];
    if (!parseVer(tag, a) || !parseVer(running, b)) return 0;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

/* Minimal scan for "tag_name": "<value>". */
static inline int extractTag(const char *body, char *out, size_t n)
{
    const char *p = strstr(body, "\"tag_name\"");
    if (!p) return 0;
    p += 10;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    /* Tag characters only (no control/ANSI bytes reach the log). */
    while (*p && *p != '"' && i + 1 < n) {
        char c = *p++;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              c == '.' || c == '_' || c == '-')) return 0;
        out[i++] = c;
    }
    if (*p != '"' || i == 0) return 0;
    out[i] = 0;
    return 1;
}

#endif
