#ifndef NO_GUI

#include "WindowIconHelper.h"
#include "debug_log.h"


#include <memory>
#include <string>

#include <SDL.h>

#include <FreeImage.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#include "WindowIconHelper_Mac.h"
#endif

#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif

#if defined(_WIN32)
#include "WindowIconHelper_Win32.h"
#endif


namespace
{

struct FreeImageBitmapDeleter {
    void operator()(FIBITMAP* bmp) const {
        if (bmp) FreeImage_Unload(bmp);
    }
};

struct SDLSurfaceDeleter {
    void operator()(SDL_Surface* surface) const {
        if (surface) SDL_FreeSurface(surface);
    }
};

static std::unique_ptr<SDL_Surface, SDLSurfaceDeleter> ConvertToSDLSurface(FIBITMAP* bitmap)
{
    if (!bitmap) return nullptr;

    const FREE_IMAGE_COLOR_TYPE color_type = FreeImage_GetColorType(bitmap);
    const unsigned bpp = FreeImage_GetBPP(bitmap);
    const bool had_alpha = (color_type == FIC_RGBALPHA);

    std::unique_ptr<FIBITMAP, FreeImageBitmapDeleter> converted_holder;
    FIBITMAP* converted = bitmap;

    if (bpp != 32 || color_type != FIC_RGBALPHA)
    {
        converted = FreeImage_ConvertTo32Bits(bitmap);
        if (!converted) {
            DBG_PRINT("[ICON] Failed to convert bitmap to 32-bit RGBA");
            return nullptr;
        }
        converted_holder.reset(converted);
    }

    const unsigned width = FreeImage_GetWidth(converted);
    const unsigned height = FreeImage_GetHeight(converted);
    const unsigned pitch = FreeImage_GetPitch(converted);
    BYTE* bits = FreeImage_GetBits(converted);

    if (!bits || width == 0 || height == 0 || pitch == 0)
    {
        DBG_PRINT("[ICON] Invalid bitmap data");
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, static_cast<int>(width), static_cast<int>(height), 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surface)
    {
        DBG_PRINT("[ICON] SDL_CreateRGBSurfaceWithFormat failed: %s", SDL_GetError());
        return nullptr;
    }

    Uint8* dstPixels = static_cast<Uint8*>(surface->pixels);
    SDL_PixelFormat* fmt = surface->format;

    for (unsigned y = 0; y < height; ++y)
    {
        const BYTE* srcLine = FreeImage_GetScanLine(converted, y);
        if (!srcLine) continue;
        Uint32* dstLine = reinterpret_cast<Uint32*>(dstPixels + y * surface->pitch);
        for (unsigned x = 0; x < width; ++x)
        {
            const BYTE* srcPixel = srcLine + x * 4;
            BYTE r = srcPixel[FI_RGBA_RED];
            BYTE g = srcPixel[FI_RGBA_GREEN];
            BYTE b = srcPixel[FI_RGBA_BLUE];
            BYTE a = had_alpha ? srcPixel[FI_RGBA_ALPHA] : 0xFF;
            dstLine[x] = SDL_MapRGBA(fmt, r, g, b, a);
        }
    }

    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceRLE(surface, 0);

    return std::unique_ptr<SDL_Surface, SDLSurfaceDeleter>(surface);
}

static std::unique_ptr<FIBITMAP, FreeImageBitmapDeleter> LoadFreeImage(const std::string& path)
{
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(path.c_str(), 0);
    if (fif == FIF_UNKNOWN)
        fif = FreeImage_GetFIFFromFilename(path.c_str());

    if (fif == FIF_UNKNOWN)
    {
        DBG_PRINT("[ICON] Unknown image format: %s", path.c_str());
        return nullptr;
    }

    FIBITMAP* bitmap = FreeImage_Load(fif, path.c_str());
    if (!bitmap)
    {
        DBG_PRINT("[ICON] FreeImage_Load failed for: %s", path.c_str());
        return nullptr;
    }

    return std::unique_ptr<FIBITMAP, FreeImageBitmapDeleter>(bitmap);
}

#if defined(_WIN32)
// Windows-specific icon helpers are implemented in WindowIconHelper_Win32.cpp
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
// macOS dock icon helper implemented in WindowIconHelper_Mac.cpp
#endif

static bool SetSDLWindowIcon(SDL_Window* window, SDL_Surface* surface)
{
    if (!window || !surface) return false;
    SDL_SetWindowIcon(window, surface);
    return true;
}

} // namespace

bool WindowIconHelper::SetWindowIcon(SDL_Window* window, const std::string& icon_path)
{
    if (!window) {
        DBG_PRINT("[ICON] Cannot set icon: window is null");
        return false;
    }

    auto bitmap = LoadFreeImage(icon_path);
    if (!bitmap) return false;

    return SetWindowIconFromBitmap(window, bitmap.get());
}

bool WindowIconHelper::SetWindowIconFromBitmap(SDL_Window* window, FIBITMAP* bitmap)
{
    if (!window || !bitmap) return false;

    auto surface = ConvertToSDLSurface(bitmap);
    if (!surface) return false;

#if defined(_DEBUG) || !defined(NDEBUG)
    {
        DBG_PRINT("[ICON] Saving icon to icon.bmp");
        if (SDL_SaveBMP(surface.get(), "icon.bmp") != 0)
        {
            DBG_PRINT("[ICON] SDL_SaveBMP failed: %s", SDL_GetError());
        }
    }
#endif
    bool success = false;

#if defined(_WIN32)
    if (WindowIconWin32_SetTaskbarIcon(window, surface.get())) success = true;
    if (WindowIconWin32_UpdateAppUserModel(window, surface.get())) success = true;
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
    if (WindowIconMac_SetDockIcon(surface.get())) success = true;
#endif

    if (SetSDLWindowIcon(window, surface.get())) success = true;

    return success;
}

#endif // NO_GUI


