/*
 * net_http.h -- HTTPS + WebSocket client for the online directory service
 * (D414). Small, blocking, worker-thread API over the platform's own TLS
 * stack, so the port ships no TLS library of its own:
 *
 *   Windows: WinHTTP (system DLL; WebSockets need Windows 8+).
 *   Linux:   libcurl (system libcurl.so.4), when the build found it
 *            (GE007_HAVE_CURL); WebSockets need libcurl >= 7.86 built with
 *            WebSocket support -- otherwise netWsAvailable() is 0 and the
 *            directory client falls back to HTTPS polling.
 *   Other:   nothing (netHttpAvailable() == 0): LAN / direct play only.
 *
 * URLs: http:// https:// ws:// wss:// (plain http/ws only for local tests).
 */
#ifndef GE_NET_HTTP_H
#define GE_NET_HTTP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WS_MAX_MSG 16384

int netHttpAvailable(void);
int netWsAvailable(void);

/* One POST with a binary body. Returns the HTTP status (200...), or -1 with
 * a reason in err. *respLen = bytes stored (the body is cut at respCap). */
int netHttpPost(const char *url, const void *body, int len, void *resp, int respCap, int *respLen,
                int timeoutMs, char *err, int errLen);

typedef struct NetWs NetWs;

/* Connect + upgrade (blocking, up to timeoutMs). NULL with a reason on
 * failure. */
NetWs *netWsConnect(const char *url, int timeoutMs, char *err, int errLen);
/* One binary message. 0 = sent, -1 = the connection is gone. */
int netWsSend(NetWs *ws, const void *data, int len);
/* One text message (keepalive "ping"). */
int netWsSendText(NetWs *ws, const char *text);
/* Next binary message: > 0 its length, 0 = none within timeoutMs, -1 =
 * closed / error. Text messages ("pong") are consumed silently. */
int netWsRecv(NetWs *ws, void *buf, int cap, int timeoutMs);
void netWsClose(NetWs *ws);

/* Split "scheme://host[:port]/path". 0 on success. */
int netUrlParse(const char *url, char *scheme, int schemeCap, char *host, int hostCap, int *port,
                char *path, int pathCap);

#ifdef __cplusplus
}
#endif

#endif /* GE_NET_HTTP_H */
