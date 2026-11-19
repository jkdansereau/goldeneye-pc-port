# D267

## D267 — M-126: IA8 dump probe + per-tile-slot upload-sequence tracking — the three IA8 images in play are an opaque noise card and two sparse mast sprites; none of them is the wall band

Probe infra (`GE_D267`, `gfx_pc.cpp`): PPM dumper + alpha histogram for 32px-wide IA8 strips in `import_texture_ia8`; per-tile-slot upload sequence tracking (`d267_seq` counter + `d267_slot_img[128]` stamped in the `import_texture()` dispatch) so tri-time probes can name which uploaded image a draw uses. Caveat discovered: `gfx_texture_cache_lookup()` returns early **before** the stamp on cache hits (key = fmt+addr+size+pal), so a draw's logged imgseq can be stale — the actual image is the earlier import that populated the cache (traceable via the GE_TEXI census log).

Results (level_36, 15 s): three distinct IA8 images — seq 113 (32×32, **fully opaque**, 1023/1024 texels at A=F; blocky noise pattern), seq 165 (32×64, sparse mast/pole sprite: 1344 transparent / 592 opaque / ~112 mid-alpha), seq 170 (32×64, another sparse sprite). Dumps in `scratch/d267_ia8_tile0_seq*.ppm`. None of these matches the horizon stripe band → the wall is not an IA8 card either. Combined with D266: **the entire IA-focused framing of D236/D265 was examining the wrong texture class.**
