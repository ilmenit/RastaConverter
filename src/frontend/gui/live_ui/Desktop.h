#pragma once

// Handing something to the desktop: opening a file with whatever program the
// user has registered for it, showing a folder in their file manager, and
// finding the resources bundled beside the executable.
//
// All of it goes through SDL, which already knows the per-platform incantation
// (xdg-open, open, ShellExecute). Doing it ourselves would mean three code
// paths to get wrong.

#include <string>

namespace rc_live_ui {

// A bundled resource - Palettes/, Generator/ - resolved against the directory
// the executable lives in when it is not found relative to the working
// directory. Launching from a desktop shortcut or a file manager makes the
// working directory meaningless, and that is the normal way to start a GUI.
std::string BundledPath(const std::string& relative);

// Opens a file with the program registered for its type, or a folder in the
// file manager. Returns false and fills `error` when the desktop refuses,
// which on a headless or minimal system it will.
bool OpenWithDesktop(const std::string& path, std::string* error);

// The folder containing `path`, opened as above. For "show me where this run
// wrote its files".
bool ShowInFileManager(const std::string& path, std::string* error);

} // namespace rc_live_ui
