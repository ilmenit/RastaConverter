#pragma once

// Per-conversion output folders, and the history of them.
//
// A conversion writes a dozen files. Dropping those beside the source image
// turns any working folder into a junk drawer, so each run gets its own
// directory named `rc-<image>-NNN` next to the input, with the counter stepping
// up whenever the name is taken. That also makes a run a single self-contained
// thing that can be listed, resumed or deleted.
//
// Nothing new is written to describe a run: the `.opt` file the converter
// already saves carries the input path, the full command line, the evaluation
// count and the normalized score in its header comments, and `/continue`
// already reads it back. This just parses the same header.

#include <cstdint>
#include <string>
#include <vector>

#include "TargetPreview.h" // PreviewImage

namespace rc_live_ui {

// What one previous conversion looked like.
struct RunSummary {
	std::string folder;       // .../rc-photo-001
	std::string output_base;  // .../rc-photo-001/photo.png  (what /o was)
	std::string input_file;   // the source image, as recorded by the run
	std::string command_line; // reproduces the run
	std::string label;        // folder name, for display

	unsigned long long evaluations = 0;
	double score = 0.0;       // normalized distance; 0 when unknown
	bool has_score = false;
	bool mask_edited = false;
	unsigned snapshots = 0;
	std::int64_t modified_time = 0; // for ordering, seconds

	// Set when the run's picture could be read, for the gallery thumbnail.
	PreviewImage thumbnail;
	bool resumable = false; // an .opt exists, so /continue has something to load
};

// Path the next run for `input_path` should write to, of the form
// <input dir>/rc-<input base>-NNN/<input base>.png. The directory is not
// created here; nothing is written until the user commits.
std::string AllocateRunOutputPath(const std::string& input_path);

// Creates the folder holding `output_path`. Returns false and fills `error`
// when it cannot, so the caller can refuse to start rather than fail later.
bool CreateRunFolder(const std::string& output_path, std::string* error);

// Records a run folder in the user-level history, most recent first.
void RegisterRecentRun(const std::string& folder);

// Previous runs, newest first, skipping folders that no longer exist. Reads
// each run's `.opt` header for its statistics. `load_thumbnails` decodes the
// output picture too, which the gallery wants and a plain listing does not.
std::vector<RunSummary> LoadRecentRuns(bool load_thumbnails, size_t limit = 60);

// Forgets a run folder (used when its directory has been removed).
void ForgetRecentRun(const std::string& folder);

} // namespace rc_live_ui
