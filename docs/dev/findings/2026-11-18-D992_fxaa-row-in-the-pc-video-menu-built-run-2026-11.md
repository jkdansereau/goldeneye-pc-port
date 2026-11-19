# D992

## D992: FXAA row in the PC Video menu -- BUILT + RUN 2026-11-18 (Windows cross-build; maintainer request)

`Video.FXAA` (the pass itself and its PC enablement are D991) gets a row on the PC Video page after Anti-aliasing: live, default 0 (the Switch preset keeps 1), restored by the VIDEO reset. The key was already read on every platform; before D991 only the Switch acted on it. Verified: Windows, Linux and Switch build; the key is written to the ini and FXAA draws on GL and Vulkan (D991). The menu row itself was not opened (no scripted key input).
