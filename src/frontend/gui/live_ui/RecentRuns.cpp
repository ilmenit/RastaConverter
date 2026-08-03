#include "RecentRuns.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

#if defined(__APPLE__)
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif
#include <FreeImage.h>
#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif

#include "FreeImageIO.h"
#include "Utf8Path.h"

namespace rc_live_ui {

namespace {

// How many folder names to try before giving up, so a pathological directory
// cannot spin forever.
constexpr int kMaxRunIndex = 999;

// Longest side of a gallery thumbnail, in pixels.
constexpr int kThumbnailSize = 256;

std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string DirectoryOf(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string StripExtension(const std::string& name)
{
	const size_t dot = name.find_last_of('.');
	return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

// Folder names have to survive being typed and tab-completed, so anything
// awkward in the image name becomes an underscore.
std::string Sanitize(const std::string& name)
{
	std::string safe;
	safe.reserve(name.size());
	for (char c : name) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (u >= 0x80 || std::isalnum(u) || c == '-' || c == '_' || c == '.')
			safe.push_back(c);
		else
			safe.push_back('_');
	}
	while (!safe.empty() && (safe.back() == '.' || safe.back() == '_'))
		safe.pop_back();
	return safe.empty() ? std::string("image") : safe;
}

bool PathExists(const std::string& path)
{
	return SDL_GetPathInfo(path.c_str(), nullptr);
}

// The history index lives with the user's other preferences, because runs
// themselves are scattered across whatever folders their inputs came from.
std::string IndexPath()
{
	char* pref = SDL_GetPrefPath("RastaConverter", "RastaConverter");
	if (pref == nullptr)
		return std::string();
	std::string path = std::string(pref) + "recent-runs.txt";
	SDL_free(pref);
	return path;
}

std::vector<std::string> ReadIndex()
{
	std::vector<std::string> folders;
	const std::string path = IndexPath();
	if (path.empty())
		return folders;
	std::ifstream in(Utf8Path(path));
	std::string line;
	while (std::getline(in, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			line.pop_back();
		if (!line.empty())
			folders.push_back(line);
	}
	return folders;
}

void WriteIndex(const std::vector<std::string>& folders)
{
	const std::string path = IndexPath();
	if (path.empty())
		return;
	// Written whole each time; the list is short and this keeps a partial
	// write from corrupting the order.
	std::ofstream out(Utf8Path(path), std::ios::trunc);
	for (const std::string& folder : folders)
		out << folder << '\n';
}

struct BitmapDeleter {
	void operator()(FIBITMAP* bitmap) const
	{
		if (bitmap != nullptr)
			FreeImage_Unload(bitmap);
	}
};
using BitmapPtr = std::unique_ptr<FIBITMAP, BitmapDeleter>;

// Loads a picture as a small RGBA thumbnail. Freshly loaded FreeImage bitmaps
// are bottom-up, so the rows are flipped here (unlike the converter's own
// buffers, which already live in scanline order).
bool LoadThumbnail(const std::string& path, PreviewImage* out)
{
	FREE_IMAGE_FORMAT format = FreeImageFormatUtf8(path);
	if (format == FIF_UNKNOWN)
		return false;
	BitmapPtr loaded(FreeImageLoadUtf8(path));
	if (!loaded)
		return false;

	const int source_width = static_cast<int>(FreeImage_GetWidth(loaded.get()));
	const int source_height = static_cast<int>(FreeImage_GetHeight(loaded.get()));
	if (source_width <= 0 || source_height <= 0)
		return false;

	// SavePicture already stretches to double width before writing (see
	// RescaleFIBitmapDoubleWidth), so the file on disk is 320 wide and its
	// aspect ratio is already the display one. Correcting again here squashed
	// every thumbnail to half its proper height.
	const float scale = std::min(1.0f,
		static_cast<float>(kThumbnailSize) / std::max(source_width, source_height));
	const int width = std::max(1, static_cast<int>(source_width * scale));
	const int height = std::max(1, static_cast<int>(source_height * scale));

	BitmapPtr scaled(FreeImage_Rescale(loaded.get(), width, height, FILTER_BILINEAR));
	if (!scaled)
		return false;
	BitmapPtr rgb24(FreeImage_ConvertTo24Bits(scaled.get()));
	if (!rgb24)
		return false;

	out->width = width;
	out->height = height;
	out->pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	for (int y = 0; y < height; ++y) {
		const BYTE* row = FreeImage_GetScanLine(rgb24.get(), height - 1 - y);
		for (int x = 0; x < width; ++x) {
			const BYTE* pixel = row + x * 3;
			(*out).pixels[static_cast<size_t>(y) * width + x] =
				static_cast<std::uint32_t>(pixel[FI_RGBA_RED])
				| (static_cast<std::uint32_t>(pixel[FI_RGBA_GREEN]) << 8)
				| (static_cast<std::uint32_t>(pixel[FI_RGBA_BLUE]) << 16)
				| 0xFF000000u;
		}
	}
	return true;
}

// Trims a trailing separator so folder paths compare equal.
std::string NormalizeFolder(std::string folder)
{
	while (folder.size() > 1
		&& (folder.back() == '/' || folder.back() == '\\')) {
		folder.pop_back();
	}
	return folder;
}

std::string ValueAfter(const std::string& line, const char* key)
{
	const size_t pos = line.find(key);
	if (pos == std::string::npos)
		return std::string();
	std::string value = line.substr(pos + std::strlen(key));
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
		value.erase(value.begin());
	while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
		value.pop_back();
	return value;
}

// Finds the run's .opt file. The basename follows the output name, which is
// the image name by construction, but a folder is scanned rather than assumed
// so hand-made or renamed folders still work.
std::string FindOptFile(const std::string& folder)
{
	std::string found;
	SDL_EnumerateDirectory(folder.c_str(),
		[](void* userdata, const char* dir, const char* name) -> SDL_EnumerationResult {
			std::string* out = static_cast<std::string*>(userdata);
			const std::string file(name);
			// ".opt" exactly, not ".opt.h" or ".opt.ini".
			if (file.size() > 4 && file.compare(file.size() - 4, 4, ".opt") == 0) {
				*out = std::string(dir) + file;
				return SDL_ENUM_SUCCESS;
			}
			return SDL_ENUM_CONTINUE;
		},
		&found);
	return found;
}

} // namespace

std::string AllocateRunOutputPath(const std::string& input_path, bool subfolder)
{
	if (input_path.empty())
		return "output.png";
	const std::string directory = DirectoryOf(input_path);
	const std::string base = Sanitize(StripExtension(FileName(input_path)));

	if (!subfolder) {
		// Beside the source image. The plain name is used when it is free -
		// and it is never free when the source is itself a .png of that name,
		// which is the common case and would mean writing the output over the
		// input. Otherwise the numbering starts at 001, so the sequence reads
		// as one rather than starting at two for no visible reason.
		const std::string plain = directory + base + ".png";
		if (!PathExists(plain))
			return plain;
		for (int index = 1; index <= kMaxRunIndex; ++index) {
			char suffix[8];
			std::snprintf(suffix, sizeof(suffix), "%03d", index);
			const std::string candidate = directory + base + "-" + suffix + ".png";
			if (!PathExists(candidate))
				return candidate;
		}
		return directory + base + "-999.png";
	}

	for (int index = 1; index <= kMaxRunIndex; ++index) {
		char suffix[8];
		std::snprintf(suffix, sizeof(suffix), "%03d", index);
		const std::string folder = directory + "rc-" + base + "-" + suffix;
		if (!PathExists(folder))
			return folder + "/" + base + ".png";
	}
	// Every name taken: fall back to the last one rather than refusing to run.
	return directory + "rc-" + base + "-999/" + base + ".png";
}

bool CreateRunFolder(const std::string& output_path, std::string* error)
{
	const std::string folder = NormalizeFolder(DirectoryOf(output_path));
	if (folder.empty())
		return true; // writing to the working directory
	if (PathExists(folder))
		return true;
	if (!SDL_CreateDirectory(folder.c_str())) {
		if (error != nullptr) {
			*error = "Could not create " + folder + ": "
				+ (SDL_GetError() != nullptr ? SDL_GetError() : "unknown error");
		}
		return false;
	}
	return true;
}

void RegisterRecentRun(const std::string& folder)
{
	const std::string normalized = NormalizeFolder(folder);
	if (normalized.empty())
		return;
	std::vector<std::string> folders = ReadIndex();
	folders.erase(std::remove_if(folders.begin(), folders.end(),
		[&normalized](const std::string& entry) {
			return NormalizeFolder(entry) == normalized;
		}), folders.end());
	folders.insert(folders.begin(), normalized);
	if (folders.size() > 200)
		folders.resize(200);
	WriteIndex(folders);
}

void ForgetRecentRun(const std::string& folder)
{
	const std::string normalized = NormalizeFolder(folder);
	std::vector<std::string> folders = ReadIndex();
	const size_t before = folders.size();
	folders.erase(std::remove_if(folders.begin(), folders.end(),
		[&normalized](const std::string& entry) {
			return NormalizeFolder(entry) == normalized;
		}), folders.end());
	if (folders.size() != before)
		WriteIndex(folders);
}

size_t ClearRecentRuns(bool delete_folders, size_t* skipped)
{
	std::vector<std::string> folders = ReadIndex();
	size_t removed = 0;
	size_t left = 0;
	if (delete_folders) {
		for (const std::string& raw : folders) {
			const std::string folder = NormalizeFolder(raw);
			// The guard: a run folder is one this program named. Anything else
			// in the index - a hand-edited line, a path that once pointed at a
			// working directory - is forgotten but not deleted.
			const std::string name = FileName(folder);
			if (name.rfind("rc-", 0) != 0) {
				++left;
				continue;
			}
			std::error_code ec;
			const uintmax_t count = std::filesystem::remove_all(Utf8Path(folder), ec);
			if (ec || count == 0)
				++left;
			else
				++removed;
		}
	}
	WriteIndex(std::vector<std::string>());
	if (skipped != nullptr)
		*skipped = left;
	return removed;
}

std::vector<RunSummary> LoadRecentRuns(bool load_thumbnails, size_t limit)
{
	std::vector<RunSummary> runs;
	std::vector<std::string> folders = ReadIndex();
	std::vector<std::string> surviving;
	surviving.reserve(folders.size());

	for (const std::string& raw : folders) {
		if (runs.size() >= limit) {
			surviving.push_back(raw); // keep the tail of the history intact
			continue;
		}
		const std::string folder = NormalizeFolder(raw);
		// The user may have deleted the folder; the history follows suit.
		if (!PathExists(folder))
			continue;
		surviving.push_back(folder);

		RunSummary summary;
		summary.folder = folder;
		summary.label = FileName(folder);

		SDL_PathInfo info;
		if (SDL_GetPathInfo(folder.c_str(), &info))
			summary.modified_time = static_cast<std::int64_t>(info.modify_time / 1000000000);

		const std::string opt = FindOptFile(folder + "/");
		if (!opt.empty()) {
			bool playfield_recorded = false;
			summary.resumable = true;
			// /o was the .opt path without its extension.
			summary.output_base = opt.substr(0, opt.size() - 4);

			std::ifstream in(Utf8Path(opt));
			std::string line;
			// Everything wanted is in the leading comment block.
			while (std::getline(in, line)) {
				if (line.empty() || line[0] != ';')
					break;
				if (summary.input_file.empty()) {
					const std::string value = ValueAfter(line, "; InputName:");
					if (!value.empty())
						summary.input_file = value;
				}
				if (summary.command_line.empty()) {
					const std::string value = ValueAfter(line, "; CmdLine:");
					if (!value.empty()) {
						summary.command_line = value;
						if (value.find("/graphics_mode=antic4")
							!= std::string::npos)
							summary.text_mode = true;
						if (value.find("/playfield=wide") != std::string::npos) {
							summary.wide_playfield = true;
							playfield_recorded = true;
						} else if (value.find("/playfield=normal")
							!= std::string::npos) {
							summary.wide_playfield = false;
							playfield_recorded = true;
						}
					}
				}
				if (line.find("; Graphics Mode: ANTIC 4")
					!= std::string::npos)
					summary.text_mode = true;
				const std::string playfield =
					ValueAfter(line, "; Playfield Width:");
				if (playfield == "WIDE" || playfield == "NORMAL") {
					summary.wide_playfield = playfield == "WIDE";
					playfield_recorded = true;
				}
				const std::string evaluations = ValueAfter(line, "; Evaluations:");
				if (!evaluations.empty())
					summary.evaluations = std::strtoull(evaluations.c_str(), nullptr, 10);
				const std::string score = ValueAfter(line, "; Score:");
				if (!score.empty()) {
					summary.score = std::strtod(score.c_str(), nullptr);
					summary.has_score = true;
				}
				const std::string edited = ValueAfter(line, "; Mask Edited:");
				if (!edited.empty())
					summary.mask_edited = edited == "yes";
				const std::string destinationEdited =
					ValueAfter(line, "; Destination Edited:");
				if (!destinationEdited.empty() && destinationEdited == "yes")
					summary.mask_edited = true;
				const std::string snapshots = ValueAfter(line, "; Snapshots:");
				if (!snapshots.empty())
					summary.snapshots = static_cast<unsigned>(
						std::strtoul(snapshots.c_str(), nullptr, 10));
			}
			// Before width selection existed, ANTIC 4 always emitted the wide
			// 168-clock layout. ANTIC E's historical default was Normal.
			if (!playfield_recorded && summary.text_mode)
				summary.wide_playfield = true;
		}

		// Prefer the converted picture; fall back to the target, which exists
		// even if a run was stopped during preprocessing. Dual runs deliberately
		// use fixed A/B names, so their result is not the basename inferred from
		// out_dual_A.opt. Keep the picture path separate: output_base must remain
		// the .opt base used for resume and XEX assembly.
		summary.dual_mode = PathExists(folder + "/out_dual_A.opt");
		std::vector<std::string> candidates;
		if (summary.dual_mode) {
			candidates.push_back(folder + "/out_dual_blended.png");
			candidates.push_back(folder + "/out_dual_A.png");
			candidates.push_back(folder + "/out_dual_B.png");
		}
		candidates.push_back(summary.output_base);
		candidates.push_back(summary.output_base + "-dst.png");
		candidates.push_back(summary.output_base + "-src.png");
		for (const std::string& candidate : candidates) {
			if (candidate.empty() || !PathExists(candidate))
				continue;
			if (summary.picture_path.empty())
				summary.picture_path = candidate;
			if (load_thumbnails && LoadThumbnail(candidate, &summary.thumbnail)) {
				summary.picture_path = candidate;
				break;
			}
		}

		runs.push_back(std::move(summary));
	}

	if (surviving.size() != folders.size())
		WriteIndex(surviving);
	return runs;
}

} // namespace rc_live_ui
