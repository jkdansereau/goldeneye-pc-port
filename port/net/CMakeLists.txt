# ge007net -- the game-independent netplay core (D409, docs/dev/NETPLAY-PLAN.md).
#
# Built as a static library with a CLEAN include path: the game's directory
# include list puts port/shim and the decomp's include/ first, which shadow
# libc headers (<stdlib.h>, <string.h>) with N64 stubs (D324 class -- implicit
# declarations that truncate pointers on x86-64). Nothing in port/net needs a
# game header; game-facing glue lives in port/src/netgame.c / netui.c.
#
# The same sources also build standalone (server + selftest, any compiler):
# see tools_pc/netplay/CMakeLists.txt.

set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

add_library(ge007net STATIC
  net_plat.c
  net_sock.c
  net_proto.c
  net_gamedata.c
  net_session.c
  net_host.c
  net_client.c
  net_runtime.c
  net_stun.c
  net_dirproto.c
  net_http.c
  net_dir.c
)

target_include_directories(ge007net
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  # Only for the global `-include versioninfo.h` compile option.
  PRIVATE "${CMAKE_BINARY_DIR}/port/include"
)

if(WIN32)
  # winhttp: the online directory service client (D410, net_http.c).
  target_link_libraries(ge007net PUBLIC ws2_32 bcrypt winmm winhttp)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(ge007net PUBLIC Threads::Threads)
  # Online directory service over HTTPS / WebSockets (D410): the system
  # libcurl. Without it the build still has LAN / direct-connect play.
  find_package(CURL)
  if(CURL_FOUND)
    target_link_libraries(ge007net PUBLIC CURL::libcurl)
    target_compile_definitions(ge007net PRIVATE GE007_HAVE_CURL=1)
    message(STATUS "Online service client: libcurl ${CURL_VERSION_STRING}")
  else()
    message(STATUS "Online service client: libcurl not found -- LAN / direct play only")
  endif()
endif()
