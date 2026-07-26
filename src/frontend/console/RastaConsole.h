#pragma once

#ifdef NO_GUI

#include <string>
#include "FreeImage.h"
#include "gui.h"
#include "LiveStats.h"

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

    // The console frontend has no live UI; these keep the converter's call
    // sites uniform across frontends.
    void PublishStats(const LiveStats&) {}
    void PublishImage(GuiImageSlot, FIBITMAP*) {}
    void PublishDetailsMask(const GuiDetailsMask&) {}
    void PublishDestinationLayer(const GuiDestinationLayer&) {}
    bool TakeMaskStroke(GuiMaskStroke&) { return false; }
    bool TakeDestinationChanges(GuiMaskStroke&) { return false; }
    bool CreateBranchOutput(const std::string&, std::string&, std::string&) {
        return false;
    }
    bool LiveUiActive() const { return false; }
    bool AbortRequested() const { return false; }
};

#endif // NO_GUI
