#pragma once

#ifndef JKSV_ENABLE_CURL
#define JKSV_ENABLE_CURL 1
#endif

#ifndef JKSV_ENABLE_REMOTE
#define JKSV_ENABLE_REMOTE JKSV_ENABLE_CURL
#endif

#if (JKSV_ENABLE_CURL != 0 && JKSV_ENABLE_CURL != 1) || (JKSV_ENABLE_REMOTE != 0 && JKSV_ENABLE_REMOTE != 1)
#error "JKSV feature switches must be 0 or 1"
#endif

#if JKSV_ENABLE_REMOTE && !JKSV_ENABLE_CURL
#error "Remote storage requires curl"
#endif

// The upstream updater distributes Switch 1 binaries and overwrites the running NRO.
// Keep it unavailable on Switch 2 until a compatible release source is configured.
#if JKSV_ENABLE_CURL && !defined(__SWITCH2__)
#define JKSV_ENABLE_UPDATER 1
#else
#define JKSV_ENABLE_UPDATER 0
#endif
