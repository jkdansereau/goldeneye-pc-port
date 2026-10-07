/*
 * net_http.c -- HTTPS + WebSocket client (D410). See net_http.h.
 *
 * Windows uses WinHTTP. Its WebSocket entry points (Windows 8+) are looked up
 * at run time from winhttp.dll so the code builds with any SDK / MinGW
 * header vintage and degrades to HTTPS polling on a system without them.
 * WinHTTP receives block, so each connection has a small receive thread
 * feeding a queue; sends may run concurrently with that receive (WinHTTP
 * allows one of each in flight).
 *
 * Linux uses libcurl when CMake found it (GE007_HAVE_CURL). Everything there
 * runs on the caller's thread: libcurl handles are single-threaded and the
 * directory client calls send and receive from one thread.
 */
#include "net_http.h"
#include "net_plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* URL parsing (shared)                                                      */
/* ------------------------------------------------------------------------ */

int netUrlParse(const char *url, char *scheme, int schemeCap, char *host, int hostCap, int *port,
                char *path, int pathCap)
{
    const char *p, *hs, *he, *ps;
    int n, defPort;
    if (!url) return -1;
    p = strstr(url, "://");
    if (!p) return -1;
    n = (int)(p - url);
    if (n <= 0 || n >= schemeCap) return -1;
    memcpy(scheme, url, (size_t)n);
    scheme[n] = 0;
    {
        int i;
        for (i = 0; i < n; i++) {
            if (scheme[i] >= 'A' && scheme[i] <= 'Z') scheme[i] = (char)(scheme[i] - 'A' + 'a');
        }
    }
    if (!strcmp(scheme, "https") || !strcmp(scheme, "wss")) {
        defPort = 443;
    } else if (!strcmp(scheme, "http") || !strcmp(scheme, "ws")) {
        defPort = 80;
    } else {
        return -1;
    }
    hs = p + 3;
    ps = strchr(hs, '/');
    if (!ps) ps = hs + strlen(hs);
    he = ps;
    *port = defPort;
    {
        const char *colon = NULL, *q;
        for (q = hs; q < ps; q++) {
            if (*q == ':') colon = q;
            if (*q == '@') return -1;   /* no credentials in service URLs */
        }
        if (colon) {
            long v = strtol(colon + 1, NULL, 10);
            if (v <= 0 || v > 65535) return -1;
            *port = (int)v;
            he = colon;
        }
    }
    n = (int)(he - hs);
    if (n <= 0 || n >= hostCap) return -1;
    memcpy(host, hs, (size_t)n);
    host[n] = 0;
    if (*ps) {
        netStrCopy(path, pathCap, ps);
    } else {
        netStrCopy(path, pathCap, "/");
    }
    return 0;
}

#if defined(_WIN32)
/* ======================================================================== */
/* Windows: WinHTTP                                                          */
/* ======================================================================== */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#define GE_WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114
#define GE_WS_BINARY_MESSAGE   0
#define GE_WS_BINARY_FRAGMENT  1
#define GE_WS_UTF8_MESSAGE     2
#define GE_WS_UTF8_FRAGMENT    3
#define GE_WS_CLOSE            4
#define GE_ACCESS_AUTOMATIC_PROXY 4   /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (8.1+) */

typedef HINTERNET (WINAPI *PFN_WsCompleteUpgrade)(HINTERNET, DWORD_PTR);
typedef DWORD (WINAPI *PFN_WsSend)(HINTERNET, int, PVOID, DWORD);
typedef DWORD (WINAPI *PFN_WsReceive)(HINTERNET, PVOID, DWORD, DWORD *, int *);
typedef DWORD (WINAPI *PFN_WsShutdown)(HINTERNET, USHORT, PVOID, DWORD);

static struct {
    int tried, ok;
    PFN_WsCompleteUpgrade completeUpgrade;
    PFN_WsSend send;
    PFN_WsReceive receive;
    PFN_WsShutdown shutdown;
} W;

