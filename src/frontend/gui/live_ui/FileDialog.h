#pragma once

// Native system file pickers.
//
// SDL 3.1.3+ ships platform dialogs (GTK/portal on Linux, Cocoa on macOS,
// IFileDialog on Windows), so the live UI gets a real file selector with no
// extra dependency. SDL invokes the completion callback on an unspecified
// thread, so results are parked behind a mutex and drained by the UI thread.

#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;

namespace rc_live_ui {

class FileDialogs {
public:
	// The kind of file being chosen; selects the filter list shown.
	enum class Kind {
		InputImage,
		OutputImage,
		Palette,
		MaskImage,
		OnOffText,
	};

	// True when the running SDL provides native dialogs. When false, callers
	// should keep the text field editable and hide the browse button.
	static bool Available();

	// Opens a picker. `target` is an opaque caller-chosen id echoed back by
	// Poll(), so one FileDialogs instance can serve every path field on screen.
	// `current` seeds the starting directory. Ignored while another dialog of
	// the same target is already open.
	void RequestOpen(SDL_Window* window, std::string target, Kind kind,
		const std::string& current);
	void RequestSave(SDL_Window* window, std::string target, Kind kind,
		const std::string& current);

	// Drains one completed selection. Returns false when nothing is ready.
	// A cancelled dialog produces no result at all.
	bool Poll(std::string* target, std::string* path);

	bool IsPending() const;

	// Called from the SDL dialog completion callback, possibly off the UI
	// thread. Not part of the intended caller-facing surface.
	void Push(std::string target, std::string path);
	void Cancelled();

private:
	struct Result {
		std::string target;
		std::string path;
	};

	mutable std::mutex mutex_;
	std::vector<Result> results_;
	int pending_ = 0;
};

} // namespace rc_live_ui
