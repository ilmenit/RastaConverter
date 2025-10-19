#ifndef NO_GUI

#include "WindowIconHelper.h"
#include "debug_log.h"

#include <SDL.h>
#include <FreeImage.h>

#include <memory>
#include <string>
#include <cstdint>
#include <cstring>

#if SDL_VERSION_ATLEAST(2,0,2)
#include <SDL_syswm.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <CoreGraphics/CoreGraphics.h>
#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif
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
struct IconHolder {
    HICON big = nullptr;
    HICON small = nullptr;

    ~IconHolder() {
        if (big) DestroyIcon(big);
        if (small) DestroyIcon(small);
    }
};

static HICON CreateWin32Icon(SDL_Surface* surface)
{
    if (!surface) return nullptr;

    // Create an HBITMAP from the SDL surface
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = surface->w;
    bi.bV5Height = -surface->h; // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    if (!hdc) return nullptr;

    void* dibBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &dibBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (!hBitmap || !dibBits) {
        if (hBitmap) DeleteObject(hBitmap);
        DBG_PRINT("[ICON] CreateDIBSection failed");
        return nullptr;
    }

    const uint8_t* src = static_cast<const uint8_t*>(surface->pixels);
    const int src_pitch = surface->pitch;
    uint8_t* dst = static_cast<uint8_t*>(dibBits);

    for (int y = 0; y < surface->h; ++y)
    {
        memcpy(dst + y * surface->w * 4, src + y * src_pitch, static_cast<size_t>(surface->w) * 4);
    }

    HBITMAP hMask = CreateBitmap(surface->w, surface->h, 1, 1, NULL);
    if (!hMask)
    {
        DBG_PRINT("[ICON] CreateBitmap mask failed (err=%lu)", GetLastError());
        DeleteObject(hBitmap);
        return nullptr;
    }

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = hBitmap;
    iconInfo.hbmMask = hMask;

    HICON hIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hBitmap);
    DeleteObject(hMask);

    if (!hIcon)
    {
        DBG_PRINT("[ICON] CreateIconIndirect failed (err=%lu)", GetLastError());
    }

    return hIcon;
}

static bool SetTaskbarIconWin32(SDL_Window* window, SDL_Surface* surface)
{
#if SDL_VERSION_ATLEAST(2,0,2)
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo))
        return false;

    HWND hwnd = wmInfo.info.win.window;
    if (!hwnd)
        return false;

    HICON bigIcon = CreateWin32Icon(surface);
    if (!bigIcon)
    {
        return false;
    }

    HICON smallIcon = CopyIcon(bigIcon);
    if (!smallIcon)
    {
        DBG_PRINT("[ICON] CopyIcon failed (err=%lu)", GetLastError());
        DestroyIcon(bigIcon);
        return false;
    }

    static IconHolder s_icons;

    if (s_icons.big) {
        DestroyIcon(s_icons.big);
        s_icons.big = nullptr;
    }
    if (s_icons.small) {
        DestroyIcon(s_icons.small);
        s_icons.small = nullptr;
    }

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)bigIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);

    s_icons.big = bigIcon;
    s_icons.small = smallIcon;

    return true;
#else
    (void)window;
    (void)surface;
    return false;
#endif
}
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
static bool SetDockIconMac(SDL_Surface* surface)
{
    if (!surface) return false;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) return false;

    CGContextRef ctx = CGBitmapContextCreate(surface->pixels, surface->w, surface->h, 8, surface->pitch,
                                             colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(colorSpace);

    if (!ctx) return false;

    CGImageRef imageRef = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);

    if (!imageRef) return false;

    Class NSImageClass = objc_getClass("NSImage");
    if (!NSImageClass) {
        CGImageRelease(imageRef);
        return false;
    }

    SEL allocSel = sel_registerName("alloc");
    SEL initSel = sel_registerName("initWithCGImage:size:");
    SEL releaseSel = sel_registerName("release");

    id nsImage = ((id(*)(Class, SEL))objc_msgSend)(NSImageClass, allocSel);
    if (!nsImage) {
        CGImageRelease(imageRef);
        return false;
    }

    struct CGSize size = CGSizeMake(static_cast<CGFloat>(surface->w), static_cast<CGFloat>(surface->h));
    nsImage = ((id(*)(id, SEL, CGImageRef, CGSize))objc_msgSend)(nsImage, initSel, imageRef, size);
    CGImageRelease(imageRef);
    if (!nsImage) return false;

    Class NSApplicationClass = objc_getClass("NSApplication");
    if (!NSApplicationClass) {
        ((void(*)(id, SEL))objc_msgSend)(nsImage, releaseSel);
        return false;
    }

    SEL sharedAppSel = sel_registerName("sharedApplication");
    SEL setIconSel = sel_registerName("setApplicationIconImage:");
    id sharedApp = ((id(*)(Class, SEL))objc_msgSend)(NSApplicationClass, sharedAppSel);
    if (!sharedApp) {
        ((void(*)(id, SEL))objc_msgSend)(nsImage, releaseSel);
        return false;
    }

    ((void(*)(id, SEL, id))objc_msgSend)(sharedApp, setIconSel, nsImage);
    ((void(*)(id, SEL))objc_msgSend)(nsImage, releaseSel);

    return true;
}
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
    if (SetTaskbarIconWin32(window, surface.get())) success = true;
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
    if (SetDockIconMac(surface.get())) success = true;
#endif

    if (SetSDLWindowIcon(window, surface.get())) success = true;

    return success;
}

#endif // NO_GUI


