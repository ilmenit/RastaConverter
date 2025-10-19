#pragma once

#ifndef NO_GUI

#include <string>

struct SDL_Window;
struct FIBITMAP;

// Helper responsible for loading an arbitrary image (via FreeImage)
// and applying it as an SDL window icon with platform-specific handling.
class WindowIconHelper {
public:
    // Attempts to set the icon of the given SDL window using the supplied image path.
    // Returns true if any icon assignment succeeded (platform-specific or SDL fallback).
    static bool SetWindowIcon(SDL_Window* window, const std::string& icon_path);

    // Sets the icon from an already-loaded FreeImage bitmap. The bitmap is not modified.
    static bool SetWindowIconFromBitmap(SDL_Window* window, FIBITMAP* bitmap);

private:
    WindowIconHelper() = delete;
};

#endif // NO_GUI


