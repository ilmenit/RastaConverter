#ifndef NO_GUI
#include "RastaSDL.h"
#include "debug_log.h"
#include <iostream>
#include <time.h>

using namespace std;

bool RastaSDL::Init(std::string command_line)
{
	DBG_PRINT("[SDL] Init start. cmdline='%s'", command_line.c_str());
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		std::cerr << "SDL_Init: " << SDL_GetError() << std::endl;
		DBG_PRINT("[SDL] SDL_Init failed: %s", SDL_GetError());
		return false;
	}

	std::string title = "Rasta Converter " + command_line;
	window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
	renderer = SDL_CreateRenderer(window, -1, 0);
	DBG_PRINT("[SDL] Window=%p Renderer=%p", (void*)window, (void*)renderer);

	// Set up scaling behavior so content renders at logical size and SDL handles stretching
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	SDL_RenderSetLogicalSize(renderer, window_width, window_height);

	// load font
	TTF_Init();
	string font_path = SDL_GetBasePath();
	font_path += "clacon2.ttf";

	font = TTF_OpenFont(font_path.c_str(), 16);
	DBG_PRINT("[SDL] TTF path '%s' -> font=%p", font_path.c_str(), (void*)font);
	if (!font) {
		std::cerr << "SDL_Init: Cannot load font clacon2.ttf" << std::endl;
		string error_text = string("TTF_OpenFont: ") + TTF_GetError();
		std::cerr << error_text << std::endl;
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", error_text.c_str(), NULL);
		DBG_PRINT("[SDL] TTF_OpenFont failed: %s", TTF_GetError());
		return false;
	}

	DBG_PRINT("[SDL] Init success");
	return true;
}

void RastaSDL::DisplayText(int x, int y, const std::string& text)
{
	static SDL_Color textColor = { 255, 255, 255 };
	static SDL_Color backgroundColor = { 0, 0, 0 }; // Black background color

	// Create a surface from the string with a solid background
	if (!font) { DBG_PRINT("[SDL] DisplayText called with null font"); return; }
	SDL_Surface* textSurface = TTF_RenderText_Shaded(font, text.c_str(), textColor, backgroundColor);
	if (textSurface == nullptr)
	{
		SDL_Log("Unable to create text surface: %s\n", TTF_GetError());
		return;
	}

	// Create a texture from the surface
	SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
	if (textTexture == nullptr)
	{
		SDL_Log("Unable to create text texture: %s\n", SDL_GetError());
		SDL_FreeSurface(textSurface);
		return;
	}

	// Set the position and size for the text
	SDL_Rect textRect = { x, y, textSurface->w, textSurface->h };

	// Copy the texture to the renderer at the designated position
	SDL_RenderCopy(renderer, textTexture, NULL, &textRect);

	// Clean up the surface and texture
	SDL_DestroyTexture(textTexture);
	SDL_FreeSurface(textSurface);
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
	SDL_Surface* lineSurface = SDL_CreateRGBSurfaceWithFormat(0, width, 1, bpp * 8, SDL_PIXELFORMAT_BGR24);

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
	SDL_Surface* lineSurface = FIBitmapLineToSDLSurface(fiBitmap, line_y);
	if (lineSurface == NULL) {
		// Handle error
		return;
	}

	SDL_Texture* lineTexture = SDL_CreateTextureFromSurface(renderer, lineSurface);

	// Set the position and size for the line
	SDL_Rect destRect = { x*2, y, lineSurface->w*2, 1 };
	SDL_RenderCopy(renderer, lineTexture, NULL, &destRect);

	// Clean up
	SDL_DestroyTexture(lineTexture);
	SDL_FreeSurface(lineSurface);
}

// Function to convert FIBITMAP to SDL_Surface
SDL_Surface* RastaSDL::FIBitmapToSDLSurface(FIBITMAP* fiBitmap) {
	int width = FreeImage_GetWidth(fiBitmap);
	int height = FreeImage_GetHeight(fiBitmap);
	int pitch = FreeImage_GetPitch(fiBitmap);
	int bpp = FreeImage_GetBPP(fiBitmap);

	// For a 24-bit FIBITMAP, the format is typically BGR
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
		FreeImage_GetBits(fiBitmap), width, height, bpp, pitch, SDL_PIXELFORMAT_BGR24
	);

	return surface;
}

// Function to display FIBITMAP on SDL_Window at (x, y)
void RastaSDL::DisplayBitmap(int x, int y, FIBITMAP* fiBitmap)
{
	SDL_Surface* surface = FIBitmapToSDLSurface(fiBitmap);
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

	// Set the position and size for the stretched bitmap
	SDL_Rect destRect = { x*2, y, surface->w * 2, surface->h }; // Double the width
	SDL_RenderCopy(renderer, texture, NULL, &destRect);
	// Clean up
	SDL_DestroyTexture(texture);
	SDL_FreeSurface(surface);

//	SDL_RenderPresent(renderer);
}

void RastaSDL::Present()
{
	SDL_RenderPresent(renderer);
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
	SDL_Event e;

	while (SDL_PollEvent(&e) > 0)
	{
		switch (e.type)
		{
		case SDL_QUIT:
			return GUI_command::STOP;
		case SDL_KEYDOWN:
			if (e.key.repeat == 0)
			{
				switch (e.key.keysym.sym)
				{
				case SDLK_s:
				case SDLK_d:
					return GUI_command::SAVE;
				case SDLK_a:
					return GUI_command::SHOW_A; // show frame A
				case SDLK_b:
					return GUI_command::SHOW_B; // show frame B
				case SDLK_m:
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
					if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0) {
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
		case SDL_WINDOWEVENT:
			if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_EXPOSED) {
				// On resize, clear current backbuffer and request a redraw; scaling handled by logical size
				SDL_RenderSetViewport(renderer, NULL);
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
				SDL_RenderClear(renderer);
				return GUI_command::REDRAW;
			}
		}
	}
	SDL_RenderPresent(renderer);
	return GUI_command::CONTINUE;
}

#endif


