#ifndef NO_GUI
#include "RastaSDL.h"
#include "debug_log.h"
#include "WindowIconHelper.h"
#if defined(RASTA_ENABLE_LIVE_UI)
#include "live_ui/RecentRuns.h"
#endif
#include <iostream>
#include <time.h>
#if defined(RASTA_ENABLE_LIVE_UI)
#include "live_ui/LiveUI.h"
#endif

using namespace std;

extern bool quiet;

RastaSDL::RastaSDL() = default;

RastaSDL::~RastaSDL()
{
#if defined(RASTA_ENABLE_LIVE_UI)
	liveOverlay.reset();
#endif
	if (framebufferTexture) SDL_DestroyTexture(framebufferTexture);
	if (font) TTF_CloseFont(font);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window) SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();
}

bool RastaSDL::Init(std::string command_line, bool enable_live_ui)
{
	DBG_PRINT("[SDL] Init start. cmdline='%s'", command_line.c_str());
	// A GUI build is also the normal command-line executable. /quiet must be
	// genuinely headless: macOS has no SDL "offscreen" video backend, and a
	// batch conversion has no reason to create a window, renderer or font.
	if (quiet) {
		headless = true;
		DBG_PRINT("[SDL] Quiet conversion: skipping video initialization");
		return true;
	}
	// The program handles SIGINT/SIGTERM itself, turning them into a save-and-
	// stop. SDL would otherwise install its own handlers and convert them into
	// a quit event that only some of the loops read.
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::cerr << "SDL_Init: " << SDL_GetError() << std::endl;
		DBG_PRINT("[SDL] SDL_Init failed: %s", SDL_GetError());
		return false;
	}

	if (enable_live_ui) {
		// The dashboard lays itself out; give it a workable window rather than
		// the legacy three-thumbnail strip.
		window_width = 1420;
		window_height = 900;
	}
	std::string title = "Rasta Converter " + command_line;
	window = SDL_CreateWindow(title.c_str(), window_width, window_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!window) {
		DBG_PRINT("[SDL] SDL_CreateWindow failed: %s", SDL_GetError());
		return false;
	}
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	// Prefer accelerated renderer with vsync; gracefully fall back
#ifdef __linux__
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
	renderer = SDL_CreateRenderer(window, nullptr);
	if (renderer) SDL_SetRenderVSync(renderer, 1);
	if (!renderer) renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
	if (!renderer) {
		DBG_PRINT("[SDL] SDL_CreateRenderer failed: %s", SDL_GetError());
		return false;
	}
	DBG_PRINT("[SDL] Window=%p Renderer=%p", (void*)window, (void*)renderer);

	// Set up scaling behavior so content renders at logical size and SDL handles stretching.
	// The live dashboard is resolution-independent and must not be stretched.
	if (!enable_live_ui)
		SDL_SetRenderLogicalPresentation(renderer, window_width, window_height, SDL_LOGICAL_PRESENTATION_STRETCH);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	// Create retained framebuffer texture that persists across presents
	// This avoids relying on undefined backbuffer contents on some Linux drivers
	int lw, lh; SDL_RendererLogicalPresentation mode; SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
	if (lw <= 0 || lh <= 0) { lw = window_width; lh = window_height; }
	if (!framebufferTexture) {
		framebufferTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, lw, lh);
		if (framebufferTexture) {
			SDL_SetTextureBlendMode(framebufferTexture, SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(framebufferTexture, SDL_SCALEMODE_NEAREST);
			SDL_SetRenderTarget(renderer, framebufferTexture);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderTarget(renderer, NULL);
		}
	}

	// load font
	if (!TTF_Init()) {
		DBG_PRINT("[SDL] TTF_Init failed: %s", SDL_GetError());
		return false;
	}
	const char* base_path = SDL_GetBasePath();
	string font_path = base_path ? base_path : "";
	font_path += "clacon2.ttf";

	font = TTF_OpenFont(font_path.c_str(), 16);
	DBG_PRINT("[SDL] TTF path '%s' -> font=%p", font_path.c_str(), (void*)font);
	if (!font) {
		std::cerr << "SDL_Init: Cannot load font clacon2.ttf" << std::endl;
		string error_text = string("TTF_OpenFont: ") + SDL_GetError();
		std::cerr << error_text << std::endl;
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", error_text.c_str(), NULL);
		DBG_PRINT("[SDL] TTF_OpenFont failed: %s", SDL_GetError());
		return false;
	}

