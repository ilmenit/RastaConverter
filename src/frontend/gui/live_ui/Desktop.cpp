#include "Desktop.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "Utf8Path.h"

namespace rc_live_ui {
namespace {

// file:// wants an absolute path with the awkward characters escaped. Spaces
// are the ones that actually turn up - a run folder is named after the picture,
// and pictures live in folders people named themselves.
std::string FileUrl(const std::string& path)
{
	const std::string absolute = Utf8AbsolutePath(path);
	const std::string generic = Utf8Path(absolute).generic_u8string();
	std::string url;
#if defined(_WIN32)
	// generic UNC paths begin with //server/share. Prefixing only "file:"
	// preserves the authority; drive paths need the conventional third slash.
	url = generic.rfind("//", 0) == 0 ? "file:" : "file:///";
#else
	url = "file://";
#endif
	for (char c : generic) {
		const unsigned char u = static_cast<unsigned char>(c);
		const bool safe = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
			|| (u >= '0' && u <= '9') || c == '/' || c == '-' || c == '_'
			|| c == '.' || c == '~' || c == ':';
		if (safe) {
			url += c;
		} else {
			char escaped[4];
			std::snprintf(escaped, sizeof(escaped), "%%%02X", u);
			url += escaped;
		}
	}
	return url;
}

} // namespace

std::string BundledPath(const std::string& relative)
{
	if (relative.empty() || SDL_GetPathInfo(relative.c_str(), nullptr))
		return relative;
	const char* base = SDL_GetBasePath();
	if (base == nullptr)
		return relative;
	const std::string candidate = std::string(base) + relative;
	if (SDL_GetPathInfo(candidate.c_str(), nullptr))
		return candidate;
	return relative;
}

bool OpenWithDesktop(const std::string& path, std::string* error)
{
	if (path.empty()) {
		if (error != nullptr)
			*error = "nothing to open";
		return false;
	}
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path.c_str(), &info)) {
		if (error != nullptr)
			*error = path + " is not there any more";
		return false;
	}
	if (SDL_OpenURL(FileUrl(path).c_str()))
		return true;
	if (error != nullptr) {
		const char* message = SDL_GetError();
		*error = message != nullptr && message[0] != '\0'
			? message : "the desktop would not open it";
	}
	return false;
}

bool ShowInFileManager(const std::string& path, std::string* error)
{
	SDL_PathInfo info;
	if (SDL_GetPathInfo(path.c_str(), &info)
		&& info.type == SDL_PATHTYPE_DIRECTORY)
		return OpenWithDesktop(path, error);

	const std::string parent = Utf8ParentPath(path);
	if (parent.empty()) {
		if (error != nullptr)
			*error = "no folder to show";
		return false;
	}
	return OpenWithDesktop(parent, error);
}

} // namespace rc_live_ui
