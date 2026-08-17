#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/*
 * Platform abstraction for the GE PC port.
 *
 * Provides the small set of host primitives the port layer needs, with
 * per-OS implementations selected at compile time. Modelled on the PD port's
 * port/include/platform.h.
 */

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32) || defined(_WIN64)
  #define PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    #define PLATFORM_IOS 1
  #else
    #define PLATFORM_MACOS 1
  #endif
#elif defined(__linux__)
  #define PLATFORM_LINUX 1
#else
  #error "Unsupported platform"
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__)
  #define PLATFORM_64BIT 1
#endif

/* Constructor attribute: run before main(). */
#if defined(_MSC_VER)
  #define PD_CONSTRUCTOR
  #define PD_EXPORT
#else
  #define PD_CONSTRUCTOR __attribute__((constructor))
  #define PD_EXPORT __attribute__((visibility("default")))
#endif

/* Log levels (see system.h). */
enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG,
};

#endif /* PORT_PLATFORM_H */
