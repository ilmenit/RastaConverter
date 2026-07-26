#pragma once

#ifndef NO_GUI

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif

#include <FreeImage.h>

#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif

#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <memory>
#include "gui.h"
#include "LiveStats.h"

#if defined(RASTA_ENABLE_LIVE_UI)
namespace rc_live_ui { class Overlay; }
#endif


class RastaSDL {
private:
    int window_width = 320 * 3;
    int window_height = 480;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    bool frameDirty = false;
    bool abortRequested = false;
    bool screenshotDone = false;
    SDL_Texture* framebufferTexture = nullptr; // retained backbuffer to avoid flicker
    int logical_w = 0;
    int logical_h = 0;
#if defined(RASTA_ENABLE_LIVE_UI)
    std::unique_ptr<rc_live_ui::Overlay> liveOverlay;
#endif
    void EnsureFramebuffer();
    SDL_Surface* FIBitmapToSDLSurface(FIBITMAP* fiBitmap);
    SDL_Surface* FIBitmapLineToSDLSurface(FIBITMAP* fiBitmap, int line_y);
public:
    RastaSDL();
    ~RastaSDL();
    bool Init(std::string command_line, bool enable_live_ui = false);
    void Error(std::string e);
    void DisplayBitmapLine(int x, int y, int line_y, FIBITMAP* fiBitmap);
    void DisplayText(int x, int y, const std::string& text);
    void DisplayBitmap(int x, int y, FIBITMAP* fiBitmap);
    void Present();
    GUI_command NextFrame();
    bool SetIcon(FIBITMAP* bitmap);

    // Additive live-UI surface. No-ops in a build without the live UI, so the
    // converter can call them unconditionally.
    void PublishStats(const LiveStats& stats);
    void PublishImage(GuiImageSlot slot, FIBITMAP* bitmap);
    void PublishDetailsMask(const GuiDetailsMask& mask);
    void PublishDestinationLayer(const GuiDestinationLayer& layer);
    bool TakeMaskStroke(GuiMaskStroke& stroke);
    bool TakeDestinationChanges(GuiMaskStroke& stroke);
    bool CreateBranchOutput(const std::string& input_file,
        std::string& output_file, std::string& error);
    // True when the dashboard owns the window, so the legacy three-image
    // display should not be drawn.
    bool LiveUiActive() const;
    // True when the user chose Abort rather than Stop and save.
    bool AbortRequested() const;

};

#endif // NO_GUI
