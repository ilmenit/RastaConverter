#pragma once

#ifndef NO_GUI

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX

#include <SDL.h>

bool WindowIconMac_SetDockIcon(SDL_Surface* surface);

#endif // defined(__APPLE__) && TARGET_OS_OSX

#endif // NO_GUI


