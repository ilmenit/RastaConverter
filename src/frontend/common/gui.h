#pragma once

enum GUI_command {
	STOP,
	CONTINUE,
	SAVE,
	REDRAW,
	SHOW_A,   // dual-mode: show frame A
	SHOW_B,   // dual-mode: show frame B
	SHOW_MIX  // dual-mode: show blended
};

// Which of the converter's pictures a published bitmap is. Used by the
// additive live-UI publishing calls; the console frontend ignores them.
enum class GuiImageSlot {
	Source, // the resized input
	Target, // the quantized, dithered destination
	Output, // what the current best raster program renders
};

// One grey level per target pixel, as DetailsMask stores it: the effective
// error weighting, not the source file. Published so the run's viewer can
// overlay it the same way the setup preview does.
struct GuiDetailsMask {
	const unsigned char* values = nullptr;
	int width = 0;
	int height = 0;
};
