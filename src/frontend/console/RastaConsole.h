#pragma once

#ifdef NO_GUI

#include <string>
#include "FreeImage.h"
#include "gui.h"

class RastaConsole {
private:
public:
    bool Init(std::string command_line);
    void Error(std::string e);
    void DisplayBitmapLine(int x, int y, int line_y, FIBITMAP* fiBitmap);
    void DisplayText(int x, int y, const std::string& text);
    void DisplayBitmap(int x, int y, FIBITMAP* fiBitmap);
    void Present();
    GUI_command NextFrame();
};

#endif // NO_GUI


