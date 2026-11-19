# Issue #44: the Vulkan backend's SPIR-V shader pack, the PC twin of the
# deko3d pack (cmake/SwitchPort.cmake, D973). Same generator (tools_pc/dkshgen),
# same GLSL (the generated combiners and port/switch/shaders/), same pack
# layout (port/fast3d/gfx_dk_layout.h, blobs = SPIR-V words); only the compiler
# differs: glslangValidator -V instead of uam. --sub 4 moves every uniform block
# up by 4 bindings so they share descriptor set 0 with the two samplers
# (binding 0, 1): Draw 4, Comb 5, Mesh 6 (gfx_vulkan.cpp's set layout).
#
# Script mode (-P, see vulkan_shaders below): host dkshgen, glslang, pack.
if(VK_PACK_RUN)
  file(MAKE_DIRECTORY "${VK_OUT}/glsl")
  execute_process(COMMAND "${VK_HOST_CXX}" -std=c++20 -O2 -I "${VK_SRC}/port/fast3d"
      "${VK_SRC}/tools_pc/dkshgen/dkshgen.cpp" "${VK_SRC}/port/fast3d/gfx_cc.cpp" -o "${VK_OUT}/dkshgen"
    RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "dkshgen: host build failed (${VK_HOST_CXX})")
  endif()
  file(GLOB _old "${VK_OUT}/glsl/*")
  if(_old)
    file(REMOVE ${_old})
  endif()
  execute_process(COMMAND "${VK_OUT}/dkshgen" glsl "${VK_LIST}" "${VK_OUT}/glsl" RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "dkshgen glsl failed")
  endif()
  macro(_vk_spv_sub sub stage src out)
    execute_process(COMMAND "${VK_GLSLANG}" -V --target-env vulkan1.3 --sub ${sub} -S ${stage} -o "${out}" "${src}"
      RESULT_VARIABLE _ur OUTPUT_VARIABLE _uo ERROR_VARIABLE _ue)
    if(NOT _ur EQUAL 0)
      message(FATAL_ERROR "glslangValidator failed on ${src}:
${_uo}${_ue}")
    endif()
  endmacro()
  macro(_vk_spv stage src out)
    _vk_spv_sub(4 ${stage} "${src}" "${out}")
  endmacro()
  set(_pack "")
  _vk_spv(vert "${VK_SRC}/port/switch/shaders/uber.vert.glsl" "${VK_OUT}/glsl/uber.vert.spv")
  _vk_spv(frag "${VK_SRC}/port/switch/shaders/uber.frag.glsl" "${VK_OUT}/glsl/uber.frag.spv")
  # Vulkan GLSL names the vertex index gl_VertexIndex (uam takes gl_VertexID)
  file(READ "${VK_SRC}/port/switch/shaders/fullscreen.vert.glsl" _fs)
  string(REPLACE "gl_VertexID" "gl_VertexIndex" _fs "${_fs}")
  file(WRITE "${VK_OUT}/glsl/fullscreen.vk.vert.glsl" "${_fs}")
  _vk_spv(vert "${VK_OUT}/glsl/fullscreen.vk.vert.glsl" "${VK_OUT}/glsl/fullscreen.vert.spv")
  _vk_spv(frag "${VK_SRC}/port/switch/shaders/fxaa.frag.glsl" "${VK_OUT}/glsl/fxaa.frag.spv")
  # GE+ sky and sea (D997): skyplus and skylut share skycommon.glsl, pasted at their `#pragma skycommon` line (as
  # cmake/SwitchPort.cmake does for uam). They sample five textures (bindings 0..4), so their uniform block moves up by 5
  # (to binding 5) and they use the sky descriptor set layout of gfx_vulkan.cpp, not the draws' (whose binding 4 is a block).
  file(READ "${VK_SRC}/port/switch/shaders/skycommon.glsl" _skycommon)
  foreach(_sc skyplus skylut)
    file(READ "${VK_SRC}/port/switch/shaders/${_sc}.frag.glsl" _scsrc)
    string(REPLACE "#pragma skycommon" "${_skycommon}" _scsrc "${_scsrc}")
    file(WRITE "${VK_OUT}/glsl/${_sc}.vk.frag.glsl" "${_scsrc}")
    _vk_spv_sub(5 frag "${VK_OUT}/glsl/${_sc}.vk.frag.glsl" "${VK_OUT}/glsl/${_sc}.frag.spv")
  endforeach()
  string(APPEND _pack "0 1 ${VK_OUT}/glsl/uber.vert.spv ${VK_OUT}/glsl/uber.frag.spv -
")
  # the post passes (gfx_dk_layout.h DK_PACK_FULLSCREEN / FXAA / SKYPLUS)
  string(APPEND _pack "0 2 ${VK_OUT}/glsl/fullscreen.vert.spv - -
")
  string(APPEND _pack "0 3 - ${VK_OUT}/glsl/fxaa.frag.spv -
")
  string(APPEND _pack "0 4 - ${VK_OUT}/glsl/skyplus.frag.spv -
")
  string(APPEND _pack "0 6 - ${VK_OUT}/glsl/skylut.frag.spv -
")
  file(STRINGS "${VK_OUT}/glsl/list.txt" _lines)
  set(_n 0)
  foreach(_l IN LISTS _lines)
    string(REPLACE " " ";" _f "${_l}")
    list(GET _f 0 _id0)
    list(GET _f 1 _id1)
    list(GET _f 2 _base)
    _vk_spv(vert "${VK_OUT}/glsl/${_base}.vert.glsl" "${VK_OUT}/glsl/${_base}.vert.spv")
    _vk_spv(vert "${VK_OUT}/glsl/${_base}.mesh.vert.glsl" "${VK_OUT}/glsl/${_base}.mesh.vert.spv")
    _vk_spv(frag "${VK_OUT}/glsl/${_base}.frag.glsl" "${VK_OUT}/glsl/${_base}.frag.spv")
    string(APPEND _pack "${_id0} ${_id1} ${VK_OUT}/glsl/${_base}.vert.spv ${VK_OUT}/glsl/${_base}.frag.spv ${VK_OUT}/glsl/${_base}.mesh.vert.spv
")
    math(EXPR _n "${_n} + 1")
  endforeach()
  file(WRITE "${VK_OUT}/pack.txt" "${_pack}")
  execute_process(COMMAND "${VK_OUT}/dkshgen" pack "${VK_PACK}" "${VK_OUT}/pack.txt" RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "dkshgen pack failed")
  endif()
  message(STATUS "Vulkan shader pack: ${_n} combiners compiled to SPIR-V")
  return()
endif()

# Builds the pack and links it into <target> through port/fast3d/gfx_vk_pack.S
# (.incbin), in a small library of its own: the game's compile options (the
# forced versioninfo.h, the shadow headers) are not meant for an assembler file.
macro(vulkan_shaders target)
  find_program(VK_GLSLANG glslangValidator REQUIRED)
  find_program(VK_HOST_CXX NAMES c++ g++ HINTS /usr/bin REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  set(_vk_pack "${CMAKE_BINARY_DIR}/ge007_spv.bin")
  file(GLOB _vk_glsl "${CMAKE_SOURCE_DIR}/port/switch/shaders/*.glsl")
  add_custom_command(OUTPUT "${_vk_pack}"
    COMMAND "${CMAKE_COMMAND}" -DVK_PACK_RUN=ON
            "-DVK_SRC=${CMAKE_SOURCE_DIR}" "-DVK_OUT=${CMAKE_BINARY_DIR}/spv"
            "-DVK_LIST=${CMAKE_SOURCE_DIR}/port/switch/shaders/combiners.txt"
            "-DVK_PACK=${_vk_pack}" "-DVK_GLSLANG=${VK_GLSLANG}" "-DVK_HOST_CXX=${VK_HOST_CXX}"
            -P "${CMAKE_SOURCE_DIR}/cmake/VulkanShaders.cmake"
    DEPENDS "${CMAKE_SOURCE_DIR}/port/switch/shaders/combiners.txt" ${_vk_glsl}
            "${CMAKE_SOURCE_DIR}/tools_pc/dkshgen/dkshgen.cpp"
            "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_cc.cpp"
            "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_cc.h"
            "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_dk_layout.h"
            "${CMAKE_SOURCE_DIR}/cmake/VulkanShaders.cmake"
    COMMENT "Vulkan shader pack (dkshgen + glslangValidator)"
    VERBATIM)
  set(_vk_s "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_vk_pack.S")
  add_library(ge_vkpack STATIC "${_vk_s}")
  set_property(TARGET ge_vkpack PROPERTY INCLUDE_DIRECTORIES "")
  set_property(TARGET ge_vkpack PROPERTY COMPILE_OPTIONS "-Wa,-I${CMAKE_BINARY_DIR}")
  set_source_files_properties("${_vk_s}" PROPERTIES OBJECT_DEPENDS "${_vk_pack}")
  target_link_libraries(${target} ge_vkpack)
endmacro()
