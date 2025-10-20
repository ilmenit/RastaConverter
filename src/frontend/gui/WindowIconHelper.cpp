#ifndef NO_GUI

#include "WindowIconHelper.h"
#include "debug_log.h"

#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <sstream>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#undef small // In case of macro conflict
#include <Windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propvarutil.h>
#include <propkey.h>
#include <shlwapi.h>
#include <knownfolders.h>
#endif

#include <SDL.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif
#endif

#include <FreeImage.h>

#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif

#if SDL_VERSION_ATLEAST(2,0,2)
#include <SDL_syswm.h>
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
struct IconHolder {
    HICON bigIcon;
    HICON smallIcon;

    IconHolder() : bigIcon(NULL), smallIcon(NULL) {}

    ~IconHolder() {
        if (bigIcon) DestroyIcon(bigIcon);
        if (smallIcon) DestroyIcon(smallIcon);
    }
};

static SDL_Surface* ScaleSurface(SDL_Surface* src, int targetWidth, int targetHeight)
{
    if (!src || targetWidth <= 0 || targetHeight <= 0) return nullptr;

    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, targetWidth, targetHeight, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!scaled) return nullptr;

    // Simple nearest-neighbor scaling with pre-multiplied alpha
    const float xRatio = static_cast<float>(src->w) / targetWidth;
    const float yRatio = static_cast<float>(src->h) / targetHeight;

    SDL_LockSurface(scaled);
    SDL_LockSurface(src);

    Uint32* dstPixels = static_cast<Uint32*>(scaled->pixels);
    const Uint32* srcPixels = static_cast<const Uint32*>(src->pixels);

    for (int y = 0; y < targetHeight; ++y) {
        for (int x = 0; x < targetWidth; ++x) {
            int srcX = static_cast<int>(x * xRatio);
            int srcY = static_cast<int>(y * yRatio);
            
            if (srcX >= src->w) srcX = src->w - 1;
            if (srcY >= src->h) srcY = src->h - 1;

            const Uint32 srcPixel = srcPixels[(srcY * src->w) + srcX];
            
            Uint8 r, g, b, a;
            SDL_GetRGBA(srcPixel, src->format, &r, &g, &b, &a);

            // Pre-multiply alpha
            r = (r * a) / 255;
            g = (g * a) / 255;
            b = (b * a) / 255;

            dstPixels[(y * targetWidth) + x] = SDL_MapRGBA(scaled->format, r, g, b, a);
        }
    }

    SDL_UnlockSurface(src);
    SDL_UnlockSurface(scaled);

    return scaled;
}

static HICON CreateWin32Icon(SDL_Surface* surface, int targetSize)
{
    if (!surface || targetSize <= 0) return nullptr;

    // Scale surface to target size if needed
    std::unique_ptr<SDL_Surface, SDLSurfaceDeleter> scaledSurface;
    SDL_Surface* workingSurface = surface;
    
    if (surface->w != targetSize || surface->h != targetSize) {
        SDL_Surface* scaled = ScaleSurface(surface, targetSize, targetSize);
        if (!scaled) {
            DBG_PRINT("[ICON] Failed to scale surface to %dx%d", targetSize, targetSize);
            return nullptr;
        }
        scaledSurface.reset(scaled);
        workingSurface = scaled;
    }

    // Create an HBITMAP from the SDL surface
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(BITMAPV5HEADER));
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = workingSurface->w;
    bi.bV5Height = -workingSurface->h; // top-down DIB
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

    const uint8_t* src = static_cast<const uint8_t*>(workingSurface->pixels);
    const int src_pitch = workingSurface->pitch;
    uint8_t* dst = static_cast<uint8_t*>(dibBits);

    for (int y = 0; y < workingSurface->h; ++y)
    {
        memcpy(dst + y * workingSurface->w * 4, src + y * src_pitch, static_cast<size_t>(workingSurface->w) * 4);
    }

    HBITMAP hMask = CreateBitmap(workingSurface->w, workingSurface->h, 1, 1, NULL);
    if (!hMask)
    {
        DBG_PRINT("[ICON] CreateBitmap mask failed (err=%lu)", GetLastError());
        DeleteObject(hBitmap);
        return nullptr;
    }

    ICONINFO iconInfo;
    memset(&iconInfo, 0, sizeof(ICONINFO));
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

    // Create properly sized icons for Windows taskbar
    // ICON_BIG is typically 32x32, ICON_SMALL is 16x16
    HICON bigIcon = CreateWin32Icon(surface, 32);
    if (!bigIcon)
    {
        DBG_PRINT("[ICON] Failed to create 32x32 icon");
        return false;
    }

    HICON smallIcon = CreateWin32Icon(surface, 16);
    if (!smallIcon)
    {
        DBG_PRINT("[ICON] Failed to create 16x16 icon");
        DestroyIcon(bigIcon);
        return false;
    }

    static IconHolder s_icons;

    if (s_icons.bigIcon) {
        DestroyIcon(s_icons.bigIcon);
        s_icons.bigIcon = nullptr;
    }
    if (s_icons.smallIcon) {
        DestroyIcon(s_icons.smallIcon);
        s_icons.smallIcon = nullptr;
    }

    // Set both window and taskbar icons
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)bigIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);

    // Force a taskbar icon refresh
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

    s_icons.bigIcon = bigIcon;
    s_icons.smallIcon = smallIcon;

    DBG_PRINT("[ICON] Successfully set taskbar icons (32x32 and 16x16)");

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