#if defined(RASTA_ENABLE_LIVE_UI)
	if (enable_live_ui) {
		liveOverlay = std::make_unique<rc_live_ui::Overlay>();
		if (!liveOverlay->Initialize(window, renderer)) {
			// Silently falling back left the user staring at the legacy
			// three-blit display with no idea why the dashboard vanished.
			std::cerr << "Warning: the live dashboard could not start ("
				<< SDL_GetError() << "); falling back to the legacy display."
				<< std::endl;
			liveOverlay.reset();
		}
	}
#else
	(void)enable_live_ui;
#endif

	DBG_PRINT("[SDL] Init success");
	return true;
}

void RastaSDL::DisplayText(int x, int y, const std::string& text)
{
	static SDL_Color textColor = { 255, 255, 255, 255 };
	static SDL_Color backgroundColor = { 0, 0, 0, 255 }; // Opaque black clears the previous text.

	// Create a surface from the string with a solid background
	if (!font) { DBG_PRINT("[SDL] DisplayText called with null font"); return; }
	SDL_Surface* textSurface = TTF_RenderText_Shaded(font, text.c_str(), 0, textColor, backgroundColor);
	if (textSurface == nullptr)
	{
		SDL_Log("Unable to create text surface: %s\n", SDL_GetError());
		return;
	}

	// Create a texture from the surface
	SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
	if (textTexture == nullptr)
	{
		SDL_Log("Unable to create text texture: %s\n", SDL_GetError());
		SDL_DestroySurface(textSurface);
		return;
	}
	// SDL3 enables blending for textures with alpha. Shaded text is used as an
	// opaque character cell here, so copying it must replace the previous value.
	SDL_SetTextureBlendMode(textTexture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(textTexture, SDL_SCALEMODE_NEAREST);

	// Set the position and size for the text
	SDL_FRect textRect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(textSurface->w), static_cast<float>(textSurface->h) };

	// Copy the texture to the renderer at the designated position
	if (framebufferTexture) SDL_SetRenderTarget(renderer, framebufferTexture);
	SDL_RenderTexture(renderer, textTexture, NULL, &textRect);
	if (framebufferTexture) SDL_SetRenderTarget(renderer, NULL);
	frameDirty = true;

	// Clean up the surface and texture
	SDL_DestroyTexture(textTexture);
	SDL_DestroySurface(textSurface);
}

void RastaSDL::Error(std::string e)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", e.c_str(), NULL);
	SDL_Quit();
}

// Function to convert a single line of FIBITMAP to SDL_Surface
SDL_Surface* RastaSDL::FIBitmapLineToSDLSurface(FIBITMAP* fiBitmap, int line_y) {
	int width = FreeImage_GetWidth(fiBitmap);
	int bpp = FreeImage_GetBPP(fiBitmap) / 8; // Bytes per pixel
	BYTE* bits = FreeImage_GetScanLine(fiBitmap, line_y);

	// Create an SDL_Surface for the line
	SDL_Surface* lineSurface = SDL_CreateSurface(width, 1, SDL_PIXELFORMAT_BGR24);

	if (lineSurface == NULL) {
		// Handle error
		return NULL;
	}

	// Copy the line into the surface
	memcpy(lineSurface->pixels, bits, width * bpp);

	return lineSurface;
}

