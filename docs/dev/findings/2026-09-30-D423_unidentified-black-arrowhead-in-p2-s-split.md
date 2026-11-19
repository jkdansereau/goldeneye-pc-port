# D423

<!-- from the old section-F/H table row -->
| D423 | **Unidentified black arrowhead in P2's split-screen view (stage 38)** — full `## D423` entry at file tail | CLOSED 2026-10-01: faithful (level shadow at a doorway top), maintainer-confirmed live. |

## D423 — Unidentified black arrowhead in P2's split-screen view

**CLOSED 2026-10-01: faithful, not a bug.** The maintainer played a live 4P Temple match (mouse/keyboard P1 + Xbox pad P2, split-screen build) and confirmed the black triangle is the level's own shadow/shading at the top of a doorway. The session exited cleanly (window closed, rc=0, no crash log).

**2026-10-01:** seen again headlessly in ONE frame of a 4P run on stage 38 (`GE_STARTMP=4 GE_MPVIRT=3`, scripted seats): P4's quadrant at frame 920, a flat, untextured near-black (RGB ~10-13) right-pointing triangle with sharp edges at a wall/ceiling edge; it looks in-world, not HUD. (An earlier note here also claimed P2 at frame 380; that was a thumbnail misread, and the full-res crop shows nothing.) So it is real rendered pixels and not 2P-only, but it has not been shown to be a port bug: it could be legitimate geometry, e.g. part of another player's model or a level object, until checked against 1964 or traced.

Seen in a `GE_PCDUMP` frame (2P, stage 38, frame 500) as a black right-pointing triangle at the left of the lower (P2) view, and again in live play (user: "not sure what it is"). Not identified: could be an in-world object/marker or a HUD/radar element drawn with a bad texture. Next: capture several frames, `GE_DTEX`/DL dump around that screen position, check whether it also appears in P1's view or in solo MP-stage loads.
