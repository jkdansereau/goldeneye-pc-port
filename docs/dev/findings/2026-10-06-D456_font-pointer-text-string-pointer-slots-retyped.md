# D456

<!-- from the old section-F/H table row -->
| D456 | **Font-pointer / text-string-pointer slots retyped from `s32` to real pointers (ABI/layout exception; front.c, options.c, language.c, bondview*, mpmenu.c, propobj.c:3909 alias replaced by named field).** `frontPrintText`/`display_aligned_white_text_to_screen` font params, `copy_1st/2ndfonttable`, `setFontTables`, ~26 options.c locals, `g_LangBanks[]` + `langGet` result, `player.ptr_text_*_mp_award`. front.c 186->2, options.c 139->0 int-conversion warnings. FIXED (static + 1 launch; not gameplay-verified). |

## D456 - font/text pointer slots held in s32 (ABI/layout exception, 2026-09-30)

**Source.** Read-side census (ABI-CENSUS-READSIDE.md): 286 of 618 -Wint-conversion warnings were two slot kinds, font pointers (`struct font *` / `struct fontchar *`) and `langGet()` strings, parked in `s32`. Implicit ptr->int narrowing has no cast, so a cast census never sees it. Pointers come from arena memory; any arena >= 2 GiB sign-extends to wild pointers, >= 4 GiB truncates.

**Change (all `#ifdef PORT`, `#else` = original; no logic change).**
- front.c: `frontPrintText`, `display_aligned_white_text_to_screen` (+ forward decl) font params -> `struct fontchar *` / `struct font *`; `constructor_menu16_nocontrollers` `text` -> `char *`; `frontGetPlayersFavoriteWeaponInHand` returns `u16 *` (was `int`, also in `port/include/pc_protos.h`: the unprototyped `int` decl made mpmenu.c:1596 truncate the pointer).
- bondview.c/.h, bondview_internal.h, bondview2.c: `copy_1stfonttable`/`copy_2ndfonttable`, `setFontTables` params.
- options.c: 26 locals in 9 functions (pFontFile/pFontChars/*2, text locals, sp78/sp7C in draw_current_hand_item_and_ammo). NOTE the `spNN` names are per-function: `sp4C` is text in draw_abort_cancel_confirm but a colour in draw_text_mission_status, and sp50/sp4C are coordinates in draw_text_q_watch_v201_beta; retype by use, not by name.
- language.c: `g_LangBanks` -> `u32 *[45]`; `langGet` result built as `(u8 *)textbank_ptr + off` (was a `u32` accumulator, truncating >= 4 GiB). bondview.h: `player.ptr_text_first/second_mp_award` -> `char *` (live struct, never serialised; adds 4 bytes of pointer width, sizeof is taken from the type in player.c).
- mpmenu.c: `h2` (only receives langGet) -> `char *`; cast at the favourite-weapon assignment.
- propobj.c:3909: `!((s32)obj->model & 0x400)` -> `!(prop->chr->chrflags & CHRFLAG_HIDDEN)`. Prop type is VIEWER/CHR there and `obj`/`chr` share a union slot (bondtypes.h:2342-2344), so identical behaviour today (two layout shifts cancelled), now by name.

**Left (not a type/cast swap, or out of scope).** mpmenu.c `q` (dual-use: team colour, then `langGet` string; needs a separate local); front.c `ptr_menu_videobuffer` and `modelSetAnimation(animation_table_ptrs1)`; language.c:364/375 `romCopy` p2 (ROM address, tag R); `sp70 = bondinvGetTextbyInvIndex` and the options.c:34 `OS_PHYSICAL_TO_K0` macro warning; JP/EU `dword_CODE_bss_jp...` font arrays (not built on ntsc-final).

**Verified.** Falsifier held: front.c 186->2 (census predicted ~172 from the two params), options.c 139->0. Full ntsc-final build clean. Disassembly: font params read as QWORD stack slots, no `mov r32,r32` before pointer args; `langGet` adds at 64-bit. One default-boot launch (GE_PCDUMP 200-1400:200, GE_QUITFRAME=1450) ran rc=0, no crash log; frame 200 (legal screen, display_aligned_white_text_to_screen + textRender) renders all text correctly. No baseline-build frame diff was taken (budget); menu/watch/MP text paths not exercised at runtime. Confidence: [H] types/ABI, [M] runtime.
