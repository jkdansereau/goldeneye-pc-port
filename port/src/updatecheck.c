/*
 * Opt-in update check. See port/include/updatecheck.h.
 *
 * Off by default (Game.CheckUpdates = 0): the ONLY entry point that can
 * reach the network, updateCheckStart(), returns before creating a thread
 * when the setting is 0. When on: one HTTPS GET per launch, on a detached
 * thread, ~5 s timeout, silent on any failure (one log line). Windows uses
 * WinHTTP, LoadLibrary'd at runtime (never linked); elsewhere `curl` is run
 * through popen() (skipped if missing).
 * No telemetry: the request carries only a fixed User-Agent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <SDL.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "versioninfo.h"
#include "updatecheck.h"

#define UC_HOST      "api.github.com"
/* /releases/latest deliberately excludes drafts and pre-releases (maintainer
 * decision): a pre-release never prompts anyone to update. */
#define UC_PATH      "/repos/jkdansereau/goldeneye-pc-port/releases/latest"
#define UC_URL       "https://" UC_HOST UC_PATH
#define UC_RELEASES  "https://github.com/jkdansereau/goldeneye-pc-port/releases"
#define UC_UA        "ge007-pc-port"
#define UC_BODY_MAX  (64 * 1024)

static int cfgCheckUpdates = 0;        /* Game.CheckUpdates, default OFF */
static SDL_atomic_t s_ready;           /* 1 once s_tag is published */
static char s_tag[32];

PD_CONSTRUCTOR static void updateCheckConfigInit(void)
{
    configRegisterInt("Game.CheckUpdates", &cfgCheckUpdates, 0, 1);
}

/* Parse "vMAJOR.MINOR.PATCH" (missing parts = 0, suffix ignored). */
static int parseVer(const char *s, int v[3])
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

