#ifndef PORT_NETUI_H
#define PORT_NETUI_H

/*
 * F9 online-play overlay (D409, D410). Port-layer only, same contract as the F10
 * options overlay (optionsoverlay.h): it draws its own 2D display list after
 * the game's (fast3d gfx_run) and appends NOTHING while closed and idle, so
 * golden frame dumps stay byte-identical.
 *
 * Screens: Online (quick match / browse / host / join by code through the
 * online service, D410; LAN games; host on this PC; join by address; your
 * own ge007-netserver; settings), the lobby (players, your character /
 * handicap / control / team / ready, the leader's match settings, chat,
 * start / leave), the in-match menu (resume / leave / end for everyone) and
 * the waiting-for-players screen.
 *
 * Threads: key, text and mouse events arrive on the host (window) thread
 * and are queued; all UI logic and drawing runs in netuiEmit() on the
 * scheduler thread. The window title (host thread) carries the netplay
 * status, so it stays current even while the game waits for a peer.
 *
 * Hooks:
 *   video.c    videoPumpEvents : netuiHostKeyDown / netuiHostText /
 *                                netuiHostMouseDown / netuiHostWheel,
 *                                netuiHostPump, netuiTitleStatus
 *   input.c    inputComputePad : controller 0 swallowed while open
 *   gfx_pc.cpp gfx_run         : netuiEmit() after optionsOverlayEmit()
 *   netgame.c  netgameInit     : netuiOpenFromCli() for --net-* flags
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 while the overlay is on screen (any thread). */
int netuiIsOpen(void);

/* Host thread, SDL_KEYDOWN. F9 toggles (not while the F10 overlay or the
 * front-end PC options screen owns input); Shift+F9 leaves a running match
 * at once (works even while the game is stalled waiting for a peer). While
 * open, every key except Alt+F4 / F12 is consumed. 1 = consumed. */
int netuiHostKeyDown(const SDL_KeyboardEvent *ev);
/* Host thread, SDL_TEXTINPUT (UTF-8; only printable ASCII is kept). */
void netuiHostText(const char *utf8);
/* Host thread, mouse. 1 = consumed (the overlay is open). */
int netuiHostMouseDown(const SDL_MouseButtonEvent *ev);
int netuiHostWheel(int dir);
/* Host thread, once per pump: applies text-input start/stop requests. */
void netuiHostPump(void);
/* Host thread: one-line netplay status for the window title, or 0. */
int netuiTitleStatus(char *out, int n);

/* --net-host / --net-join / --net-quick: open the overlay at startup
 * (flags only; safe before video init). */
void netuiOpenFromCli(void);

/* Any thread: open the overlay on its home page (the front end's "Online"
 * menu entries). 0 if another options UI owns the screen. */
int netuiRequestOpen(void);

/* Scheduler thread (libultra.c): the empty game display list of a waiting
 * frame (D410); netuiEmit draws the waiting screen over it. */
Gfx *netuiWaitFrameDl(void);

/* Scheduler thread (fast3d gfx_run). NULL = nothing to draw. */
Gfx *netuiEmit(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_NETUI_H */
