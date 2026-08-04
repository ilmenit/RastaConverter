#include "UiPreferences.h"
#include "../src/frontend/gui/WindowSizing.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

} // namespace

int main()
{
	using namespace rc_live_ui;
	const SDL_Rect laptop_work_area{0, 0, 1366, 728};
	Require(rc_gui::ExceedsUsableBounds(1420, 900, laptop_work_area),
		"the live UI default must be recognized as too large for a 1366x768 desktop");
	Require(!rc_gui::ExceedsUsableBounds(1366, 728, laptop_work_area),
		"a window that fits the usable desktop must not be maximized");
	Require(rc_gui::ExceedsUsableBounds(1366, 728, laptop_work_area, 16, 39),
		"native decorations must count when fitting a client area to the desktop");

	const UiPreferences defaults = ParseUiPreferences("");
	Require(defaults.run_subfolder
			&& defaults.setup_window_width == 1420
			&& defaults.setup_window_height == 900
			&& defaults.setup_form_width == 560.0f
			&& !defaults.setup_only_modified
			&& defaults.setup_open_sections == 0x5u,
		"missing preference file must retain editor defaults");

	const UiPreferences loaded = ParseUiPreferences(
		"version=1\r\n"
		"run_subfolder=0\r\n"
		"setup_window_width=1760\r\n"
		"setup_window_height=1040\r\n"
		"setup_form_width=635.5\r\n"
		"setup_only_modified=1\r\n"
		"setup_open_sections=11\r\n"
		"future_option=ignored\r\n");
	Require(!loaded.run_subfolder
			&& loaded.setup_window_width == 1760
			&& loaded.setup_window_height == 1040
			&& loaded.setup_form_width == 635.5f
			&& loaded.setup_only_modified
			&& loaded.setup_open_sections == 11u,
		"saved editor preferences must round-trip");

	const UiPreferences malformed = ParseUiPreferences(
		"run_subfolder=maybe\n"
		"setup_window_width=10\n"
		"setup_window_height=99999\n"
		"setup_form_width=bad\n"
		"setup_only_modified=maybe\n"
		"setup_open_sections=999\n");
	Require(malformed.run_subfolder
			&& malformed.setup_window_width == 800
			&& malformed.setup_window_height == 4320
			&& malformed.setup_form_width == 560.0f
			&& !malformed.setup_only_modified
			&& malformed.setup_open_sections == 7u,
		"malformed values must default or clamp to safe bounds");

	const UiPreferences roundTrip =
		ParseUiPreferences(SerializeUiPreferences(loaded));
	Require(roundTrip.run_subfolder == loaded.run_subfolder
			&& roundTrip.setup_window_width == loaded.setup_window_width
			&& roundTrip.setup_window_height == loaded.setup_window_height
			&& roundTrip.setup_form_width == loaded.setup_form_width
			&& roundTrip.setup_only_modified == loaded.setup_only_modified
			&& roundTrip.setup_open_sections == loaded.setup_open_sections,
		"serialized preferences must parse without loss");

	std::cout << "UiPreferencesTests passed\n";
	return 0;
}