#if defined(_WIN32)

namespace
{

static const uint16_t kIconSizes[] = { 16, 32, 48, 64, 128, 256 };

struct ResourceIconData
{
    BITMAPINFOHEADER header{};
    std::vector<uint8_t> xorBitmap;
    std::vector<uint8_t> andBitmap;
};

struct WindowIconState
{
    std::wstring appId;
    std::wstring iconPath;
};

static std::once_flag g_initFlag;
static std::wstring g_exePath;
static std::wstring g_relaunchCommand;
static std::wstring g_iconDirectory;
static std::wstring g_appIdPrefix = L"RastaConverter.Session.";
static std::atomic<uint64_t> g_iconCounter{0};
static std::mutex g_stateMutex;
static std::unordered_map<HWND, WindowIconState> g_windowStates;

static std::wstring DetermineIconDirectory()
{
    PWSTR knownFolder = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &knownFolder)))
    {
        result = knownFolder;
        CoTaskMemFree(knownFolder);
        if (!result.empty() && result.back() != L'\\')
        {
            result += L'\\';
        }
        result += L"RastaConverter\\Icons";
    }
    else
    {
        result = g_exePath;
        size_t pos = result.find_last_of(L"/\\");
        if (pos != std::wstring::npos)
        {
            result.erase(pos);
        }
        result += L"\\Icons";
    }

    SHCreateDirectoryExW(nullptr, result.c_str(), nullptr);
    return result;
}

static void InitializeIconEnvironment()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        DBG_PRINT("[ICON] CoInitializeEx failed: 0x%lx", hr);
    }

    wchar_t exePathBuffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    g_exePath = exePathBuffer;

    g_relaunchCommand = L"\"";
    g_relaunchCommand += g_exePath;
    g_relaunchCommand += L"\"";

    g_iconDirectory = DetermineIconDirectory();
}

static std::wstring EnsureIconDirectory()
{
    std::call_once(g_initFlag, InitializeIconEnvironment);
    return g_iconDirectory;
}

static ResourceIconData SurfaceToResourceIcon(SDL_Surface* surface)
{
    ResourceIconData data;
    data.header.biSize = sizeof(BITMAPINFOHEADER);
    data.header.biWidth = surface->w;
    data.header.biHeight = surface->h * 2;
    data.header.biPlanes = 1;
    data.header.biBitCount = 32;
    data.header.biCompression = BI_RGB;

    const size_t xorSize = static_cast<size_t>(surface->w) * surface->h * 4;
    data.xorBitmap.resize(xorSize);

    const uint8_t* src = static_cast<const uint8_t*>(surface->pixels);
    for (int y = 0; y < surface->h; ++y)
    {
        const uint8_t* srcRow = src + (surface->h - 1 - y) * surface->pitch;
        uint8_t* dstRow = data.xorBitmap.data() + static_cast<size_t>(y) * surface->w * 4;
        memcpy(dstRow, srcRow, static_cast<size_t>(surface->w) * 4);
    }

    size_t maskStride = ((surface->w + 31) / 32) * 4;
    data.andBitmap.assign(maskStride * surface->h, 0x00);

    data.header.biSizeImage = static_cast<DWORD>(xorSize);

    return data;
}

