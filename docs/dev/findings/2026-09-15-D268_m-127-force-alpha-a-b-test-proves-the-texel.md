# D268

## D268 — M-127: force-alpha A/B test proves the texel-alpha blend pipeline works end-to-end — the wall is NOT a broken blend path

`GE_D268_FORCEA=0|15` in `import_texture_ia8` forces every IA8 texel alpha to 0 (transparent) or F (opaque). Three captures at frame 730 (`scratch/d268/{normal,alpha0,alphaF}_730.ppm`): the horizon band region vanishes with alpha=0 and becomes a solid bright block with alpha=15. The GL blend state, texel-alpha path, and shader generation all composite correctly — whatever renders the wall does so with *correct* blending of *something*. (Side note: the user's 9/14 BSOD 0x119 was HWiNFO64/nvml.dll, not the game — game log clean.)
