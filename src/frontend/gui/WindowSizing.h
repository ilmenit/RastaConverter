#pragma once

#include <SDL3/SDL.h>

// A saved window size is meaningful only while it fits the desktop on which
// it is restored.  In particular, the live UI's default 1420x900 layout must
// not leave its run controls below a laptop-sized display.
namespace rc_gui {

inline bool ExceedsUsableBounds(int client_width, int client_height,
	const SDL_Rect& bounds, int horizontal_decoration = 0,
	int vertical_decoration = 0)
{
	return bounds.w > 0 && bounds.h > 0
		&& (client_width + horizontal_decoration > bounds.w
			|| client_height + vertical_decoration > bounds.h);
}

inline void MaximizeIfOutsideUsableDisplay(SDL_Window* window)
{
	if (window == nullptr)
		return;

	const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
	SDL_Rect usable_bounds{};
	if (display == 0
		|| !SDL_GetDisplayUsableBounds(display, &usable_bounds))
		return;

	int width = 0;
	int height = 0;
	SDL_GetWindowSize(window, &width, &height);
	int top = 0;
	int left = 0;
	int bottom = 0;
	int right = 0;
	// SDL window sizes describe the client area.  Include decoration when it
	// is available, otherwise the conservative client-area check still handles
	// the usual oversized-window case.
	SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
	if (ExceedsUsableBounds(width, height, usable_bounds,
		left + right, top + bottom))
		SDL_MaximizeWindow(window);
}

} // namespace rc_gui
