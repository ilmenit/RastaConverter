#pragma once

// Turning a finished run into an Atari executable.
//
// The converter already writes everything the bundled generator needs - the
// optimized raster program, the screen data, the player/missile data - and the
// generator is a MADS source file plus the assembler itself, both shipped with
// the program. So "make me a .xex" is one assembler invocation, and there is no
// reason the user should have to leave the program, find the Generator folder
// and copy files into it by hand.
//
// The .xex lands in the run's own folder next to everything else it produced,
// so a run stays one self-contained thing.

#include <string>

namespace rc_live_ui {

struct XexBuildResult {
	bool ok = false;
	std::string xex_path;
	std::string log;   // assembler output, or why it could not be started
};

// Where the executable for this run is, or would be.
std::string RunXexPath(const std::string& output_base);

// True when that file exists and is newer than the raster program it is built
// from, so it can be opened directly instead of assembled again.
bool RunXexIsCurrent(const std::string& output_base);

// Assembles it. Blocking, and slow enough to be worth a thread: call it off the
// UI thread. `output_base` is the run's /o value - ".../rc-photo-001/photo.png".
XexBuildResult BuildRunXex(const std::string& output_base);

} // namespace rc_live_ui