static int wsLoad(void)
{
    if (!W.tried) {
        HMODULE m = GetModuleHandleW(L"winhttp.dll");
        W.tried = 1;
        if (!m) m = LoadLibraryW(L"winhttp.dll");
        if (m) {
            W.completeUpgrade = (PFN_WsCompleteUpgrade)(void *)GetProcAddress(m, "WinHttpWebSocketCompleteUpgrade");
            W.send = (PFN_WsSend)(void *)GetProcAddress(m, "WinHttpWebSocketSend");
            W.receive = (PFN_WsReceive)(void *)GetProcAddress(m, "WinHttpWebSocketReceive");
            W.shutdown = (PFN_WsShutdown)(void *)GetProcAddress(m, "WinHttpWebSocketShutdown");
        }
        W.ok = W.completeUpgrade && W.send && W.receive;
    }
    return W.ok;
}

int netHttpAvailable(void) { return 1; }
int netWsAvailable(void) { return wsLoad(); }

static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w;
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)n);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void winErr(char *err, int errLen, const char *what)
{
    DWORD e = GetLastError();
    if (e == 12007 /* ERROR_WINHTTP_NAME_NOT_RESOLVED */) {
        netStrFmt(err, errLen, "%s: cannot find the online service (no internet?)", what);
    } else if (e == 12029 /* CANNOT_CONNECT */ || e == 12002 /* TIMEOUT */) {
        netStrFmt(err, errLen, "%s: the online service did not answer", what);
    } else if (e >= 12037 && e <= 12045) {   /* certificate errors */
        netStrFmt(err, errLen, "%s: secure connection failed (certificate)", what);
    } else {
        netStrFmt(err, errLen, "%s failed (error %lu)", what, (unsigned long)e);
    }
}

typedef struct WinReq {
    HINTERNET session, connect, request;
} WinReq;

static void winReqClose(WinReq *r)
{
    if (r->request) WinHttpCloseHandle(r->request);
    if (r->connect) WinHttpCloseHandle(r->connect);
    if (r->session) WinHttpCloseHandle(r->session);
    memset(r, 0, sizeof(*r));
}

