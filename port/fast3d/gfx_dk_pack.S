/* D973: the deko3d shader pack (ge007_dksh.bin, built by cmake/SwitchPort.cmake
 * switch_deko3d_shaders; format in gfx_dk_layout.h), embedded in the NRO.
 * The assembler finds the file through -Wa,-I<build dir>. */
#ifdef __SWITCH__
    .section .rodata.ge007_dksh, "a"
    .balign 256
    .global ge007_dksh_pack
ge007_dksh_pack:
    .incbin "ge007_dksh.bin"
    .global ge007_dksh_pack_end
ge007_dksh_pack_end:
    .byte 0
    .section .note.GNU-stack, "", %progbits
#endif
