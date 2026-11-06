#pragma once

// Main header of the Miyoo package. The package provides display setup (the
// Linux framebuffer, /dev/fb0) and input setup (the buttons, an evdev device)
// for Miyoo handhelds.

#ifdef _WIN32
#ifdef DEKI_MIYOO_EXPORTS
#define DEKI_MIYOO_API __declspec(dllexport)
#else
#define DEKI_MIYOO_API __declspec(dllimport)
#endif
#else
#define DEKI_MIYOO_API __attribute__((visibility("default")))
#endif