/* Open session + connection + request for `method` on `url`. */
static int winReqOpen(WinReq *r, const char *url, const wchar_t *method, int timeoutMs, int rxTimeoutMs,
                      char *err, int errLen)
{
    char scheme[8], host[256], path[512];
    int port, secure;
    wchar_t *whost, *wpath;
    memset(r, 0, sizeof(*r));
    if (netUrlParse(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)) != 0) {
        netStrCopy(err, errLen, "bad online service address");
        return -1;
    }
    secure = !strcmp(scheme, "https") || !strcmp(scheme, "wss");
    r->session = WinHttpOpen(L"GE007-PC-Online/1", GE_ACCESS_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (!r->session) {   /* before Windows 8.1 */
        r->session = WinHttpOpen(L"GE007-PC-Online/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!r->session) {
        winErr(err, errLen, "WinHTTP");
        return -1;
    }
    WinHttpSetTimeouts(r->session, timeoutMs, timeoutMs, timeoutMs, rxTimeoutMs);
    whost = widen(host);
    wpath = widen(path);
    if (!whost || !wpath) {
        free(whost);
        free(wpath);
        winReqClose(r);
        netStrCopy(err, errLen, "out of memory");
        return -1;
    }
    r->connect = WinHttpConnect(r->session, whost, (INTERNET_PORT)port, 0);
    if (r->connect) {
        r->request = WinHttpOpenRequest(r->connect, method, wpath, NULL, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    }
    free(whost);
    free(wpath);
    if (!r->request) {
        winErr(err, errLen, "connect");
        winReqClose(r);
        return -1;
    }
    return 0;
}

static DWORD winStatus(HINTERNET req)
{
    DWORD status = 0, sz = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &sz, WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    return status;
}

int netHttpPost(const char *url, const void *body, int len, void *resp, int respCap, int *respLen,
                int timeoutMs, char *err, int errLen)
{
    WinReq r;
    DWORD status;
    int got = 0;
    static const wchar_t hdr[] = L"Content-Type: application/octet-stream\r\n";
    if (respLen) *respLen = 0;
    if (winReqOpen(&r, url, L"POST", timeoutMs, timeoutMs, err, errLen) != 0) return -1;
    if (!WinHttpSendRequest(r.request, hdr, (DWORD)-1L, (LPVOID)body, (DWORD)len, (DWORD)len, 0) ||
        !WinHttpReceiveResponse(r.request, NULL)) {
        winErr(err, errLen, "request");
        winReqClose(&r);
        return -1;
    }
    status = winStatus(r.request);
    for (;;) {
        DWORD avail = 0, rd = 0;
        if (!WinHttpQueryDataAvailable(r.request, &avail) || avail == 0) break;
        if (got >= respCap) {   /* drain and drop the excess */
            uint8_t sink[512];
            if (!WinHttpReadData(r.request, sink, avail < sizeof(sink) ? avail : (DWORD)sizeof(sink), &rd) || rd == 0) break;
            continue;
        }
        if (avail > (DWORD)(respCap - got)) avail = (DWORD)(respCap - got);
        if (!WinHttpReadData(r.request, (uint8_t *)resp + got, avail, &rd) || rd == 0) break;
        got += (int)rd;
    }
    if (respLen) *respLen = got;
    winReqClose(&r);
    return (int)status;
}

/* ---- WebSocket ---- */

#define WS_QUEUE 64

struct NetWs {
    WinReq req;              /* session + connect stay open; request closed */
    HINTERNET ws;
    NetMutex *mx;            /* queue + dead */
    NetMutex *sendMx;
    NetThread *rx;
    volatile int dead;
    uint8_t *q[WS_QUEUE];
    int qlen[WS_QUEUE];
    int qHead, qTail;
};

static void wsPush(NetWs *w, const uint8_t *data, int len)
{
    uint8_t *copy = (uint8_t *)malloc((size_t)len);
    if (!copy) return;
    memcpy(copy, data, (size_t)len);
    netMutexLock(w->mx);
    if ((w->qTail + 1) % WS_QUEUE != w->qHead) {
        w->q[w->qTail] = copy;
        w->qlen[w->qTail] = len;
        w->qTail = (w->qTail + 1) % WS_QUEUE;
        copy = NULL;
    }
    netMutexUnlock(w->mx);
    free(copy);   /* queue full: the directory client is not reading; drop */
}

static void wsRxThread(void *arg)
{
    NetWs *w = (NetWs *)arg;
    uint8_t *msg = (uint8_t *)malloc(NET_WS_MAX_MSG);
    int have = 0;
    if (!msg) {
        w->dead = 1;
        return;
    }
    for (;;) {
        DWORD rd = 0;
        int type = 0;
        DWORD e = W.receive(w->ws, msg + have, (DWORD)(NET_WS_MAX_MSG - have), &rd, &type);
        if (e != NO_ERROR || type == GE_WS_CLOSE) break;
        have += (int)rd;
        if (type == GE_WS_BINARY_FRAGMENT || type == GE_WS_UTF8_FRAGMENT) {
            if (have >= NET_WS_MAX_MSG) break;   /* oversized: protocol violation */
            continue;
        }
        if (type == GE_WS_BINARY_MESSAGE && have > 0) wsPush(w, msg, have);
        have = 0;   /* text ("pong") just proves liveness */
    }
    free(msg);
    w->dead = 1;
}

NetWs *netWsConnect(const char *url, int timeoutMs, char *err, int errLen)
{
    NetWs *w;
    DWORD status;
    if (!wsLoad()) {
        netStrCopy(err, errLen, "WebSockets need Windows 8 or later");
        return NULL;
    }
    w = (NetWs *)calloc(1, sizeof(*w));
    if (!w) {
        netStrCopy(err, errLen, "out of memory");
        return NULL;
    }
    /* Receive timeout 60 s: the WebSocket inherits it, and the directory
     * client pings every 15 s, so only a dead connection ever hits it. */
    if (winReqOpen(&w->req, url, L"GET", timeoutMs, 60000, err, errLen) != 0) {
        free(w);
        return NULL;
    }
    if (!WinHttpSetOption(w->req.request, GE_WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) ||
        !WinHttpSendRequest(w->req.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(w->req.request, NULL)) {
        winErr(err, errLen, "connect");
        winReqClose(&w->req);
        free(w);
        return NULL;
    }
    status = winStatus(w->req.request);
    if (status != 101) {
        netStrFmt(err, errLen, "the online service refused the connection (HTTP %lu)", (unsigned long)status);
        winReqClose(&w->req);
        free(w);
        return NULL;
    }
    w->ws = W.completeUpgrade(w->req.request, 0);
    WinHttpCloseHandle(w->req.request);
    w->req.request = NULL;
    if (!w->ws) {
        winErr(err, errLen, "WebSocket upgrade");
        winReqClose(&w->req);
        free(w);
        return NULL;
    }
    /* Keepalive pings answer within seconds; a minute of silence is death. */
    {
        DWORD t = 60000;
        WinHttpSetOption(w->ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    }
    w->mx = netMutexCreate();
    w->sendMx = netMutexCreate();
    w->rx = (w->mx && w->sendMx) ? netThreadStart(wsRxThread, w) : NULL;
    if (!w->rx) {
        WinHttpCloseHandle(w->ws);
        if (w->mx) netMutexDestroy(w->mx);
        if (w->sendMx) netMutexDestroy(w->sendMx);
        winReqClose(&w->req);
        free(w);
        netStrCopy(err, errLen, "cannot start the connection thread");
        return NULL;
    }
    return w;
}

static int wsSendType(NetWs *w, int type, const void *data, int len)
{
    DWORD e;
    if (!w || w->dead) return -1;
    netMutexLock(w->sendMx);
    e = W.send(w->ws, type, (PVOID)data, (DWORD)len);
    netMutexUnlock(w->sendMx);
    if (e != NO_ERROR) {
        w->dead = 1;
        return -1;
    }
    return 0;
}

int netWsSend(NetWs *w, const void *data, int len)
{
    return wsSendType(w, GE_WS_BINARY_MESSAGE, data, len);
}

int netWsSendText(NetWs *w, const char *text)
{
    return wsSendType(w, GE_WS_UTF8_MESSAGE, text, (int)strlen(text));
}

int netWsRecv(NetWs *w, void *buf, int cap, int timeoutMs)
{
    uint64_t end = netTimeUs() + (uint64_t)(timeoutMs > 0 ? timeoutMs : 0) * 1000ull;
    if (!w) return -1;
    for (;;) {
        uint8_t *m = NULL;
        int len = 0;
        netMutexLock(w->mx);
        if (w->qHead != w->qTail) {
            m = w->q[w->qHead];
            len = w->qlen[w->qHead];
            w->qHead = (w->qHead + 1) % WS_QUEUE;
        }
        netMutexUnlock(w->mx);
        if (m) {
            if (len > cap) len = cap;
            memcpy(buf, m, (size_t)len);
            free(m);
            return len;
        }
        if (w->dead) return -1;
        if (netTimeUs() >= end) return 0;
        netSleepUs(2000);
    }
}

void netWsClose(NetWs *w)
{
    if (!w) return;
    if (!w->dead && W.shutdown) {
        netMutexLock(w->sendMx);
        W.shutdown(w->ws, 1000, NULL, 0);   /* send our close frame; don't wait */
        netMutexUnlock(w->sendMx);
    }
    WinHttpCloseHandle(w->ws);   /* cancels the pending receive */
    netThreadJoin(w->rx);
    while (w->qHead != w->qTail) {
        free(w->q[w->qHead]);
        w->qHead = (w->qHead + 1) % WS_QUEUE;
    }
    netMutexDestroy(w->mx);
    netMutexDestroy(w->sendMx);
    winReqClose(&w->req);
    free(w);
}

#elif defined(GE007_HAVE_CURL)
/* ======================================================================== */
/* Linux: libcurl                                                            */
/* ======================================================================== */

#include <curl/curl.h>
#include <poll.h>

#if defined(LIBCURL_VERSION_NUM) && LIBCURL_VERSION_NUM >= 0x075600 && defined(CURLWS_BINARY)
#define GE_CURL_WS 1
#else
#define GE_CURL_WS 0
#endif

static int s_curlInit = 0;

static void curlInitOnce(void)
{
    if (!s_curlInit) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        s_curlInit = 1;
    }
}

int netHttpAvailable(void) { return 1; }

int netWsAvailable(void)
{
#if GE_CURL_WS
    /* Built against a WebSocket-capable libcurl; make sure the runtime one
     * also speaks it (distributions may disable it before 8.11). */
    const curl_version_info_data *v;
    int i;
    curlInitOnce();
    v = curl_version_info(CURLVERSION_NOW);
    if (!v || !v->protocols) return 0;
    for (i = 0; v->protocols[i]; i++) {
        if (!strcmp(v->protocols[i], "wss")) return 1;
    }
    return 0;
#else
    return 0;
#endif
}

typedef struct CurlBuf {
    uint8_t *p;
    int len, cap;
} CurlBuf;

static size_t curlWrite(char *data, size_t size, size_t n, void *ud)
{
    CurlBuf *b = (CurlBuf *)ud;
    size_t total = size * n;
    size_t room = (size_t)(b->cap - b->len);
    size_t take = total < room ? total : room;
    if (take) {
        memcpy(b->p + b->len, data, take);
        b->len += (int)take;
    }
    return total;   /* swallow the rest rather than abort */
}

int netHttpPost(const char *url, const void *body, int len, void *resp, int respCap, int *respLen,
                int timeoutMs, char *err, int errLen)
{
    CURL *c;
    CURLcode rc;
    long status = 0;
    struct curl_slist *hdr = NULL;
    CurlBuf b;
    curlInitOnce();
    if (respLen) *respLen = 0;
    c = curl_easy_init();
    if (!c) {
        netStrCopy(err, errLen, "libcurl init failed");
        return -1;
    }
    b.p = (uint8_t *)resp;
    b.len = 0;
    b.cap = respCap;
    hdr = curl_slist_append(hdr, "Content-Type: application/octet-stream");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)len);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)timeoutMs);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "GE007-PC-Online/1");
    rc = curl_easy_perform(c);
    if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        netStrFmt(err, errLen, "online service: %s", curl_easy_strerror(rc));
        return -1;
    }
    if (respLen) *respLen = b.len;
    return (int)status;
}

