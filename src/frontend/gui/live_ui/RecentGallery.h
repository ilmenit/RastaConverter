#pragma once

// The "recent conversions" browser.
//
// Placement: it occupies the viewer column, which is empty anyway until an
// image is chosen, and is reachable later through the Recent button beside the
// image picker. That beats the two obvious alternatives - a permanent
// filmstrip along the bottom costs vertical space on a screen whose whole point
// is a large preview, and a separate window is modal and hides the very
// settings you are about to reuse. Here it costs nothing when unused and is one
// click away when wanted.
//
// Each card shows the run's picture, its folder, the score it reached and how
// many evaluations that took, and offers the only two things anyone wants from
// an old run: carry on with it, or start a fresh one from the same settings.

#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "RecentRuns.h"
#include "XexBuild.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace rc_live_ui {

class RecentGallery {
public:
	// What the user chose to do with a run.
	enum class Action {
		None,
		Continue,   // resume that run in its existing folder
		UseSettings,// new run, same options, new folder
		Dismiss,    // close the gallery
	};

	struct Result {
		Action action = Action::None;
		RunSummary run;
	};

	explicit RecentGallery(SDL_Renderer* renderer);
	~RecentGallery();

	RecentGallery(const RecentGallery&) = delete;
	RecentGallery& operator=(const RecentGallery&) = delete;

	// Re-reads the history from disk. Cheap enough to call when the gallery is
	// opened; not called per frame.
	void Refresh();

	bool empty() const { return runs_.empty(); }

	// Draws into the current content region. `closable` adds the dismiss
	// button, which only makes sense when there is something to go back to.
	Result Draw(bool closable);

private:
	SDL_Texture* TextureFor(size_t index);
	void DrawClearPopup();
	void StartXexBuild(size_t index);
	void PollXexBuild();
	void Open(const std::string& path, bool folder);

	SDL_Renderer* renderer_ = nullptr;
	std::vector<RunSummary> runs_;
	std::vector<SDL_Texture*> textures_;
	// Whether each run already has an executable newer than its raster program.
	// Answered once per refresh rather than per frame: it is two file stats per
	// card, and there can be sixty cards.
	std::vector<char> xex_current_;
	bool loaded_ = false;
	bool clear_folders_ = false;   // the destructive half of "Clear all"

	// Assembling runs off the UI thread - MADS takes long enough to drop frames.
	// One at a time: it is a deliberate act on one run, not a batch.
	std::future<XexBuildResult> xex_future_;
	size_t xex_index_ = 0;
	std::string xex_folder_;
	std::string xex_message_;      // last failure, shown above the grid
	std::string xex_detail_;       // its full log, on hover
	bool xex_failed_ = false;
};

} // namespace rc_live_ui
