#ifndef PORT_COOP_H
#define PORT_COOP_H

/*
 * Opt-in 2-player split-screen co-op campaign (Switch only; docs/dev/findings.md D960).
 * Logic lives in port/src/coop.c. src/game reaches it through the GE_* hook
 * macros in src/game/player.h (one-line expression substitutions).
 * On every non-Switch build coopActive() is constant 0.
 */

/* 1 while the running stage is a co-op campaign stage (two players, mission rules). */
int coopActive(void);

/* 1 in a solo stage and in a co-op stage: replaces `getPlayerCount() == 1` at rule sites. */
int coopIsMission(void);

#endif
