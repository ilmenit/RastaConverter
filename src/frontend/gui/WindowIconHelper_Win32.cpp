#include "WindowIconHelper_Win32.h"

#ifndef NO_GUI

#if defined(_WIN32)

#define NOMINMAX
#undef small

#include <Windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <Propkey.h>
#include <Propvarutil.h>
#include <psapi.h>

#include <SDL.h>
#if SDL_VERSION_ATLEAST(2,0,2)
#include <SDL_syswm.h>
#endif

#include <vector>
#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <atomic>

#include <FreeImage.h>

#include "debug_log.h"

namespace
{
// TODO: deduplicate shared logic with the cross-platform helper if practical

struct SDLSurfaceDeleter {
    void operator()(SDL_Surface* surface) const {
        if (surface) SDL_FreeSurface(surface);
    }
};

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

    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(BITMAPV5HEADER));
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = workingSurface->w;
    bi.bV5Height = -workingSurface->h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    bi.bV5CSType = LCS_sRGB;
    bi.bV5Intent = LCS_GM_IMAGES;

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
    const int srcPitch = workingSurface->pitch;
    uint32_t* dst = static_cast<uint32_t*>(dibBits);

    for (int y = 0; y < workingSurface->h; ++y)
    {
        const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(src + y * srcPitch);
        uint32_t* dstRow = dst + y * workingSurface->w;

        for (int x = 0; x < workingSurface->w; ++x)
        {
            Uint8 r, g, b, a;
            SDL_GetRGBA(srcRow[x], workingSurface->format, &r, &g, &b, &a);

            if (a < 255)
            {
                r = static_cast<Uint8>((r * a + 127) / 255);
                g = static_cast<Uint8>((g * a + 127) / 255);
                b = static_cast<Uint8>((b * a + 127) / 255);
            }

            dstRow[x] = (static_cast<uint32_t>(a) << 24) |
                        (static_cast<uint32_t>(r) << 16) |
                        (static_cast<uint32_t>(g) << 8)  |
                        (static_cast<uint32_t>(b));
        }
    }

    const size_t maskStride = ((workingSurface->w + 31) / 32) * 4;
    const size_t maskSize = maskStride * workingSurface->h;
    std::vector<uint8_t> maskData(maskSize, 0x00);
    HBITMAP hMask = CreateBitmap(workingSurface->w, workingSurface->h, 1, 1, maskData.data());
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

static bool SetWindowIcons(HWND hwnd, SDL_Surface* surface)
{
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

    SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));

    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

    s_icons.bigIcon = bigIcon;
    s_icons.smallIcon = smallIcon;

    DBG_PRINT("[ICON] Successfully set taskbar icons (32x32 and 16x16)");

    return true;
}

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
        const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(src + (surface->h - 1 - y) * surface->pitch);
        uint32_t* dstRow = reinterpret_cast<uint32_t*>(data.xorBitmap.data() + static_cast<size_t>(y) * surface->w * 4);

        for (int x = 0; x < surface->w; ++x)
        {
            Uint8 r, g, b, a;
            SDL_GetRGBA(srcRow[x], surface->format, &r, &g, &b, &a);

            if (a < 255)
            {
                r = static_cast<Uint8>((r * a + 127) / 255);
                g = static_cast<Uint8>((g * a + 127) / 255);
                b = static_cast<Uint8>((b * a + 127) / 255);
            }

            dstRow[x] = (static_cast<uint32_t>(a) << 24) |
                        (static_cast<uint32_t>(r) << 16) |
                        (static_cast<uint32_t>(g) << 8)  |
                        (static_cast<uint32_t>(b));
        }
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

} // namespace

bool WindowIconWin32_SetTaskbarIcon(SDL_Window* window, SDL_Surface* surface)
{
#if SDL_VERSION_ATLEAST(2,0,2)
    if (!window || !surface) return false;

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo))
        return false;

    HWND hwnd = wmInfo.info.win.window;
    if (!hwnd)
        return false;

    return SetWindowIcons(hwnd, surface);
#else
    (void)window;
    (void)surface;
    return false;
#endif
}

bool WindowIconWin32_UpdateAppUserModel(SDL_Window* window, SDL_Surface* surface)
{
    if (!window || !surface) return false;

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

#endif // defined(_WIN32)

#endif // NO_GUI


