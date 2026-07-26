#include "FileDialog.h"

#include <SDL3/SDL.h>

#include <cstring>

#if defined(SDL_VERSION_ATLEAST) && SDL_VERSION_ATLEAST(3, 1, 3)
#define RC_HAVE_SDL_DIALOGS 1
#include <SDL3/SDL_dialog.h>
#else
#define RC_HAVE_SDL_DIALOGS 0
#endif

namespace rc_live_ui {

#if RC_HAVE_SDL_DIALOGS

namespace {

// Filter sets, one per Kind. FreeImage reads far more than this, but a picker
// listing forty extensions helps nobody; "All files" is always the last entry.
const SDL_DialogFileFilter kImageFilters[] = {
	{"Images (png jpg bmp gif tga tif webp)", "png;jpg;jpeg;bmp;gif;tga;tif;tiff;webp;pbm;pgm;ppm"},
	{"All files", "*"},
};
const SDL_DialogFileFilter kOutputFilters[] = {
	{"PNG image", "png"},
	{"All files", "*"},
};
const SDL_DialogFileFilter kPaletteFilters[] = {
	{"Atari palette (.act .pal)", "act;pal"},
	{"All files", "*"},
};
const SDL_DialogFileFilter kOnOffFilters[] = {
	{"Register on/off list", "txt;onoff;cfg"},
	{"All files", "*"},
};

struct FilterSet {
	const SDL_DialogFileFilter* filters;
	int count;
};

FilterSet FiltersFor(FileDialogs::Kind kind)
{
	switch (kind) {
	case FileDialogs::Kind::OutputImage:
		return {kOutputFilters, SDL_arraysize(kOutputFilters)};
	case FileDialogs::Kind::Palette:
		return {kPaletteFilters, SDL_arraysize(kPaletteFilters)};
	case FileDialogs::Kind::OnOffText:
		return {kOnOffFilters, SDL_arraysize(kOnOffFilters)};
	case FileDialogs::Kind::InputImage:
	case FileDialogs::Kind::MaskImage:
	default:
		return {kImageFilters, SDL_arraysize(kImageFilters)};
	}
}

// The directory the picker should open in, derived from whatever the field
// currently holds. Returns an empty string to let the platform decide.
std::string StartLocation(const std::string& current)
{
	if (current.empty())
		return std::string();
	const size_t slash = current.find_last_of("/\\");
	if (slash == std::string::npos)
		return std::string();
	std::string directory = current.substr(0, slash);
	if (directory.empty())
		return std::string("/");
	if (!SDL_GetPathInfo(directory.c_str(), nullptr))
		return std::string();
	return directory;
}

} // namespace

// Heap payload handed to SDL; freed by the callback exactly once.
struct DialogRequest {
	FileDialogs* owner;
	std::string target;
};

extern "C" void SDLCALL RcFileDialogCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
	DialogRequest* request = static_cast<DialogRequest*>(userdata);
	if (request == nullptr)
		return;
	// filelist == nullptr signals an error; an empty list signals cancellation.
	if (filelist != nullptr && filelist[0] != nullptr)
		request->owner->Push(request->target, filelist[0]);
	else
		request->owner->Cancelled();
	delete request;
}

bool FileDialogs::Available()
{
	return true;
}

void FileDialogs::RequestOpen(SDL_Window* window, std::string target, Kind kind,
	const std::string& current)
{
	const FilterSet filters = FiltersFor(kind);
	const std::string location = StartLocation(current);
	DialogRequest* request = new DialogRequest{this, std::move(target)};
	{
		std::lock_guard<std::mutex> lock(mutex_);
		++pending_;
	}
	SDL_ShowOpenFileDialog(RcFileDialogCallback, request, window,
		filters.filters, filters.count,
		location.empty() ? nullptr : location.c_str(), false);
}

void FileDialogs::RequestSave(SDL_Window* window, std::string target, Kind kind,
	const std::string& current)
{
	const FilterSet filters = FiltersFor(kind);
	const std::string location = StartLocation(current);
	DialogRequest* request = new DialogRequest{this, std::move(target)};
	{
		std::lock_guard<std::mutex> lock(mutex_);
		++pending_;
	}
	SDL_ShowSaveFileDialog(RcFileDialogCallback, request, window,
		filters.filters, filters.count,
		location.empty() ? nullptr : location.c_str());
}

#else // !RC_HAVE_SDL_DIALOGS

bool FileDialogs::Available()
{
	return false;
}

void FileDialogs::RequestOpen(SDL_Window*, std::string, Kind, const std::string&) {}
void FileDialogs::RequestSave(SDL_Window*, std::string, Kind, const std::string&) {}

#endif // RC_HAVE_SDL_DIALOGS

void FileDialogs::Push(std::string target, std::string path)
{
	std::lock_guard<std::mutex> lock(mutex_);
	results_.push_back({std::move(target), std::move(path)});
	if (pending_ > 0)
		--pending_;
}

void FileDialogs::Cancelled()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (pending_ > 0)
		--pending_;
}

bool FileDialogs::Poll(std::string* target, std::string* path)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (results_.empty())
		return false;
	if (target != nullptr)
		*target = results_.front().target;
	if (path != nullptr)
		*path = results_.front().path;
	results_.erase(results_.begin());
	return true;
}

bool FileDialogs::IsPending() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return pending_ > 0;
}

} // namespace rc_live_ui
