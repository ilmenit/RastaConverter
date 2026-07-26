#pragma once

// What the run dashboard displays (design §9.8).
//
// A plain snapshot struct so the optimizer can publish everything the UI needs
// in one call, without the UI reaching into evaluator internals and without the
// core depending on ImGui. Every field already exists in the run state; nothing
// here requires new bookkeeping on the hot path.

#include <string>
#include <vector>

struct LiveStats {
	// --- progress ---
	unsigned long long evaluations = 0;
	unsigned long long last_best_evaluation = 0;
	unsigned long long max_evals = 0;
	double rate = 0.0;              // evaluations per second
	double normalized_distance = 0.0;
	double normalized_drift = 0.0;  // active escalation drift, 0 when inactive
	unsigned long long unstuck_after = 0;
	double elapsed_seconds = 0.0;

	// --- mutation operators (design §9.6) ---
	struct MutationStat {
		std::string name;
		unsigned long long count = 0;
	};
	std::vector<MutationStat> mutations;

	// --- island diagnostics, currently never surfaced ---
	unsigned long long accepted = 0;
	unsigned long long global_improvements = 0;
	unsigned long long migrations = 0;
	unsigned long long cache_hits = 0;
	unsigned long long cache_lookups = 0;

	// --- dual frame (design §9.5) ---
	bool dual_mode = false;
	std::string dual_phase;       // human-readable phase name
	bool dual_focus_b = false;
	char dual_display = 'A';      // which frame the viewer shows
	unsigned long long dual_block_steps = 0;   // length of the current block
	unsigned long long dual_block_progress = 0;

	// --- run identity and the restart-only recap (design §9.4) ---
	std::string input_file;
	std::string output_file;
	std::string command_line;
	std::string config_recap;
	int threads = 0;
	int cache_mb = 0;

	// --- lifecycle ---
	bool preprocessing = false;   // still building the target picture
	bool finished = false;
	std::string message;          // latest status line from the converter
	double last_save_seconds_ago = -1.0;
};

// Commands the dashboard can raise back into the run loop. Mirrors the
// GUI_command values the keyboard already produces, plus the explicit
// stop-and-save the design asks for.
enum class LiveCommand {
	None,
	Save,
	StopAndSave,
	Abort,
	ShowA,
	ShowB,
	ShowMix,
};
