#ifdef NO_GUI
#include "RastaConsole.h"
#include <iostream>

bool Init(std::string command_line)
{
	return true;
}

void RastaConsole::DisplayText(int x, int y, const std::string& text)
{
	std::cout << text << std::endl;
}

void RastaConsole::Error(std::string e)
{
	std::cerr << e << std::endl;
	exit(1);
}

void RastaConsole::DisplayBitmapLine(int x, int y, int line_y, FIBITMAP* fiBitmap)
{
}


void RastaConsole::DisplayBitmap(int x, int y, FIBITMAP* fiBitmap)
{
}

GUI_command RastaConsole::NextFrame()
{
	return GUI_command::CONTINUE;
}

#endif