static bool WriteIconFile(SDL_Surface* sourceSurface, const std::wstring& path)
{
    std::wstring directory = EnsureIconDirectory();
    if (directory.empty())
    {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        DBG_PRINT("[ICON] Failed to open icon file: %ls", path.c_str());
        return false;
    }

    const uint16_t reserved = 0;
    const uint16_t type = 1;

    std::vector<ResourceIconData> images;
    for (uint16_t size : kIconSizes)
    {
        SDL_Surface* resized = ScaleSurface(sourceSurface, size, size);
        if (!resized)
        {
            continue;
        }

        ResourceIconData data = SurfaceToResourceIcon(resized);
        SDL_FreeSurface(resized);
        images.push_back(std::move(data));
    }

    if (images.empty())
    {
        DBG_PRINT("[ICON] No scaled icons produced");
        return false;
    }

    const uint16_t count = static_cast<uint16_t>(images.size());
    out.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
    out.write(reinterpret_cast<const char*>(&type), sizeof(type));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    struct IconDirEntry
    {
        uint8_t width;
        uint8_t height;
        uint8_t colorCount;
        uint8_t reserved;
        uint16_t planes;
        uint16_t bitCount;
        uint32_t bytesInRes;
        uint32_t imageOffset;
    };

    uint32_t offset = 6 + static_cast<uint32_t>(count) * sizeof(IconDirEntry);

    std::vector<IconDirEntry> entries;
    entries.reserve(images.size());

    for (const auto& image : images)
    {
        IconDirEntry entry{};
        uint32_t width = static_cast<uint32_t>(image.header.biWidth);
        uint32_t height = static_cast<uint32_t>(image.header.biHeight / 2);
        entry.width = static_cast<uint8_t>(width == 256 ? 0 : width);
        entry.height = static_cast<uint8_t>(height == 256 ? 0 : height);
        entry.colorCount = 0;
        entry.reserved = 0;
        entry.planes = image.header.biPlanes;
        entry.bitCount = image.header.biBitCount;
        entry.bytesInRes = sizeof(BITMAPINFOHEADER) + static_cast<uint32_t>(image.xorBitmap.size() + image.andBitmap.size());
        entry.imageOffset = offset;

        entries.push_back(entry);
        offset += entry.bytesInRes;
    }

    out.write(reinterpret_cast<const char*>(entries.data()), entries.size() * sizeof(IconDirEntry));

    for (const auto& image : images)
    {
        out.write(reinterpret_cast<const char*>(&image.header), sizeof(BITMAPINFOHEADER));
        out.write(reinterpret_cast<const char*>(image.xorBitmap.data()), image.xorBitmap.size());
        out.write(reinterpret_cast<const char*>(image.andBitmap.data()), image.andBitmap.size());
    }

    return true;
}

static bool SetAppUserModelProperties(HWND hwnd, const std::wstring& appId, const std::wstring& iconPath)
{
    IPropertyStore* propertyStore = nullptr;
    HRESULT hr = SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&propertyStore));
    if (FAILED(hr) || !propertyStore)
    {
        DBG_PRINT("[ICON] SHGetPropertyStoreForWindow failed: 0x%lx", hr);
        return false;
    }

    PROPVARIANT variant;
    PropVariantInit(&variant);

    bool success = true;

    hr = InitPropVariantFromString(appId.c_str(), &variant);
    if (SUCCEEDED(hr))
    {
        hr = propertyStore->SetValue(PKEY_AppUserModel_ID, variant);
    }
    PropVariantClear(&variant);

    if (FAILED(hr))
    {
        DBG_PRINT("[ICON] Failed to set AppUserModelID: 0x%lx", hr);
        success = false;
    }

    if (success)
    {
        hr = InitPropVariantFromString(g_relaunchCommand.c_str(), &variant);
        if (SUCCEEDED(hr))
        {
            hr = propertyStore->SetValue(PKEY_AppUserModel_RelaunchCommand, variant);
        }
        PropVariantClear(&variant);
        if (FAILED(hr))
        {
            DBG_PRINT("[ICON] Failed to set RelaunchCommand: 0x%lx", hr);
            success = false;
        }
    }

    if (success)
    {
        std::wstring iconResource = iconPath + L",0";
        hr = InitPropVariantFromString(iconResource.c_str(), &variant);
        if (SUCCEEDED(hr))
        {
            hr = propertyStore->SetValue(PKEY_AppUserModel_RelaunchIconResource, variant);
        }
        PropVariantClear(&variant);
        if (FAILED(hr))
        {
            DBG_PRINT("[ICON] Failed to set RelaunchIconResource: 0x%lx", hr);
            success = false;
        }
    }

    if (success)
    {
        hr = propertyStore->Commit();
        if (FAILED(hr))
        {
            DBG_PRINT("[ICON] Commit failed: 0x%lx", hr);
            success = false;
        }
    }

    propertyStore->Release();
    return success;
}

static bool UpdateAppUserModelForWindow(SDL_Window* window, SDL_Surface* surface)
{
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo))
    {
        return false;
    }

    HWND hwnd = wmInfo.info.win.window;
    if (!hwnd)
    {
        return false;
    }

    std::wstring directory = EnsureIconDirectory();
    if (directory.empty())
    {
        return false;
    }

    WindowIconState state;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& entry = g_windowStates[hwnd];
        if (entry.appId.empty())
        {
            uint64_t id = ++g_iconCounter;
            entry.appId = g_appIdPrefix + std::to_wstring(id);
            entry.iconPath = directory + L"\\session_" + std::to_wstring(id) + L".ico";
        }
        state = entry;
    }

    if (!WriteIconFile(surface, state.iconPath))
    {
        return false;
    }

    return SetAppUserModelProperties(hwnd, state.appId, state.iconPath);
}

} // namespace

#endif

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
    if (UpdateAppUserModelForWindow(window, surface.get())) success = true;
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
    if (SetDockIconMac(surface.get())) success = true;
#endif

    if (SetSDLWindowIcon(window, surface.get())) success = true;

    return success;
}

#endif // NO_GUI