// Function to display a single line of FIBITMAP on SDL_Window at (x, y)
void RastaSDL::DisplayBitmapLine(int x, int y, int line_y, FIBITMAP* fiBitmap) {
	if (headless)
		return;
	SDL_Surface* lineSurface = FIBitmapLineToSDLSurface(fiBitmap, line_y);
	if (lineSurface == NULL) {
		// Handle error
		return;
	}

	SDL_Texture* lineTexture = SDL_CreateTextureFromSurface(renderer, lineSurface);
	if (!lineTexture) {
		SDL_DestroySurface(lineSurface);
		return;
	}
	SDL_SetTextureScaleMode(lineTexture, SDL_SCALEMODE_NEAREST);

	// Set the position and size for the line
	SDL_FRect destRect = { static_cast<float>(x * 2), static_cast<float>(y), static_cast<float>(lineSurface->w * 2), 1.0f };
	if (framebufferTexture) SDL_SetRenderTarget(renderer, framebufferTexture);
	SDL_RenderTexture(renderer, lineTexture, NULL, &destRect);
	if (framebufferTexture) SDL_SetRenderTarget(renderer, NULL);
	frameDirty = true;

	// Clean up
	SDL_DestroyTexture(lineTexture);
	SDL_DestroySurface(lineSurface);
}

// Function to convert FIBITMAP to SDL_Surface
SDL_Surface* RastaSDL::FIBitmapToSDLSurface(FIBITMAP* fiBitmap) {
	int width = FreeImage_GetWidth(fiBitmap);
	int height = FreeImage_GetHeight(fiBitmap);
	int pitch = FreeImage_GetPitch(fiBitmap);
	int bpp = FreeImage_GetBPP(fiBitmap);

	// For a 24-bit FIBITMAP, the format is typically BGR
	SDL_Surface* surface = SDL_CreateSurfaceFrom(
		width, height, SDL_PIXELFORMAT_BGR24, FreeImage_GetBits(fiBitmap), pitch);

	return surface;
}

// Function to display FIBITMAP on SDL_Window at (x, y)
void RastaSDL::DisplayBitmap(int x, int y, FIBITMAP* fiBitmap)
{
	if (headless)
		return;
	SDL_Surface* surface = FIBitmapToSDLSurface(fiBitmap);
	if (!surface) return;
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (!texture) {
		SDL_DestroySurface(surface);
		return;
	}
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

	// Set the position and size for the stretched bitmap
	SDL_FRect destRect = { static_cast<float>(x * 2), static_cast<float>(y), static_cast<float>(surface->w * 2), static_cast<float>(surface->h) }; // Double the width
	if (framebufferTexture) SDL_SetRenderTarget(renderer, framebufferTexture);
	SDL_RenderTexture(renderer, texture, NULL, &destRect);
	if (framebufferTexture) SDL_SetRenderTarget(renderer, NULL);
	frameDirty = true;
	// Clean up
	SDL_DestroyTexture(texture);
	SDL_DestroySurface(surface);

//	SDL_RenderPresent(renderer);
}

void RastaSDL::PublishStats(const LiveStats& stats)
{
#if defined(RASTA_ENABLE_LIVE_UI)
	if (liveOverlay) {
		liveOverlay->PublishStats(stats);
		frameDirty = true;
	}
#else
	(void)stats;
#endif
}

void RastaSDL::PublishImage(GuiImageSlot slot, FIBITMAP* bitmap)
{
#if defined(RASTA_ENABLE_LIVE_UI)
	if (liveOverlay) {
		rc_live_ui::ImageSlot mapped = rc_live_ui::ImageSlot::Output;
		switch (slot) {
		case GuiImageSlot::Source: mapped = rc_live_ui::ImageSlot::Source; break;
		case GuiImageSlot::Target: mapped = rc_live_ui::ImageSlot::Target; break;
		case GuiImageSlot::Output: mapped = rc_live_ui::ImageSlot::Output; break;
		}
		liveOverlay->PublishBitmap(mapped, bitmap);
		frameDirty = true;
	}
#else
	(void)slot;
	(void)bitmap;
#endif
}

bool RastaSDL::AbortRequested() const
{
	return abortRequested;
}

