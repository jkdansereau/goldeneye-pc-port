# Standalone build of the netplay tools (D409): the matchmaking / relay server
# and the protocol selftest. Needs no ROM, no SDL, no game sources.
#
#   cmake -S tools_pc/netplay -B build-net -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-net --config Release
#   build-net/netplay_selftest          (exit 0 = pass)
#   build-net/ge007-netserver --port 27008
#
# Works with GCC/Clang (Linux, MinGW) and MSVC.

cmake_minimum_required(VERSION 3.16)
project(ge007_netplay_tools C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../port/net")

# AddressSanitizer (+ UBSan where available) for every target: the fuzzer's
# memory-error oracle, and a stricter selftest.
option(NETPLAY_SANITIZE "Build the netplay tools with AddressSanitizer" OFF)
if(NETPLAY_SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

add_library(ge007net_tools STATIC
  "${NET_DIR}/net_plat.c"
  "${NET_DIR}/net_sock.c"
  "${NET_DIR}/net_proto.c"
  "${NET_DIR}/net_gamedata.c"
  "${NET_DIR}/net_session.c"
  "${NET_DIR}/net_host.c"
  "${NET_DIR}/net_client.c"
  "${NET_DIR}/net_runtime.c"
  "${NET_DIR}/net_stun.c"
  "${NET_DIR}/net_dirproto.c"
  "${NET_DIR}/net_http.c"
  "${NET_DIR}/net_dir.c"
)
target_include_directories(ge007net_tools PUBLIC "${NET_DIR}/include")

if(MSVC)
  target_compile_options(ge007net_tools PRIVATE /W4)
  target_compile_definitions(ge007net_tools PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(ge007net_tools PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
  target_link_libraries(ge007net_tools PUBLIC ws2_32 bcrypt winmm winhttp)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(ge007net_tools PUBLIC Threads::Threads)
  # Online directory client (D410): HTTPS / WebSockets through libcurl.
  find_package(CURL)
  if(CURL_FOUND)
    target_link_libraries(ge007net_tools PUBLIC CURL::libcurl)
    target_compile_definitions(ge007net_tools PRIVATE GE007_HAVE_CURL=1)
  endif()
endif()

add_executable(ge007-netserver netserver_main.c)
target_link_libraries(ge007-netserver PRIVATE ge007net_tools)

add_executable(netplay_selftest netplay_selftest.c)
target_link_libraries(netplay_selftest PRIVATE ge007net_tools)

# Robustness fuzzer (D412): hostile hosts, joiners and services against the
# real code. Best run in a sanitizer build:
#   cmake -S tools_pc/netplay -B build-net-asan -DNETPLAY_SANITIZE=ON
add_executable(netfuzz netfuzz.c)
target_link_libraries(netfuzz PRIVATE ge007net_tools)

# Directory client harness, driven by cloudflare/test/integration.mjs against
# the local mock service (D410).
add_executable(netdir_test netdir_test.c)
target_link_libraries(netdir_test PRIVATE ge007net_tools)

# The game's network runtime end to end (service + STUN + hole punch +
# direct connection), driven by cloudflare/test/online.mjs (D410).
add_executable(netonline_test netonline_test.c)
target_link_libraries(netonline_test PRIVATE ge007net_tools)

enable_testing()
add_test(NAME netplay_selftest COMMAND netplay_selftest)
# A short fuzz pass (D412); longer runs: netfuzz --iters 150 --seed N
add_test(NAME netfuzz_smoke COMMAND netfuzz --iters 8 --seed 1)
