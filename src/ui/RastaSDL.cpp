#ifndef NO_GUI
#include "RastaSDL.h"
#include <iostream>
#include <time.h>

using namespace std;

bool RastaSDL::Init(std::string command_line)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		std::cerr << "SDL_Init: " << SDL_GetError() << std::endl;
		return false;
	}

	std::string title = "Rasta Converter " + command_line;
	window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
	renderer = SDL_CreateRenderer(window, -1, 0);

	// load font
	TTF_Init();
	string font_path = SDL_GetBasePath();
	font_path += "clacon2.ttf";

	font = TTF_OpenFont(font_path.c_str(), 16);
    if (!font) {
        std::cerr << "SDL_Init: Cannot load font clacon2.ttf" << std::endl;
        string error_text = string("TTF_OpenFont: ") + TTF_GetError();
        std::cerr << error_text << std::endl;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", error_text.c_str(), NULL);

        return false;
    }



    return true;
}

void RastaSDL::DisplayText(int x, int y, const std::string& text)
{
        static SDL_Color textColor = { 255, 255, 255, 255 };
        static SDL_Color backgroundColor = { 0, 0, 0, 255 }; // Black background color

	// Create a surface from the string with a solid background
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
    if (font) { TTF_CloseFont(font); font = nullptr; }
    if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    TTF_Quit();
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

    SDL_RenderPresent(renderer);
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
#ifdef UI_DEBUG
        std::cout << "[UI] Event type=" << e.type << std::endl;
#endif
		switch (e.type)
		{
		case SDL_QUIT:
#ifdef UI_DEBUG
            std::cout << "[UI] SDL_QUIT received" << std::endl;
#endif
            // Ask for confirmation before quitting via window close
            {
                const SDL_MessageBoxButtonData buttons[] = {
                    { /* .flags, .buttonid, .text */        0, 0, "No" },
                    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
                };
                const SDL_MessageBoxData messageboxdata = {
                    SDL_MESSAGEBOX_INFORMATION, /* .flags */
                    NULL, /* .window */
                    "Rasta Converter", /* .title */
                    "Are you sure you want to quit?", /* .message */
                    SDL_arraysize(buttons), /* .numbuttons */
                    buttons, /* .buttons */
                    NULL /* .colorScheme */
                };
                int buttonid = 0;
                if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0) {
                    SDL_Log("error displaying message box");
                    return GUI_command::CONTINUE;
                }
                if (buttonid == 1) {
#ifdef UI_DEBUG
                    std::cout << "[UI] SDL_QUIT -> STOP confirmed by message box" << std::endl;
#endif
                    return GUI_command::STOP;
                }
#ifdef UI_DEBUG
                std::cout << "[UI] SDL_QUIT -> CONTINUE (No selected)" << std::endl;
#endif
                return GUI_command::CONTINUE;
            }
		case SDL_KEYDOWN:
			if (e.key.repeat == 0)
			{
				switch (e.key.keysym.sym)
				{
				case SDLK_s:
				case SDLK_d:
#ifdef UI_DEBUG
                    std::cout << "[UI] Key S/D -> SAVE" << std::endl;
#endif
					return GUI_command::SAVE;
                case SDLK_ESCAPE: {
                    #ifdef UI_DEBUG
                    std::cout << "[UI] ESC pressed" << std::endl;
                    #endif
                    // Wrap message box locals in a block to avoid cross-case init issues
                    const SDL_MessageBoxButtonData buttons[] = {
                        { 0, 0, "No" },
                        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
                    };
                    const SDL_MessageBoxColorScheme colorScheme = {
                        { { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 } }
                    };
                    const SDL_MessageBoxData messageboxdata = {
                        SDL_MESSAGEBOX_INFORMATION,
                        NULL,
                        "Rasta Converter",
                        "Are you sure you want to quit?",
                        SDL_arraysize(buttons),
                        buttons,
                        &colorScheme
                    };
                    int buttonid = 0;
                    if (SDL_ShowMessageBox(&messageboxdata, &buttonid) < 0) {
                        SDL_Log("error displaying message box");
                        return GUI_command::STOP;
                    }
                    if (buttonid == 1) {
                        #ifdef UI_DEBUG
                        std::cout << "[UI] ESC -> STOP confirmed by message box" << std::endl;
                        #endif
                        return GUI_command::STOP;
                    }
                    #ifdef UI_DEBUG
                    std::cout << "[UI] ESC -> CONTINUE (No selected)" << std::endl;
                    #endif
                    return GUI_command::CONTINUE;
                }
                case SDLK_b:
                    return GUI_command::SHOW_BLENDED;
                case SDLK_a:
                    return GUI_command::SHOW_A;
                case SDLK_z:
                    return GUI_command::SHOW_B;
				}
			}
			break;
		case SDL_WINDOWEVENT:
#ifdef UI_DEBUG
            std::cout << "[UI] Window event=" << (int)e.window.event << std::endl;
#endif
			if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
				int newWidth = e.window.data1;
				int newHeight = e.window.data2;

				// Adjust the viewport to the new window size

				SDL_Rect new_rect = { 0, 0, newWidth, newHeight };
				SDL_RenderSetViewport(renderer, &new_rect );

				// Optionally, you can also adjust the scale to stretch the content
				float scaleX = (float)newWidth / (float)window_width; // originalWidth is the width of your content
				float scaleY = (float)newHeight / (float)window_height; // originalHeight is the height of your content
				SDL_RenderSetScale(renderer, scaleX, scaleY);

                #ifdef UI_DEBUG
                std::cout << "[UI] Returning REDRAW due to resize" << std::endl;
                #endif
                return GUI_command::REDRAW;
			}
		}
	}
	SDL_RenderPresent(renderer);
    #ifdef UI_DEBUG
    std::cout << "[UI] Returning CONTINUE (no events)" << std::endl;
    #endif
	return GUI_command::CONTINUE;
}

#endif