bool RastaSDL::TakeEditorApply(GuiEditorApply& request)
{
#if defined(RASTA_ENABLE_LIVE_UI)
	return liveOverlay && liveOverlay->TakeEditorApply(request);
#else
	(void)request;
	return false;
#endif
}

bool RastaSDL::EditorWantsDestination() const
{
#if defined(RASTA_ENABLE_LIVE_UI)
	return liveOverlay && liveOverlay->EditorWantsDestination();
#else
	return false;
#endif
}

bool RastaSDL::CreateBranchOutput(const std::string& input_file,
	std::string& output_file, std::string& error)
{
#if defined(RASTA_ENABLE_LIVE_UI)
	output_file = rc_live_ui::AllocateRunOutputPath(input_file);
	if (!rc_live_ui::CreateRunFolder(output_file, &error))
		return false;
	const size_t slash = output_file.find_last_of("/\\");
	rc_live_ui::RegisterRecentRun(slash == std::string::npos
		? std::string(".") : output_file.substr(0, slash));
	return true;
#else
	(void)input_file;
	(void)output_file;
	error = "Branching requires the live UI build.";
	return false;
#endif
}

void RastaSDL::PublishDetailsMask(const GuiDetailsMask& mask)
{
#if defined(RASTA_ENABLE_LIVE_UI)
	if (liveOverlay) {
		liveOverlay->PublishDetailsMask(mask.values, mask.editable_values,
			mask.width, mask.height, mask.active);
		frameDirty = true;
	}
#else
	(void)mask;
#endif
}

void RastaSDL::PublishDestinationLayer(const GuiDestinationLayer& layer)
{
#if defined(RASTA_ENABLE_LIVE_UI)
	if (liveOverlay)
		liveOverlay->PublishDestinationLayer(layer.palette_indices,
			layer.width, layer.height);
#else
	(void)layer;
#endif
}

bool RastaSDL::LiveUiActive() const
{
#if defined(RASTA_ENABLE_LIVE_UI)
	return liveOverlay != nullptr;
#else
	return false;
#endif
}

bool RastaSDL::SetIcon(FIBITMAP* bitmap)
{
	if (headless)
		return true;
	return WindowIconHelper::SetWindowIconFromBitmap(window, bitmap);
}

void RastaSDL::Present()
{
	if (headless)
		return;
#if defined(RASTA_ENABLE_LIVE_UI)
	if (liveOverlay) {
		// The dashboard draws the whole window, including the images, so the
		// legacy three-blit framebuffer is not composited at all.
		SDL_SetRenderTarget(renderer, NULL);
		SDL_SetRenderViewport(renderer, NULL);
		SDL_SetRenderDrawColor(renderer, 19, 21, 26, 255);
		SDL_RenderClear(renderer);
		liveOverlay->Render();

		// Development hook, matching the Setup screen's: render a fixed number
		// of frames offscreen, save the window and stop.
		static const char* const shot_path = SDL_getenv("RASTA_RUN_SCREENSHOT");
		if (shot_path != nullptr) {
			static int frames_left = SDL_getenv("RASTA_RUN_SCREENSHOT_FRAMES") != nullptr
				? SDL_atoi(SDL_getenv("RASTA_RUN_SCREENSHOT_FRAMES")) : 400;
			if (frames_left > 0 && --frames_left == 0) {
				if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
					SDL_SaveBMP(shot, shot_path);
					SDL_DestroySurface(shot);
				}
				screenshotDone = true;
			}
		}

		SDL_RenderPresent(renderer);
		frameDirty = false;
		return;
	}
#endif
	if (frameDirty) {
		if (framebufferTexture) {
			SDL_SetRenderTarget(renderer, NULL);
			int lw, lh; SDL_RendererLogicalPresentation mode; SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
			SDL_FRect dst = { 0.0f, 0.0f, static_cast<float>(lw), static_cast<float>(lh) };
			if (dst.w == 0 || dst.h == 0) { dst.w = window_width; dst.h = window_height; }
			SDL_RenderTexture(renderer, framebufferTexture, NULL, &dst);
		}
#if defined(RASTA_ENABLE_LIVE_UI)
		if (liveOverlay) liveOverlay->Render();
#endif
		SDL_RenderPresent(renderer);
		frameDirty = false;
	}
}

