# D980

## D980: Jungle fern leaves drawn over the leaves in front of them -- foliage props draw their translucent surfaces as depth-writing cut-outs (`Video.FoliageDepth`, default on; Original N64 restores the N64 behaviour) -- BUILT + MAINTAINER-CHECKED 2026-11-16 (Ryujinx, Jungle; Switch build; render only; issue #35)

**Symptom.** In Jungle, far fern leaves cover the near ones. Same picture in v0.1.0, v0.2.0 and ares v148 (paraLLEl-RDP) on the original ROM: N64 behaviour, more visible on the port because the image is sharper.

**Cause.** Objects draw with `PropType` 9 (`chrobjRenderProp`), which in `modelApplyRenderModeType3`/`Type4` gives the model's secondary surfaces `G_RM_AA_ZB_XLU_SURF2`: depth test, no depth write. The foliage leaves are on those surfaces, so they stack in the model's authored order and a far leaf drawn after a near one covers it.

**Fix (render only, `#ifdef PORT`).**
- `drawdistgameplay.c`: `s_foliageModels` (`PROP_JUNGLE3_TREE`, `PROP_JUNGLE5_TREE`, `PROP_PALM`, `PROP_PALMTREE`, `PROP_PLANT1/11/2/2B/3`), `portPropIsFoliage`, `portFoliageDraw`.
- `propobj.c chrobjRenderProp`: `portFoliageDraw` is set around `sub_GAME_7F04AC20` for an opaque foliage prop (the display list is built inside that call).
- `model.c`: for those props the secondary surfaces use `G_RM_AA_ZB_TEX_EDGE2` (alpha cut-out with depth write), the mode the same functions already use for fading objects. fast3d maps it to the texture-edge discard.
- Options: `Video.FoliageDepth` row ("Foliage depth fix") on Video, live, in the Custom set, reset default 1; the Original N64 preset sets 0.

**Verification.** Maintainer, Ryujinx, Jungle: leaves sort correctly ("c'est niquel"). Switch NRO build.

**Related, not changed.** The Dam intro truck wheels through the mudguards (D306) are also N64 behaviour, now confirmed on ares as well; it is opaque geometry, not this mechanism.

**Confidence.** High for the ferns checked by eye; the other listed models share the path but were not each checked.