#if GE_CURL_WS

struct NetWs {
    CURL *c;
    int dead;
    uint8_t msg[NET_WS_MAX_MSG];
    int have;
};

NetWs *netWsConnect(const char *url, int timeoutMs, char *err, int errLen)
{
    NetWs *w;
    CURLcode rc;
    curlInitOnce();
    if (!netWsAvailable()) {
        netStrCopy(err, errLen, "this libcurl has no WebSocket support");
        return NULL;
    }
    w = (NetWs *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->c = curl_easy_init();
    if (!w->c) {
        free(w);
        netStrCopy(err, errLen, "libcurl init failed");
        return NULL;
    }
    curl_easy_setopt(w->c, CURLOPT_URL, url);
    curl_easy_setopt(w->c, CURLOPT_CONNECT_ONLY, 2L);   /* WebSocket mode */
    curl_easy_setopt(w->c, CURLOPT_CONNECTTIMEOUT_MS, (long)timeoutMs);
    curl_easy_setopt(w->c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(w->c, CURLOPT_USERAGENT, "GE007-PC-Online/1");
    rc = curl_easy_perform(w->c);
    if (rc != CURLE_OK) {
        netStrFmt(err, errLen, "online service: %s", curl_easy_strerror(rc));
        curl_easy_cleanup(w->c);
        free(w);
        return NULL;
    }
    return w;
}

static int wsSendFlags(NetWs *w, const void *data, int len, unsigned int flags)
{
    size_t done = 0;
    uint64_t end = netTimeUs() + 5000000ull;
    if (!w || w->dead) return -1;
    while (done < (size_t)len) {
        size_t sent = 0;
        CURLcode rc = curl_ws_send(w->c, (const char *)data + done, (size_t)len - done, &sent, 0, flags);
        if (rc == CURLE_AGAIN) {
            if (netTimeUs() > end) break;
            netSleepUs(1000);
            continue;
        }
        if (rc != CURLE_OK) break;
        done += sent;
    }
    if (done < (size_t)len) {
        w->dead = 1;
        return -1;
    }
    return 0;
}

int netWsSend(NetWs *w, const void *data, int len) { return wsSendFlags(w, data, len, CURLWS_BINARY); }
int netWsSendText(NetWs *w, const char *text) { return wsSendFlags(w, text, (int)strlen(text), CURLWS_TEXT); }

int netWsRecv(NetWs *w, void *buf, int cap, int timeoutMs)
{
    uint64_t end = netTimeUs() + (uint64_t)(timeoutMs > 0 ? timeoutMs : 0) * 1000ull;
    if (!w || w->dead) return -1;
    for (;;) {
        size_t rlen = 0;
        const struct curl_ws_frame *meta = NULL;
        CURLcode rc;
        if (w->have >= NET_WS_MAX_MSG) {
            w->dead = 1;
            return -1;
        }
        rc = curl_ws_recv(w->c, w->msg + w->have, (size_t)(NET_WS_MAX_MSG - w->have), &rlen, &meta);
        if (rc == CURLE_AGAIN) {
            curl_socket_t s = CURL_SOCKET_BAD;
            int left;
            if (netTimeUs() >= end) return 0;
            curl_easy_getinfo(w->c, CURLINFO_ACTIVESOCKET, &s);
            left = (int)((end - netTimeUs()) / 1000ull);
            if (s != CURL_SOCKET_BAD) {
                struct pollfd pfd;
                pfd.fd = s;
                pfd.events = POLLIN;
                pfd.revents = 0;
                poll(&pfd, 1, left > 0 ? left : 0);
            } else {
                netSleepUs(2000);
            }
            continue;
        }
        if (rc != CURLE_OK || !meta || (meta->flags & CURLWS_CLOSE)) {
            w->dead = 1;
            return -1;
        }
        w->have += (int)rlen;
        if (meta->bytesleft > 0 || (meta->flags & CURLWS_CONT)) continue;   /* more of this message */
        if (meta->flags & CURLWS_BINARY) {
            int len = w->have < cap ? w->have : cap;
            memcpy(buf, w->msg, (size_t)len);
            w->have = 0;
            return len;
        }
        w->have = 0;   /* text / ping / pong */
    }
}

void netWsClose(NetWs *w)
{
    if (!w) return;
    if (!w->dead) {
        size_t sent = 0;
        curl_ws_send(w->c, "", 0, &sent, 0, CURLWS_CLOSE);
    }
    curl_easy_cleanup(w->c);
    free(w);
}

#else /* libcurl without WebSockets: the directory client polls instead */

NetWs *netWsConnect(const char *url, int timeoutMs, char *err, int errLen)
{
    (void)url;
    (void)timeoutMs;
    netStrCopy(err, errLen, "this libcurl has no WebSocket support");
    return NULL;
}
int netWsSend(NetWs *w, const void *d, int n) { (void)w; (void)d; (void)n; return -1; }
int netWsSendText(NetWs *w, const char *t) { (void)w; (void)t; return -1; }
int netWsRecv(NetWs *w, void *b, int c, int t) { (void)w; (void)b; (void)c; (void)t; return -1; }
void netWsClose(NetWs *w) { (void)w; }

#endif /* GE_CURL_WS */

#else
/* ======================================================================== */
/* No HTTP stack in this build: LAN / direct play only                       */
/* ======================================================================== */

int netHttpAvailable(void) { return 0; }
int netWsAvailable(void) { return 0; }

int netHttpPost(const char *url, const void *body, int len, void *resp, int respCap, int *respLen,
                int timeoutMs, char *err, int errLen)
{
    (void)url; (void)body; (void)len; (void)resp; (void)respCap; (void)timeoutMs;
    if (respLen) *respLen = 0;
    netStrCopy(err, errLen, "this build has no online service support (no libcurl)");
    return -1;
}

NetWs *netWsConnect(const char *url, int timeoutMs, char *err, int errLen)
{
    (void)url;
    (void)timeoutMs;
    netStrCopy(err, errLen, "this build has no online service support (no libcurl)");
    return NULL;
}
int netWsSend(NetWs *w, const void *d, int n) { (void)w; (void)d; (void)n; return -1; }
int netWsSendText(NetWs *w, const char *t) { (void)w; (void)t; return -1; }
int netWsRecv(NetWs *w, void *b, int c, int t) { (void)w; (void)b; (void)c; (void)t; return -1; }
void netWsClose(NetWs *w) { (void)w; }

#endif
