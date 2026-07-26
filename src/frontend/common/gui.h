#pragma once

#include <vector>

enum GUI_command {
	STOP,
	CONTINUE,
	SAVE,
	REDRAW,
	MASK_EDIT,
	BRANCH,
	DESTINATION_BEGIN,
	DESTINATION_APPLY,
	DESTINATION_DISCARD,
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
	const unsigned char* editable_values = nullptr;
	int width = 0;
	int height = 0;
};

// A completed paint stroke. Changes are coalesced per pixel by the UI, so the
// core can apply them atomically at an iteration boundary and undo can restore
// the exact prior bytes.
struct GuiMaskPixelChange {
	unsigned x = 0;
	unsigned y = 0;
	unsigned char before = 0;
	unsigned char after = 0;
};

struct GuiMaskStroke {
	std::vector<GuiMaskPixelChange> pixels;
};

struct GuiDestinationLayer {
	const unsigned char* palette_indices = nullptr;
	int width = 0;
	int height = 0;
};
