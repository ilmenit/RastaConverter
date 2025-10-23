#pragma once

#ifndef NO_GUI

#if defined(_WIN32)

struct SDL_Window;
struct SDL_Surface;

bool WindowIconWin32_SetTaskbarIcon(SDL_Window* window, SDL_Surface* surface);
bool WindowIconWin32_UpdateAppUserModel(SDL_Window* window, SDL_Surface* surface);

#endif // defined(_WIN32)

#endif // NO_GUI


