#pragma once

#ifndef NO_GUI

#include <SDL.h>
#include <FreeImage.h>
#include <SDL_ttf.h>
#include <string>
#include "gui.h"

class RastaSDL {
private:
    int window_width = 320 * 3;
    int window_height = 480;

    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    SDL_Surface* FIBitmapToSDLSurface(FIBITMAP* fiBitmap);
    SDL_Surface* FIBitmapLineToSDLSurface(FIBITMAP* fiBitmap, int line_y);
public:
    bool Init(std::string command_line);
    void Error(std::string e);
    void DisplayBitmapLine(int x, int y, int line_y, FIBITMAP* fiBitmap);
    void DisplayText(int x, int y, const std::wstring& text);
    void DisplayBitmap(int x, int y, FIBITMAP* fiBitmap);
    GUI_command NextFrame();

};

#endif // NO_GUI
