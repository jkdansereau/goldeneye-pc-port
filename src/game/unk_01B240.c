#include <ultra64.h>
#include "unk_01B240.h"


f32 D_8002BB00 = 0;
f32 D_8002BB04 = 0;
f32 D_8002BB08 = 0;
f32 D_8002BB0C = 255.0;
f32 D_8002BB10 = 255.0;
f32 D_8002BB14 = 255.0;
s32 D_8002BB18 = 0;
s32 D_8002BB1C = 0;
s32 D_8002BB20 = 0;
s32 D_8002BB24 = 0;
s32 D_8002BB28 = 0;
s32 D_8002BB2C = 0;











/*
* Address: 0x7F01B240
*/
Gfx* sub_GAME_7F01B240(Gfx* gdl, s32 imgIndex, s32 x, struct FolderSelect* arg3, struct FolderSelect* arg4) {
    f32 temp_f0;
    f32 temp_f12;
    f32 temp_f14;
    f32 temp_f14_2;
    f32 temp_f16;
    f32 temp_f18;
    f32 temp_f2;
    s32 i;

    temp_f0 = arg3->unk00;
    temp_f2 = arg3->unk04;
    temp_f12 = arg3->unk08;
    temp_f14 = arg4->unk00;
    temp_f16 = arg4->unk04;
    temp_f18 = arg4->unk08;
    D_8002BB0C = temp_f14;
    D_8002BB10 = temp_f16;
    D_8002BB14 = temp_f18;

    i = 0;
    while ((i + 1) < 300) {
        gDPLoadTextureBlock(
            gdl++, imgIndex, G_IM_FMT_I, G_IM_SIZ_8b, 440, 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD
        );

        gDPSetPrimColor(
            gdl++, 0, 0, temp_f0 + (((temp_f14 - temp_f0) * i) / 299.0f),
            temp_f2 + (((temp_f16 - temp_f2) * i) / 299.0f), temp_f12 + (((temp_f18 - temp_f12) * i) / 299.0f), 255
        );

        if (x < 0) {
            gSPTextureRectangle(
                gdl++, 0, (i + 0x10) << 2, (440 << 2) - 1, ((((i + 1) + 0x10)) << 2) - 1, G_TX_RENDERTILE, (-x) << 5, 0,
                1 << 10, 1 << 10
            );
        } else {
            gSPTextureRectangle(
                gdl++, x << 2, (i + 0x10) << 2, (440 << 2) - 1, ((((i + 1) + 0x10)) << 2) - 1, G_TX_RENDERTILE, 0, 0,
                1 << 10, 1 << 10
            );
        }
        i++;
        imgIndex += 440;
    }
    D_8002BB08 = temp_f12;
    D_8002BB04 = temp_f2;
    D_8002BB00 = temp_f0;

    return gdl;
}