void Wait(int t)
{
	unsigned b;
	unsigned a = (unsigned)time(NULL);
	while (a == (unsigned)time(NULL));
	if (t == 2)
	{
		b = (unsigned)time(NULL);
		while (b == (unsigned)time(NULL));
	}
}


GUI_command RastaSDL::NextFrame()
{
	if (headless)
		return GUI_command::CONTINUE;
	SDL_Event e;

	while (SDL_PollEvent(&e) > 0)
	{
#if defined(RASTA_ENABLE_LIVE_UI)
		if (liveOverlay)
			liveOverlay->ProcessEvent(e);
#endif
		switch (e.type)
		{
		case SDL_EVENT_QUIT:
			{
				const SDL_MessageBoxButtonData buttons[] = {
					{ /* .flags, .buttonid, .text */        0, 0, "No" },
					{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
				};
				const SDL_MessageBoxColorScheme colorScheme = {
					{ /* .colors (.r, .g, .b) */
						/* [SDL_MESSAGEBOX_COLOR_BACKGROUND] */
						{ 255,   0,   0 },
						/* [SDL_MESSAGEBOX_COLOR_TEXT] */
						{   0, 255,   0 },
						/* [SDL_MESSAGEBOX_COLOR_BUTTON_BORDER] */
						{ 255, 255,   0 },
						/* [SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND] */
						{   0,   0, 255 },
						/* [SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED] */
						{ 255,   0, 255 }
					}
				};
				const SDL_MessageBoxData messageboxdata = {
					SDL_MESSAGEBOX_INFORMATION, /* .flags */
					NULL, /* .window */
					"Rasta Converter", /* .title */
					"Do you really want to quit?", /* .message */
					SDL_arraysize(buttons), /* .numbuttons */
					buttons, /* .buttons */
					&colorScheme /* .colorScheme */
				};
				int buttonid;
				if (!SDL_ShowMessageBox(&messageboxdata, &buttonid)) {
					SDL_Log("error displaying message box");
					return GUI_command::STOP;
				}
				if (buttonid == 1) {
					return GUI_command::STOP;
				}
				return GUI_command::CONTINUE;
			}
		case SDL_EVENT_KEY_DOWN:
#if defined(RASTA_ENABLE_LIVE_UI)
			if (liveOverlay && liveOverlay->WantsKeyboard())
				break;
#endif
			if (e.key.repeat == 0)
			{
				switch (e.key.key)
				{
				case SDLK_S:
				case SDLK_D:
					return GUI_command::SAVE;
				case SDLK_A:
					return GUI_command::SHOW_A; // show frame A
				case SDLK_B:
					return GUI_command::SHOW_B; // show frame B
				case SDLK_M:
					return GUI_command::SHOW_MIX; // show blended
				case SDLK_ESCAPE:
					const SDL_MessageBoxButtonData buttons[] = {
						{ /* .flags, .buttonid, .text */        0, 0, "No" },
						{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
					};
					const SDL_MessageBoxColorScheme colorScheme = {
						{ /* .colors (.r, .g, .b) */
							/* [SDL_MESSAGEBOX_COLOR_BACKGROUND] */
							{ 255,   0,   0 },
							/* [SDL_MESSAGEBOX_COLOR_TEXT] */
							{   0, 255,   0 },
							/* [SDL_MESSAGEBOX_COLOR_BUTTON_BORDER] */
							{ 255, 255,   0 },
							/* [SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND] */
							{   0,   0, 255 },
							/* [SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED] */
							{ 255,   0, 255 }
						}
					};
					const SDL_MessageBoxData messageboxdata = {
						SDL_MESSAGEBOX_INFORMATION, /* .flags */
						NULL, /* .window */
						"Rasta Converter", /* .title */
						"Do you really want to quit?", /* .message */
						SDL_arraysize(buttons), /* .numbuttons */
						buttons, /* .buttons */
						&colorScheme /* .colorScheme */
					};
					int buttonid;
					if (!SDL_ShowMessageBox(&messageboxdata, &buttonid)) {
						SDL_Log("error displaying message box");
						return GUI_command::STOP;
					}
					if (buttonid == 1) {
						return GUI_command::STOP;
					}
					return GUI_command::CONTINUE;
				}
			}
			break;
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_EXPOSED:
			{
				// On resize, clear current backbuffer and request a redraw; scaling handled by logical size
				SDL_SetRenderViewport(renderer, NULL);
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
				SDL_RenderClear(renderer);
				
				// Get new logical size after resize
				int lw, lh;
				SDL_RendererLogicalPresentation mode;
				SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
				if (lw <= 0 || lh <= 0) { lw = window_width; lh = window_height; }
				
				// Check if framebuffer texture needs to be recreated (size changed)
				int tex_w = 0, tex_h = 0;
				if (framebufferTexture) {
					float tex_wf = 0.0f, tex_hf = 0.0f;
					SDL_GetTextureSize(framebufferTexture, &tex_wf, &tex_hf);
					tex_w = static_cast<int>(tex_wf);
					tex_h = static_cast<int>(tex_hf);
				}
				
				// Recreate framebuffer texture if size changed or doesn't exist
				if (!framebufferTexture || tex_w != lw || tex_h != lh) {
					if (framebufferTexture) {
						SDL_DestroyTexture(framebufferTexture);
						framebufferTexture = nullptr;
					}
					framebufferTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, lw, lh);
					if (framebufferTexture) {
						SDL_SetTextureBlendMode(framebufferTexture, SDL_BLENDMODE_NONE);
						SDL_SetTextureScaleMode(framebufferTexture, SDL_SCALEMODE_NEAREST);
					}
				}
				
				// Clear the framebuffer texture (the actual rendering target)
				if (framebufferTexture) {
					SDL_SetRenderTarget(renderer, framebufferTexture);
					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
					SDL_RenderClear(renderer);
					SDL_SetRenderTarget(renderer, NULL);
				}
				
				return GUI_command::REDRAW;
			}
		}
	}
#if defined(RASTA_ENABLE_LIVE_UI)
	if (liveOverlay) {
		// The dashboard repaints every frame so its chart and timers stay live.
		Present();
		if (screenshotDone)
			return GUI_command::STOP;
		switch (liveOverlay->TakeCommand())
		{
		case LiveCommand::Save:        return GUI_command::SAVE;
		case LiveCommand::StopAndSave: return GUI_command::STOP;
		case LiveCommand::Abort:
			abortRequested = true;
			return GUI_command::STOP;
		case LiveCommand::EditorBegin:   return GUI_command::EDITOR_BEGIN;
		case LiveCommand::EditorApply:   return GUI_command::EDITOR_APPLY;
		case LiveCommand::EditorDiscard: return GUI_command::EDITOR_DISCARD;
		case LiveCommand::ShowA:       return GUI_command::SHOW_A;
		case LiveCommand::ShowB:       return GUI_command::SHOW_B;
		case LiveCommand::ShowMix:     return GUI_command::SHOW_MIX;
		case LiveCommand::None:
		default:                       break;
		}
		return GUI_command::CONTINUE;
	}
#endif
	// Only present if something was drawn this frame
	if (frameDirty) {
		if (framebufferTexture) {
			SDL_SetRenderTarget(renderer, NULL);
			int lw, lh; SDL_RendererLogicalPresentation mode; SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
			SDL_FRect dst = { 0.0f, 0.0f, static_cast<float>(lw), static_cast<float>(lh) };
			if (dst.w == 0 || dst.h == 0) { dst.w = window_width; dst.h = window_height; }
			SDL_RenderTexture(renderer, framebufferTexture, NULL, &dst);
		}
		SDL_RenderPresent(renderer);
		frameDirty = false;
	}
	return GUI_command::CONTINUE;
}

#endif
