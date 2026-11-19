/* Issue #44: the Vulkan backend's SPIR-V shader pack (ge007_spv.bin, built by
 * cmake/VulkanShaders.cmake; format in gfx_dk_layout.h), embedded in the
 * executable. The assembler finds the file through -Wa,-I<build dir>. */
#if defined(_WIN32)
    .section .rdata, "dr"
#else
    .section .rodata.ge007_spv, "a"
#endif
    .balign 256
    .global ge007_spv_pack
ge007_spv_pack:
    .incbin "ge007_spv.bin"
    .global ge007_spv_pack_end
ge007_spv_pack_end:
    .byte 0
#if !defined(_WIN32)
    .section .note.GNU-stack, "", %progbits
#endif