#ifdef NONMATCHING
// 38% https://decomp.me/scratch/fxkRS
Gfx *sub_GAME_7F01B6E0(Gfx *gdl, s32 arg1, s32 arg2)
{
    s32 i;
    f32 temp_f14 = arg2;
    
    D_8002BB2C = temp_f14;
    D_8002BB24 = temp_f14;
    D_8002BB28 = temp_f14;

    for (i = 0; i + 1 < 218; i++)
    {
        gDPLoadTextureBlock(gdl++, arg1, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        
        arg1 += 0x280;
        
        gDPSetPrimColor(gdl++,
            0,
            0,
            (temp_f14 + (((temp_f14 - temp_f14) * i) / 217.0f)),
            (temp_f14 + (((temp_f14 - temp_f14) * i) / 217.0f)),
            (temp_f14 + (((D_8002BB2C - temp_f14) * i) / 217.0f)),
            0xff);
        
        gSPTextureRectangle(gdl++, 0, (i + 12) << 2, 0, ((i + 13) << 2) - 1, G_TX_RENDERTILE, 0, 0, 1024, 1024);
    }    

    D_8002BB20 = temp_f14;
    D_8002BB1C = temp_f14;
    D_8002BB18 = temp_f14;

    return gdl;
}
}
#else
GLOBAL_ASM(
.text
glabel sub_GAME_7F01B6E0
/* 050210 7F01B6E0 44862000 */  mtc1  $a2, $f4
/* 050214 7F01B6E4 27BDFFB8 */  addiu $sp, $sp, -0x48
/* 050218 7F01B6E8 3C018003 */  lui   $at, %hi(D_8002BB2C)
/* 05021C 7F01B6EC 468023A0 */  cvt.s.w $f14, $f4
/* 050220 7F01B6F0 AFBF0044 */  sw    $ra, 0x44($sp)
/* 050224 7F01B6F4 AFBE0040 */  sw    $fp, 0x40($sp)
/* 050228 7F01B6F8 AFB60038 */  sw    $s6, 0x38($sp)
/* 05022C 7F01B6FC AFB3002C */  sw    $s3, 0x2c($sp)
/* 050230 7F01B700 AFB7003C */  sw    $s7, 0x3c($sp)
/* 050234 7F01B704 E42EBB2C */  swc1  $f14, %lo(D_8002BB2C)($at)
/* 050238 7F01B708 C426BB2C */  lwc1  $f6, %lo(D_8002BB2C)($at)
/* 05023C 7F01B70C 46007406 */  mov.s $f16, $f14
/* 050240 7F01B710 3C018003 */  lui   $at, %hi(D_8002BB24)
/* 050244 7F01B714 E430BB24 */  swc1  $f16, %lo(D_8002BB24)($at)
/* 050248 7F01B718 3C018003 */  lui   $at, %hi(D_8002BB28)
/* 05024C 7F01B71C E42EBB28 */  swc1  $f14, %lo(D_8002BB28)($at)
/* 050250 7F01B720 3C014359 */  li    $at, 0x43590000 # 217.000000
/* 050254 7F01B724 AFB50034 */  sw    $s5, 0x34($sp)
/* 050258 7F01B728 AFB40030 */  sw    $s4, 0x30($sp)
/* 05025C 7F01B72C AFB20028 */  sw    $s2, 0x28($sp)
/* 050260 7F01B730 AFB10024 */  sw    $s1, 0x24($sp)
/* 050264 7F01B734 AFB00020 */  sw    $s0, 0x20($sp)
/* 050268 7F01B738 F7B80018 */  sdc1  $f24, 0x18($sp)
/* 05026C 7F01B73C F7B60010 */  sdc1  $f22, 0x10($sp)
/* 050270 7F01B740 F7B40008 */  sdc1  $f20, 8($sp)
/* 050274 7F01B744 3C130708 */  lui   $s3, (0x07080200 >> 16) # lui $s3, 0x708
/* 050278 7F01B748 3C160713 */  lui   $s6, (0x0713F01A >> 16) # lui $s6, 0x713
/* 05027C 7F01B74C 3C1EF510 */  lui   $fp, (0xF510A000 >> 16) # lui $fp, 0xf510
/* 050280 7F01B750 3C1F0008 */  lui   $ra, (0x00080200 >> 16) # lui $ra, 8
/* 050284 7F01B754 3C0D0400 */  lui   $t5, (0x04000400 >> 16) # lui $t5, 0x400
/* 050288 7F01B758 3C0AE44F */  lui   $t2, (0xE44FF000 >> 16) # lui $t2, 0xe44f
/* 05028C 7F01B75C 44818000 */  mtc1  $at, $f16
/* 050290 7F01B760 00A08025 */  move  $s0, $a1
/* 050294 7F01B764 00003825 */  move  $a3, $zero
/* 050298 7F01B768 46007006 */  mov.s $f0, $f14
/* 05029C 7F01B76C 46007306 */  mov.s $f12, $f14
/* 0502A0 7F01B770 354AF000 */  ori   $t2, (0xE44FF000 & 0xFFFF) # ori $t2, $t2, 0xf000
/* 0502A4 7F01B774 35AD0400 */  ori   $t5, (0x04000400 & 0xFFFF) # ori $t5, $t5, 0x400
/* 0502A8 7F01B778 37FF0200 */  ori   $ra, (0x00080200 & 0xFFFF) # ori $ra, $ra, 0x200
/* 0502AC 7F01B77C 37DEA000 */  ori   $fp, (0xF510A000 & 0xFFFF) # ori $fp, $fp, 0xa000
/* 0502B0 7F01B780 36D6F01A */  ori   $s6, (0x0713F01A & 0xFFFF) # ori $s6, $s6, 0xf01a
/* 0502B4 7F01B784 36730200 */  ori   $s3, (0x07080200 & 0xFFFF) # ori $s3, $s3, 0x200
/* 0502B8 7F01B788 460E7501 */  sub.s $f20, $f14, $f14
/* 0502BC 7F01B78C 3C11FD10 */  lui   $s1, 0xfd10
/* 0502C0 7F01B790 3C12F510 */  lui   $s2, 0xf510
/* 0502C4 7F01B794 460E7581 */  sub.s $f22, $f14, $f14
/* 0502C8 7F01B798 3C14E600 */  lui   $s4, 0xe600
/* 0502CC 7F01B79C 3C15F300 */  lui   $s5, 0xf300
/* 0502D0 7F01B7A0 3C17E700 */  lui   $s7, 0xe700
/* 0502D4 7F01B7A4 24090001 */  li    $t1, 1
/* 0502D8 7F01B7A8 3C0CB300 */  lui   $t4, 0xb300
/* 0502DC 7F01B7AC 3C0BB400 */  lui   $t3, 0xb400
/* 0502E0 7F01B7B0 460E3601 */  sub.s $f24, $f6, $f14
.L7F01B7B4:
/* 0502E4 7F01B7B4 44874000 */  mtc1  $a3, $f8
/* 0502E8 7F01B7B8 00801025 */  move  $v0, $a0
/* 0502EC 7F01B7BC 24840008 */  addiu $a0, $a0, 8
/* 0502F0 7F01B7C0 468040A0 */  cvt.s.w $f2, $f8
/* 0502F4 7F01B7C4 00801825 */  move  $v1, $a0
/* 0502F8 7F01B7C8 AC510000 */  sw    $s1, ($v0)
/* 0502FC 7F01B7CC AC500004 */  sw    $s0, 4($v0)
/* 050300 7F01B7D0 24840008 */  addiu $a0, $a0, 8
/* 050304 7F01B7D4 00802825 */  move  $a1, $a0
/* 050308 7F01B7D8 4602A282 */  mul.s $f10, $f20, $f2
/* 05030C 7F01B7DC AC730004 */  sw    $s3, 4($v1)
/* 050310 7F01B7E0 AC720000 */  sw    $s2, ($v1)
/* 050314 7F01B7E4 24840008 */  addiu $a0, $a0, 8
/* 050318 7F01B7E8 00803025 */  move  $a2, $a0
/* 05031C 7F01B7EC 24840008 */  addiu $a0, $a0, 8
/* 050320 7F01B7F0 ACB40000 */  sw    $s4, ($a1)
/* 050324 7F01B7F4 46105103 */  div.s $f4, $f10, $f16
/* 050328 7F01B7F8 ACA00004 */  sw    $zero, 4($a1)
/* 05032C 7F01B7FC 00804025 */  move  $t0, $a0
/* 050330 7F01B800 24840008 */  addiu $a0, $a0, 8
/* 050334 7F01B804 ACD60004 */  sw    $s6, 4($a2)
/* 050338 7F01B808 ACD50000 */  sw    $s5, ($a2)
/* 05033C 7F01B80C 00801025 */  move  $v0, $a0
/* 050340 7F01B810 AD000004 */  sw    $zero, 4($t0)
/* 050344 7F01B814 AD170000 */  sw    $s7, ($t0)
/* 050348 7F01B818 24840008 */  addiu $a0, $a0, 8
/* 05034C 7F01B81C 00801825 */  move  $v1, $a0
/* 050350 7F01B820 AC5F0004 */  sw    $ra, 4($v0)
/* 050354 7F01B824 AC5E0000 */  sw    $fp, ($v0)
/* 050358 7F01B828 3C0EF200 */  lui   $t6, 0xf200
/* 05035C 7F01B82C AC6E0000 */  sw    $t6, ($v1)
/* 050360 7F01B830 240E0001 */  li    $t6, 1
/* 050364 7F01B834 24840008 */  addiu $a0, $a0, 8
/* 050368 7F01B838 00802825 */  move  $a1, $a0
/* 05036C 7F01B83C 3C0F004F */  lui   $t7, (0x004FC000 >> 16) # lui $t7, 0x4f
/* 050370 7F01B840 24840008 */  addiu $a0, $a0, 8
/* 050374 7F01B844 35EFC000 */  ori   $t7, (0x004FC000 & 0xFFFF) # ori $t7, $t7, 0xc000
/* 050378 7F01B848 00801025 */  move  $v0, $a0
/* 05037C 7F01B84C AC6F0004 */  sw    $t7, 4($v1)
/* 050380 7F01B850 46002180 */  add.s $f6, $f4, $f0
/* 050384 7F01B854 24840008 */  addiu $a0, $a0, 8
/* 050388 7F01B858 3C18FA00 */  lui   $t8, 0xfa00
/* 05038C 7F01B85C 00801825 */  move  $v1, $a0
/* 050390 7F01B860 4459F800 */  cfc1  $t9, $31
/* 050394 7F01B864 44CEF800 */  ctc1  $t6, $31
/* 050398 7F01B868 ACB80000 */  sw    $t8, ($a1)
/* 05039C 7F01B86C 3C014F00 */  li    $at, 0x4F000000 # 2147483648.000000
/* 0503A0 7F01B870 46003224 */  cvt.w.s $f8, $f6
/* 0503A4 7F01B874 24840008 */  addiu $a0, $a0, 8
/* 0503A8 7F01B878 26100280 */  addiu $s0, $s0, 0x280
/* 0503AC 7F01B87C 444EF800 */  cfc1  $t6, $31
/* 0503B0 7F01B880 00000000 */  nop   
/* 0503B4 7F01B884 31CE0078 */  andi  $t6, $t6, 0x78
/* 0503B8 7F01B888 51C00013 */  beql  $t6, $zero, .L7F01B8D8
/* 0503BC 7F01B88C 440E4000 */   mfc1  $t6, $f8
/* 0503C0 7F01B890 44814000 */  mtc1  $at, $f8
/* 0503C4 7F01B894 240E0001 */  li    $t6, 1
/* 0503C8 7F01B898 46083201 */  sub.s $f8, $f6, $f8
/* 0503CC 7F01B89C 44CEF800 */  ctc1  $t6, $31
/* 0503D0 7F01B8A0 00000000 */  nop   
/* 0503D4 7F01B8A4 46004224 */  cvt.w.s $f8, $f8
/* 0503D8 7F01B8A8 444EF800 */  cfc1  $t6, $31
/* 0503DC 7F01B8AC 00000000 */  nop   
/* 0503E0 7F01B8B0 31CE0078 */  andi  $t6, $t6, 0x78
/* 0503E4 7F01B8B4 15C00005 */  bnez  $t6, .L7F01B8CC
/* 0503E8 7F01B8B8 00000000 */   nop   
/* 0503EC 7F01B8BC 440E4000 */  mfc1  $t6, $f8
/* 0503F0 7F01B8C0 3C018000 */  lui   $at, 0x8000
/* 0503F4 7F01B8C4 10000007 */  b     .L7F01B8E4
/* 0503F8 7F01B8C8 01C17025 */   or    $t6, $t6, $at
.L7F01B8CC:
/* 0503FC 7F01B8CC 10000005 */  b     .L7F01B8E4
/* 050400 7F01B8D0 240EFFFF */   li    $t6, -1
/* 050404 7F01B8D4 440E4000 */  mfc1  $t6, $f8
.L7F01B8D8:
/* 050408 7F01B8D8 00000000 */  nop   
/* 05040C 7F01B8DC 05C0FFFB */  bltz  $t6, .L7F01B8CC
/* 050410 7F01B8E0 00000000 */   nop   
.L7F01B8E4:
/* 050414 7F01B8E4 44D9F800 */  ctc1  $t9, $31
/* 050418 7F01B8E8 01C07825 */  move  $t7, $t6
/* 05041C 7F01B8EC 240E0001 */  li    $t6, 1
/* 050420 7F01B8F0 4602B282 */  mul.s $f10, $f22, $f2
/* 050424 7F01B8F4 000FC600 */  sll   $t8, $t7, 0x18
/* 050428 7F01B8F8 3C014F00 */  li    $at, 0x4F000000 # 2147483648.000000
/* 05042C 7F01B8FC 46105103 */  div.s $f4, $f10, $f16
/* 050430 7F01B900 46046180 */  add.s $f6, $f12, $f4
/* 050434 7F01B904 4459F800 */  cfc1  $t9, $31
/* 050438 7F01B908 44CEF800 */  ctc1  $t6, $31
/* 05043C 7F01B90C 00000000 */  nop   
/* 050440 7F01B910 46003224 */  cvt.w.s $f8, $f6
/* 050444 7F01B914 444EF800 */  cfc1  $t6, $31
/* 050448 7F01B918 00000000 */  nop   
/* 05044C 7F01B91C 31CE0078 */  andi  $t6, $t6, 0x78
/* 050450 7F01B920 51C00013 */  beql  $t6, $zero, .L7F01B970
/* 050454 7F01B924 440E4000 */   mfc1  $t6, $f8
/* 050458 7F01B928 44814000 */  mtc1  $at, $f8
/* 05045C 7F01B92C 240E0001 */  li    $t6, 1
/* 050460 7F01B930 46083201 */  sub.s $f8, $f6, $f8
/* 050464 7F01B934 44CEF800 */  ctc1  $t6, $31
/* 050468 7F01B938 00000000 */  nop   
/* 05046C 7F01B93C 46004224 */  cvt.w.s $f8, $f8
/* 050470 7F01B940 444EF800 */  cfc1  $t6, $31
/* 050474 7F01B944 00000000 */  nop   
/* 050478 7F01B948 31CE0078 */  andi  $t6, $t6, 0x78
/* 05047C 7F01B94C 15C00005 */  bnez  $t6, .L7F01B964
/* 050480 7F01B950 00000000 */   nop   
/* 050484 7F01B954 440E4000 */  mfc1  $t6, $f8
/* 050488 7F01B958 3C018000 */  lui   $at, 0x8000
/* 05048C 7F01B95C 10000007 */  b     .L7F01B97C
/* 050490 7F01B960 01C17025 */   or    $t6, $t6, $at
.L7F01B964:
/* 050494 7F01B964 10000005 */  b     .L7F01B97C
/* 050498 7F01B968 240EFFFF */   li    $t6, -1
/* 05049C 7F01B96C 440E4000 */  mfc1  $t6, $f8
.L7F01B970:
/* 0504A0 7F01B970 00000000 */  nop   
/* 0504A4 7F01B974 05C0FFFB */  bltz  $t6, .L7F01B964
/* 0504A8 7F01B978 00000000 */   nop   
.L7F01B97C:
/* 0504AC 7F01B97C 44D9F800 */  ctc1  $t9, $31
/* 0504B0 7F01B980 31CF00FF */  andi  $t7, $t6, 0xff
/* 0504B4 7F01B984 000FCC00 */  sll   $t9, $t7, 0x10
/* 0504B8 7F01B988 4602C282 */  mul.s $f10, $f24, $f2
/* 0504BC 7F01B98C 03197025 */  or    $t6, $t8, $t9
/* 0504C0 7F01B990 24180001 */  li    $t8, 1
/* 0504C4 7F01B994 3C014F00 */  li    $at, 0x4F000000 # 2147483648.000000
/* 0504C8 7F01B998 46105103 */  div.s $f4, $f10, $f16
/* 0504CC 7F01B99C 46047180 */  add.s $f6, $f14, $f4
/* 0504D0 7F01B9A0 444FF800 */  cfc1  $t7, $31
/* 0504D4 7F01B9A4 44D8F800 */  ctc1  $t8, $31
/* 0504D8 7F01B9A8 00000000 */  nop   
/* 0504DC 7F01B9AC 46003224 */  cvt.w.s $f8, $f6
/* 0504E0 7F01B9B0 4458F800 */  cfc1  $t8, $31
/* 0504E4 7F01B9B4 00000000 */  nop   
/* 0504E8 7F01B9B8 33180078 */  andi  $t8, $t8, 0x78
/* 0504EC 7F01B9BC 53000013 */  beql  $t8, $zero, .L7F01BA0C
/* 0504F0 7F01B9C0 44184000 */   mfc1  $t8, $f8
/* 0504F4 7F01B9C4 44814000 */  mtc1  $at, $f8
/* 0504F8 7F01B9C8 24180001 */  li    $t8, 1
/* 0504FC 7F01B9CC 46083201 */  sub.s $f8, $f6, $f8
/* 050500 7F01B9D0 44D8F800 */  ctc1  $t8, $31
/* 050504 7F01B9D4 00000000 */  nop   
/* 050508 7F01B9D8 46004224 */  cvt.w.s $f8, $f8
/* 05050C 7F01B9DC 4458F800 */  cfc1  $t8, $31
/* 050510 7F01B9E0 00000000 */  nop   
/* 050514 7F01B9E4 33180078 */  andi  $t8, $t8, 0x78
/* 050518 7F01B9E8 17000005 */  bnez  $t8, .L7F01BA00
/* 05051C 7F01B9EC 00000000 */   nop   
/* 050520 7F01B9F0 44184000 */  mfc1  $t8, $f8
/* 050524 7F01B9F4 3C018000 */  lui   $at, 0x8000
/* 050528 7F01B9F8 10000007 */  b     .L7F01BA18
/* 05052C 7F01B9FC 0301C025 */   or    $t8, $t8, $at
.L7F01BA00:
/* 050530 7F01BA00 10000005 */  b     .L7F01BA18
/* 050534 7F01BA04 2418FFFF */   li    $t8, -1
/* 050538 7F01BA08 44184000 */  mfc1  $t8, $f8
.L7F01BA0C:
/* 05053C 7F01BA0C 00000000 */  nop   
/* 050540 7F01BA10 0700FFFB */  bltz  $t8, .L7F01BA00
/* 050544 7F01BA14 00000000 */   nop   
.L7F01BA18:
/* 050548 7F01BA18 44CFF800 */  ctc1  $t7, $31
/* 05054C 7F01BA1C 331900FF */  andi  $t9, $t8, 0xff
/* 050550 7F01BA20 00197A00 */  sll   $t7, $t9, 8
/* 050554 7F01BA24 01CFC025 */  or    $t8, $t6, $t7
/* 050558 7F01BA28 371900FF */  ori   $t9, $t8, 0xff
/* 05055C 7F01BA2C 24EE000D */  addiu $t6, $a3, 0xd
/* 050560 7F01BA30 000E7880 */  sll   $t7, $t6, 2
/* 050564 7F01BA34 ACB90004 */  sw    $t9, 4($a1)
/* 050568 7F01BA38 25F8FFFF */  addiu $t8, $t7, -1
/* 05056C 7F01BA3C 33190FFF */  andi  $t9, $t8, 0xfff
/* 050570 7F01BA40 24EF000C */  addiu $t7, $a3, 0xc
/* 050574 7F01BA44 000FC080 */  sll   $t8, $t7, 2
/* 050578 7F01BA48 032A7025 */  or    $t6, $t9, $t2
/* 05057C 7F01BA4C 33190FFF */  andi  $t9, $t8, 0xfff
/* 050580 7F01BA50 AC590004 */  sw    $t9, 4($v0)
/* 050584 7F01BA54 AC4E0000 */  sw    $t6, ($v0)
/* 050588 7F01BA58 01203825 */  move  $a3, $t1
/* 05058C 7F01BA5C 00802825 */  move  $a1, $a0
/* 050590 7F01BA60 25290001 */  addiu $t1, $t1, 1
/* 050594 7F01BA64 AC600004 */  sw    $zero, 4($v1)
/* 050598 7F01BA68 AC6B0000 */  sw    $t3, ($v1)
/* 05059C 7F01BA6C 292100DA */  slti  $at, $t1, 0xda
/* 0505A0 7F01BA70 ACAD0004 */  sw    $t5, 4($a1)
/* 0505A4 7F01BA74 ACAC0000 */  sw    $t4, ($a1)
/* 0505A8 7F01BA78 1420FF4E */  bnez  $at, .L7F01B7B4
/* 0505AC 7F01BA7C 24840008 */   addiu $a0, $a0, 8
/* 0505B0 7F01BA80 3C018003 */  lui   $at, %hi(D_8002BB20)
/* 0505B4 7F01BA84 E42EBB20 */  swc1  $f14, %lo(D_8002BB20)($at)
/* 0505B8 7F01BA88 8FBF0044 */  lw    $ra, 0x44($sp)
/* 0505BC 7F01BA8C 3C018003 */  lui   $at, %hi(D_8002BB1C)
/* 0505C0 7F01BA90 E42CBB1C */  swc1  $f12, %lo(D_8002BB1C)($at)
/* 0505C4 7F01BA94 3C018003 */  lui   $at, %hi(D_8002BB18)
/* 0505C8 7F01BA98 D7B40008 */  ldc1  $f20, 8($sp)
/* 0505CC 7F01BA9C D7B60010 */  ldc1  $f22, 0x10($sp)
/* 0505D0 7F01BAA0 D7B80018 */  ldc1  $f24, 0x18($sp)
/* 0505D4 7F01BAA4 8FB00020 */  lw    $s0, 0x20($sp)
/* 0505D8 7F01BAA8 8FB10024 */  lw    $s1, 0x24($sp)
/* 0505DC 7F01BAAC 8FB20028 */  lw    $s2, 0x28($sp)
/* 0505E0 7F01BAB0 8FB3002C */  lw    $s3, 0x2c($sp)
/* 0505E4 7F01BAB4 8FB40030 */  lw    $s4, 0x30($sp)
/* 0505E8 7F01BAB8 8FB50034 */  lw    $s5, 0x34($sp)
/* 0505EC 7F01BABC 8FB60038 */  lw    $s6, 0x38($sp)
/* 0505F0 7F01BAC0 8FB7003C */  lw    $s7, 0x3c($sp)
/* 0505F4 7F01BAC4 8FBE0040 */  lw    $fp, 0x40($sp)
/* 0505F8 7F01BAC8 E420BB18 */  swc1  $f0, %lo(D_8002BB18)($at)
/* 0505FC 7F01BACC 27BD0048 */  addiu $sp, $sp, 0x48
/* 050600 7F01BAD0 03E00008 */  jr    $ra
/* 050604 7F01BAD4 00801025 */   move  $v0, $a0
)
#endif
 