static int versionNewer(const char *tag, const char *running)
{
    int a[3], b[3];
    if (!parseVer(tag, a) || !parseVer(running, b)) return 0;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

/* Minimal scan for "tag_name": "<value>". */
static int extractTag(const char *body, char *out, size_t n)
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

#ifdef _WIN32
/* winhttp.dll is loaded at runtime, only here (after the Game.CheckUpdates
 * gate), so the exe's import table never lists it and nothing networking-
 * related is mapped into the process while the setting is off. */
typedef LPVOID HINT_;
typedef HINT_ (WINAPI *PFN_Open)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef int   (WINAPI *PFN_SetTimeouts)(HINT_, int, int, int, int);
typedef HINT_ (WINAPI *PFN_Connect)(HINT_, LPCWSTR, WORD, DWORD);
typedef HINT_ (WINAPI *PFN_OpenRequest)(HINT_, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef int   (WINAPI *PFN_Send)(HINT_, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef int   (WINAPI *PFN_Recv)(HINT_, LPVOID);
typedef int   (WINAPI *PFN_Query)(HINT_, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef int   (WINAPI *PFN_Read)(HINT_, LPVOID, DWORD, LPDWORD);
typedef int   (WINAPI *PFN_Close)(HINT_);
#define UC_WH_ACCESS_DEFAULT_PROXY 0
#define UC_WH_FLAG_SECURE          0x00800000
#define UC_WH_QUERY_STATUS_CODE    19
#define UC_WH_QUERY_FLAG_NUMBER    0x20000000
#endif

/* Fetch the response body into buf (NUL-terminated). Returns 1 on success. */
static int fetchLatest(char *buf, size_t cap)
{
    size_t len = 0;
#ifdef _WIN32
    int ok = 0;
    /* System32 only: never a winhttp.dll planted next to the exe (D551 review). */
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
    HMODULE dll = LoadLibraryExW(L"winhttp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) return 0;
    PFN_Open        pOpen    = (PFN_Open)(void *)GetProcAddress(dll, "WinHttpOpen");
    PFN_SetTimeouts pTimeout = (PFN_SetTimeouts)(void *)GetProcAddress(dll, "WinHttpSetTimeouts");
    PFN_Connect     pConnect = (PFN_Connect)(void *)GetProcAddress(dll, "WinHttpConnect");
    PFN_OpenRequest pOpenReq = (PFN_OpenRequest)(void *)GetProcAddress(dll, "WinHttpOpenRequest");
    PFN_Send        pSend    = (PFN_Send)(void *)GetProcAddress(dll, "WinHttpSendRequest");
    PFN_Recv        pRecv    = (PFN_Recv)(void *)GetProcAddress(dll, "WinHttpReceiveResponse");
    PFN_Query       pQuery   = (PFN_Query)(void *)GetProcAddress(dll, "WinHttpQueryHeaders");
    PFN_Read        pRead    = (PFN_Read)(void *)GetProcAddress(dll, "WinHttpReadData");
    PFN_Close       pClose   = (PFN_Close)(void *)GetProcAddress(dll, "WinHttpCloseHandle");
    if (!pOpen || !pTimeout || !pConnect || !pOpenReq || !pSend || !pRecv ||
        !pQuery || !pRead || !pClose) {
        FreeLibrary(dll);
        return 0;
    }
    HINT_ ses = pOpen(L"" UC_UA, UC_WH_ACCESS_DEFAULT_PROXY, NULL, NULL, 0);
    if (!ses) { FreeLibrary(dll); return 0; }
    /* resolve, connect, send, receive: 5 s each */
    pTimeout(ses, 5000, 5000, 5000, 5000);
    HINT_ con = pConnect(ses, L"" UC_HOST, 443, 0);
    HINT_ req = con ? pOpenReq(con, L"GET", L"" UC_PATH, NULL, NULL, NULL,
                               UC_WH_FLAG_SECURE) : NULL;
    if (req &&
        pSend(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, NULL, 0, 0, 0) &&
        pRecv(req, NULL)) {
        DWORD status = 0, sz = sizeof(status);
        if (pQuery(req, UC_WH_QUERY_STATUS_CODE | UC_WH_QUERY_FLAG_NUMBER, NULL,
                   &status, &sz, NULL) && status == 200) {
            for (;;) {
                DWORD got = 0;
                if (len + 1 >= cap) break;
                if (!pRead(req, buf + len, (DWORD)(cap - 1 - len), &got) || got == 0)
                    break;
                len += got;
            }
            ok = len > 0;
        }
    }
    if (req) pClose(req);
    if (con) pClose(con);
    pClose(ses);
    FreeLibrary(dll);
    buf[len] = 0;
    return ok;
#else
    FILE *f = popen("curl -q -fsS --proto =https --max-time 5 -A '" UC_UA "' '" UC_URL "' 2>/dev/null", "r");
    if (!f) return 0;
    size_t got;
    while (len + 1 < cap && (got = fread(buf + len, 1, cap - 1 - len, f)) > 0) len += got;
    int rc = pclose(f);   /* nonzero if curl is missing (127) or failed */
    buf[len] = 0;
    return rc == 0 && len > 0;
#endif
}

static int updateCheckThread(void *unused)
{
    (void)unused;
    static char body[UC_BODY_MAX];   /* the thread runs at most once per launch */
    char tag[32];
    if (fetchLatest(body, UC_BODY_MAX) && extractTag(body, tag, sizeof(tag))) {
        if (versionNewer(tag, GE007_VERSION)) {
            snprintf(s_tag, sizeof(s_tag), "%s", tag);
            SDL_AtomicSet(&s_ready, 1);   /* full barrier: s_tag visible first */
            sysLogPrintf(LOG_INFO, "update check: %s is available (running v%s); %s",
                         tag, GE007_VERSION, UC_RELEASES);
        } else {
            sysLogPrintf(LOG_INFO, "update check: up to date (latest %s, running v%s)",
                         tag, GE007_VERSION);
        }
    } else {
        sysLogPrintf(LOG_INFO, "update check: no result (offline or unavailable)");
    }
    return 0;
}

void updateCheckStart(void)
{
    if (!cfgCheckUpdates) return;   /* OFF by default: no thread, no network, no init */
    SDL_Thread *t = SDL_CreateThread(updateCheckThread, "ge007-updatecheck", NULL);
    if (t) SDL_DetachThread(t);
    else sysLogPrintf(LOG_INFO, "update check: could not start thread");
}

const char *updateCheckAvailable(void)
{
    return SDL_AtomicGet(&s_ready) ? s_tag : NULL;
}

void updateCheckOpenReleases(void)
{
    if (SDL_OpenURL(UC_RELEASES) != 0)
        sysLogPrintf(LOG_INFO, "update check: could not open browser: %s", SDL_GetError());
}
