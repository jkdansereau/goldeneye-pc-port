# Determine the target architecture and set TARGET_ARCH / TARGET_IS_64BIT.
# Modelled on the Perfect Dark PC port's cmake/TargetArch.cmake.

function(target_architecture OUT_VAR)
  if(CMAKE_CROSSCOMPILING)
    # Derive from the cross-compiler target, e.g. x86_64-w64-mingw32-gcc
    string(REGEX MATCH "^(i[3-6]86|x86_64|aarch64|arm|powerpc|wasm32)" _arch
           "${CMAKE_C_COMPILER_TARGET}")
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "")
    # D515: a stripped native env (e.g. the build-pc.sh PowerShell re-exec
    # before it restored PROCESSOR_ARCHITECTURE) leaves
    # CMAKE_HOST_SYSTEM_PROCESSOR / CMAKE_SYSTEM_PROCESSOR empty. Ask the
    # compiler instead of silently producing an untagged binary ("ge007..").
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
                    OUTPUT_VARIABLE _dm OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^(i[3-6]86|x86_64|aarch64|arm|powerpc|wasm32)" _arch "${_dm}")
  else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
      set(_arch "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
      set(_arch "i686")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
      set(_arch "aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm)$")
      set(_arch "arm")
    else()
      set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
  endif()

  if(_arch STREQUAL "i386")
    set(_arch "i686")
  endif()

  if(_arch STREQUAL "")
    message(FATAL_ERROR "TargetArch: could not determine the target architecture (CMAKE_SYSTEM_PROCESSOR empty and compiler -dumpmachine did not match); refusing to build an untagged binary -- the 'ge007..' bug, see D515.")
  endif()

  set(${OUT_VAR} "${_arch}" PARENT_SCOPE)
endfunction()
