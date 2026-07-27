#pragma once

#include <memory>

#include "LiveStats.h"

struct Configuration;
struct FIBITMAP;
struct GuiEditorApply;
union SDL_Event;
struct SDL_Renderer;
struct SDL_Window;

namespace rc_live_ui {

// Runs before preprocessing. Returns false when the user cancels, which ends
// the session. `show_recent` opens with the history visible, which is what a
// conversion that has just finished wants: its result is the newest card.
bool RunSetup(Configuration& configuration, bool show_recent = false);

// Which of the converter's pictures a published bitmap is.
enum class ImageSlot {
	Source,      // the resized input
	Target,      // the quantized, dithered destination
	Output,      // what the current best raster program renders
};

// Hosts the during-run dashboard. When present it owns the whole window; the
// legacy three-blit display is skipped, and the same viewer widget the Setup
// screen used carries its zoom and pan over.
class Overlay {
public:
	Overlay();
	~Overlay();
	Overlay(const Overlay&) = delete;
	Overlay& operator=(const Overlay&) = delete;

	bool Initialize(SDL_Window* window, SDL_Renderer* renderer);
	void ProcessEvent(const SDL_Event& event);
	void Render();

	// Feeds the dashboard. Safe to call every update; uploads are cheap.
	void PublishStats(const LiveStats& stats);
	void PublishBitmap(ImageSlot slot, FIBITMAP* bitmap);
	// One grey level per target pixel; null clears the overlay.
	void PublishDetailsMask(const unsigned char* values,
		const unsigned char* editable_values, int width, int height);
	void PublishDestinationLayer(const unsigned char* palette_indices,
		int width, int height);
	bool TakeEditorApply(GuiEditorApply& request);
	bool EditorWantsDestination() const;

	// Drains a command the user issued through the dashboard buttons.
	LiveCommand TakeCommand();

	bool WantsKeyboard() const;
	bool WantsMouse() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace rc_live_ui
