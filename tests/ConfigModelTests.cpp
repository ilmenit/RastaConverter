#include "ConfigModel.h"

#include <cstdlib>
#include <iostream>
#include <string>

int solutions = 1;

namespace
{
void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}
}

int main()
{
	using namespace rc_live_ui;

	const Configuration defaults = DefaultConfiguration();
	Require(defaults.graphics_mode == GraphicsMode::AnticE,
		"ANTIC E must remain the GUI default");
	Require(std::string(CategoryTitle(Category::Source))
			== "Source and destination",
		"the first setup section must name both source and destination");
	Require(!AllOptions().empty() && AllOptions().front().id == "graphics_mode",
		"graphics mode must be the first setup option");
	Require(BuildCommandLineArgs(defaults).empty(),
		"default graphics mode must not add a command-line token");

	Configuration antic4 = defaults;
	antic4.graphics_mode = GraphicsMode::Antic4;
	const OptionDesc* option = FindOption("graphics_mode");
	Require(option != nullptr, "graphics mode must be searchable");
	Require(DisplayValue(*option, antic4) == "ANTIC 4 (text mode)",
		"ANTIC 4 must have the requested GUI label");
	Require(BuildCommandLineArgs(antic4) == "/graphics_mode=antic4",
		"ANTIC 4 GUI selection must survive presets and copied command lines");

	Configuration toggled = defaults;
	toggled.height = -1;
	toggled.dual_mode = true;
	bool antic_e_dual_mode = false;
	ApplyGraphicsModeChoice(toggled, GraphicsMode::Antic4,
		antic_e_dual_mode);
	Require(toggled.height == -1 && !toggled.dual_mode,
		"ANTIC 4 selection must preserve Auto height and disable dual mode");
	ApplyGraphicsModeChoice(toggled, GraphicsMode::AnticE,
		antic_e_dual_mode);
	Require(toggled.height == -1 && toggled.dual_mode,
		"returning to ANTIC E must preserve Auto height and restore dual-frame choice");

	toggled.graphics_mode = GraphicsMode::AnticE;
	toggled.height = 181;
	ApplyGraphicsModeChoice(toggled, GraphicsMode::Antic4,
		antic_e_dual_mode);
	Require(toggled.height == 184,
		"explicit ANTIC 4 height must snap to the nearest character row");

	std::cout << "Config model tests passed\n";
	return 0;
}
