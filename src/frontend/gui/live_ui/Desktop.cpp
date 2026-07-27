#include "Desktop.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace rc_live_ui {
namespace {

// file:// wants an absolute path with the awkward characters escaped. Spaces
// are the ones that actually turn up - a run folder is named after the picture,
// and pictures live in folders people named themselves.
std::string FileUrl(const std::string& path)
{
	std::string absolute = path;
	const bool rooted = !path.empty() && (path[0] == '/' || path[0] == '\\'
#if defined(_WIN32)
		|| (path.size() >= 2 && path[1] == ':')
#endif
		);
	if (!rooted) {
		char* cwd = SDL_GetCurrentDirectory();
		if (cwd != nullptr) {
			absolute = std::string(cwd) + path; // SDL leaves the separator on
			SDL_free(cwd);
		}
	}

	std::string url = "file://";
#if defined(_WIN32)
	url += '/';
#endif
	for (char c : absolute) {
		if (c == '\\') {
			url += '/';
			continue;
		}
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

	const size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos) {
		if (error != nullptr)
			*error = "no folder to show";
		return false;
	}
	return OpenWithDesktop(path.substr(0, slash), error);
}

} // namespace rc_live_ui
