#pragma once

// One description of every GUI-exposed conversion option.
//
// Design §7.2 requires the Setup screen to cover every option the parser
// registers, and §7.1b requires search and a "Modified" view over them. Both,
// plus the collapsed-header state summaries (P3) and "Copy as command line"
// (§8), are driven from the single table in ConfigModel.cpp so they cannot
// drift apart. The widget for each option still lives in SetupScreen; this
// table owns its identity, help, default and serialization.

#include <functional>
#include <string>
#include <vector>

#include "config.h"

namespace rc_live_ui {

// Rail categories, in the pipeline order of design P1/§7.1b.
// The order here is the order of the form, and the numbers the headers show.
// It follows the decisions rather than the pipeline: what to convert, how to
// search for it, then the two ways of shaping what "close enough" means.
// Colour holds every colour decision - the corrections and the palette match
// and dither that follow from them - rather than splitting the correction from
// the quantization it feeds.
// Objective and dual-frame are not sections of their own - they are the two
// ends of the Algorithm section, because neither is a decision anyone makes
// without the optimizer settings in front of them.
enum class Category {
	Source,
	Algorithm,
	Colour,
	Details,
	RunOutput,
	Count,
};

const char* CategoryTitle(Category category);
const char* CategoryOrdinal(Category category); // "1", "2", "3" or ""

// When a change can take effect, per design P4. Shown as tooltip text in Setup
// and as glyphs in the run dashboard.
enum class Tier {
	Live,     // applies immediately
	Staged,   // needs Apply & Retarget
	Restart,  // cannot change mid-run
};

const char* TierTooltip(Tier tier);

struct OptionDesc {
	std::string id;    // stable key used by the form and the rail
	std::string cli;   // primary command-line name, without the leading slash
	std::string label; // UI label
	std::string help;  // one-line explanation, mirroring config.cpp
	Category category;
	Tier tier;
	bool is_flag; // emitted as "/name" with no value

	// True when the current value differs from the command-line default.
	std::function<bool(const Configuration&)> modified;
	// Token value for the command line; ignored for flags.
	std::function<std::string(const Configuration&)> value;
	// False when the option does not exist for the current configuration and
	// should be hidden entirely (design P2: hide a knob, disable a feature).
	// Empty means always shown. Kept here rather than in the form so that the
	// search, which must not offer a control it cannot draw, applies the same
	// rule as the form does.
	std::function<bool(const Configuration&)> available;
	// Why the option is currently hidden, so a search for it can explain
	// itself instead of reporting no match. Empty when it is never hidden.
	std::string unavailable_hint;

	// False for the two options that identify the job rather than describe the
	// recipe: the input image and the output name. They have permanent controls
	// of their own outside the sections, they are never at a meaningful
	// "default" (an input is required, and the output is derived from it), and
	// counting them as changed settings only inflates every badge. They are
	// still emitted by BuildCommandLine, which has to reproduce the whole job.
	// Mirrors §8's rule that paths are per-job, not per-recipe.
	bool in_form = true;
	// Where that permanent control lives, for the search's empty state.
	std::string location_hint;
};

// Whether `option` currently has a control at all.
bool IsOptionAvailable(const OptionDesc& option, const Configuration& cfg);

// The full table, in form order.
const std::vector<OptionDesc>& AllOptions();

// Lookup by id. Returns nullptr when unknown (a programming error).
const OptionDesc* FindOption(const std::string& id);

// A Configuration carrying exactly the parser's documented defaults. Used as
// the comparison baseline and as the target of "Revert".
Configuration DefaultConfiguration();

// Options differing from their default, in table order.
std::vector<const OptionDesc*> ModifiedOptions(const Configuration& cfg);
int ModifiedCountInCategory(const Configuration& cfg, Category category);

// Case-insensitive match over label, CLI name and help text (§7.1b). An empty
// query matches everything.
bool MatchesQuery(const OptionDesc& option, const std::string& query);

// The full command line reproducing `cfg`, emitting only non-default options
// (§8: a preset is a token string, and stores deltas).
std::string BuildCommandLine(const Configuration& cfg, const char* executable = "RastaConverter");

// The same tokens without a program name, which is the form Configuration
// stores in `command_line` and writes to the "; CmdLine:" header of the .opt
// file. That header is what resume and the Recent browser's Reuse read back, so
// it has to describe the settings the GUI actually ran with.
std::string BuildCommandLineArgs(const Configuration& cfg);

// Human-readable current value, used in collapsed-section summaries.
std::string DisplayValue(const OptionDesc& option, const Configuration& cfg);

//
// ---- Enum label tables, shared between the form and the summaries ---------
//

extern const char* const kDistanceLabels[6];
extern const char* const kDistanceTokens[6];
extern const char* const kGraphicsModeLabels[2];
extern const char* const kGraphicsModeTokens[2];
extern const char* const kPlayfieldWidthLabels[2];
extern const char* const kPlayfieldWidthTokens[2];
extern const char* const kDitherLabels[10];
extern const char* const kDitherTokens[10];
extern const char* const kObjectiveLabels[2];
extern const char* const kObjectiveTokens[2];
extern const char* const kOptimizerLabels[3];
extern const char* const kOptimizerTokens[3];
extern const char* const kInitLabels[4];
extern const char* const kInitTokens[4];
extern const char* const kFilterLabels[6];
extern const char* const kFilterTokens[6];
extern const FREE_IMAGE_FILTER kFilterValues[6];
extern const char* const kDualDitherLabels[6];
extern const char* const kDualDitherTokens[6];
extern const char* const kDetailsModeLabels[3];
extern const char* const kDetailsModeTokens[3];

// Index of `filter` in kFilterValues, or 0.
int FilterIndex(FREE_IMAGE_FILTER filter);
// ANTIC 4 is listed first in the GUI even though ANTIC E is the default.
int GraphicsModeIndex(GraphicsMode mode);
int PlayfieldWidthIndex(PlayfieldWidth width);
// Applies a setup-screen mode change while preserving the ANTIC E-only
// dual-frame choice that ANTIC 4 temporarily overrides.
void ApplyGraphicsModeChoice(Configuration& cfg, GraphicsMode mode,
	bool& antic_e_dual_mode);
// Index of `mode` in kDetailsModeTokens, or 0.
int DetailsModeIndex(const std::string& mode);

// The optimizer history length lives in a process global (`extern int
// solutions`), not in Configuration. These wrap it so the form does not have
// to know that.
int GetSolutions();
void SetSolutions(int value);

} // namespace rc_live_ui
