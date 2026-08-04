// Dual-mode main loop extracted from RastaDual.cpp
#include "rasta.h"
#include "Interrupt.h"
#include "Program.h"
#include "Evaluator.h"
#include "TargetPicture.h"
#include "debug_log.h"
#include <thread>
#include <mutex>
#include <chrono>

extern const char *program_version;
extern OnOffMap on_off;
extern int solutions;
extern bool quiet;

namespace
{
#if defined(RASTA_DUAL_FULL_CANDIDATE_COPY)
constexpr bool k_dual_transactional_mutation = false;
#else
constexpr bool k_dual_transactional_mutation = true;
#endif
}

void RastaConverter::MainLoopDual()
{
	Message("Dual-mode optimization started.");
	DBG_PRINT("[RASTA] MainLoopDual: start");

	// Mark optimization start time for statistics (seconds since start)
	m_eval_gstate.m_time_start = time(NULL);

	// Critical initialization that was missing!
	FindPossibleColors();
	Init();

	PrecomputeDualTables();
	DBG_PRINT("[RASTA] MainLoopDual: tables ready");

	// Prepare input-based targets for post-bootstrap optimization
	PrecomputeInputTargets();

	// Dedicated evaluator for preview/initial calculations during bootstrap
	m_eval_gstate.m_dual_phase.store(EvalGlobalState::DUAL_PHASE_BOOTSTRAP_A, std::memory_order_relaxed);

	// Force solutions=1 during bootstrap phase for effective optimization even with short bootstrap
	// If bootstrap is shorter than /s, the history never fills up and optimization doesn't work properly
	// NOTE: With /continue, if user changed /s, ProcessCmdLine already updated global solutions before MainLoopDual()
	// So original_solutions captures the NEW value (user's desired value), not the saved value
	const int original_solutions = solutions;
	const int bootstrap_solutions = 1;
	solutions = bootstrap_solutions;

	// CRITICAL: Initialize optimizer history to size 1 before any evaluations
	// If history is empty, the first evaluation would use evaluators' m_solutions (original value)
	// which could be wrong. Initialize it explicitly to bootstrap_solutions=1.
	// Note: We'll reseed with actual cost after first evaluation, but this ensures correct size.
	{
		std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
		if (m_eval_gstate.m_previous_results.empty() || m_eval_gstate.m_previous_results.size() != (size_t)bootstrap_solutions) {
			// History is empty or wrong size - resize to bootstrap size
			// Use current best_result if available, otherwise will be reseeded after first evaluation
			const double savedBest = m_eval_gstate.m_best_result.load(std::memory_order_relaxed);
			double seed_cost = (savedBest != DBL_MAX) ? savedBest : 0.0;
			m_eval_gstate.m_previous_results.resize(bootstrap_solutions, seed_cost);
			m_eval_gstate.m_previous_results_index = 0;
			if (m_eval_gstate.m_current_cost == DBL_MAX) {
				m_eval_gstate.m_current_cost = seed_cost;
				m_eval_gstate.m_cost_max = seed_cost;
			}
			m_eval_gstate.m_N = bootstrap_solutions;
		}
	}

	// Evaluator owns a 32K-bucket instruction cache (~526 KiB).  Keeping this
	// long-lived bootstrap instance on MainLoopDual's stack, alongside the
	// scoped baseline evaluators below, made the function's frame exceed
	// Windows' default 1 MiB stack reserve.  It is setup-only state, so heap
	// ownership is both natural and keeps worker/main-thread stacks small.
	auto bootstrapEvalOwner = std::make_unique<Evaluator>();
	Evaluator& bootstrapEval = *bootstrapEvalOwner;
	bootstrapEval.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, bootstrap_solutions, cfg.initial_seed+101, cfg.cache_size);

	// Prepare common UI rate tracking variables (used in both paths)
	auto last_rate_check_tp = std::chrono::steady_clock::now();
	unsigned long long last_eval = m_eval_gstate.m_evaluations;

	// Fast path for /continue in dual mode: skip bootstrapping A/B and jump straight to alternating
	bool skip_bootstrap = false;
	if (cfg.continue_processing && cfg.dual_mode
		&& !m_eval_gstate.m_best_pic.raster_lines.empty()
		&& !m_best_pic_B.raster_lines.empty())
	{
		skip_bootstrap = true;
		DBG_PRINT("[RASTA] /continue detected - skipping dual bootstrap, initializing from saved A/B");

		// Re-evaluate A to populate created/targets and sprites for UI/state
		{
			raster_picture a = m_eval_gstate.m_best_pic;
			bootstrapEval.RecachePicture(&a);
			std::vector<const line_cache_result*> resA(m_height, nullptr);
			(void)bootstrapEval.ExecuteRasterProgram(&a, resA.data());
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			UpdateCreatedFromResults(resA, m_eval_gstate.m_created_picture);
			UpdateTargetsFromResults(resA, m_eval_gstate.m_created_picture_targets);
			memcpy(&m_eval_gstate.m_sprites_memory, &bootstrapEval.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
			m_eval_gstate.m_initialized = true;
			m_eval_gstate.m_update_initialized = true;
			m_eval_gstate.m_condvar_update.notify_one();
		}

		// Defer fixed-frame pointer wiring until after reseed passes

		// (pointer wiring moved below, after reseed passes to avoid stale pointers)
		// Re-evaluate B similarly
		{
			raster_picture b = m_best_pic_B;
			bootstrapEval.RecachePicture(&b);
			std::vector<const line_cache_result*> resB(m_height, nullptr);
			(void)bootstrapEval.ExecuteRasterProgram(&b, resB.data());
			UpdateCreatedFromResults(resB, m_created_picture_B);
			UpdateTargetsFromResults(resB, m_created_picture_targets_B);
			memcpy(&m_sprites_memory_B, &bootstrapEval.GetSpritesMemory(), sizeof m_sprites_memory_B);
		}

		// Two-pass baseline reseed: first B with A fixed, then A with B fixed
		// Note: solutions is still 1 here (bootstrap mode), will be restored before alternating phase
		{
			auto baseEvBOwner = std::make_unique<Evaluator>();
			Evaluator& baseEvB = *baseEvBOwner;
			baseEvB.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, bootstrap_solutions, cfg.initial_seed + 2222, cfg.cache_size);
			baseEvB.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				 m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				 m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				 m_input_target_y.data(), m_input_target_u.data(), m_input_target_v.data());
			baseEvB.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_input_target_y8.data(), m_input_target_u8.data(), m_input_target_v8.data());
			baseEvB.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);
			std::vector<const line_cache_result*> resB(m_height, nullptr);
			std::vector<const unsigned char*> fixedARows((size_t)m_height, (const unsigned char*)nullptr);
			for (int y = 0; y < m_height; ++y) fixedARows[y] = (y < (int)m_eval_gstate.m_created_picture.size() && !m_eval_gstate.m_created_picture[y].empty()) ? m_eval_gstate.m_created_picture[y].data() : nullptr;
			raster_picture bprog = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
			baseEvB.RecachePicture(&bprog);
			(void)baseEvB.ExecuteRasterProgramDual(&bprog, resB.data(), fixedARows, /*mutateB*/true);
			UpdateCreatedFromResults(resB, m_created_picture_B);
			UpdateTargetsFromResults(resB, m_created_picture_targets_B);
		}

		// Second pass: A with B fixed, seed baseline
		{
			auto baseEvOwner = std::make_unique<Evaluator>();
			Evaluator& baseEv = *baseEvOwner;
			baseEv.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, bootstrap_solutions, cfg.initial_seed + 1337, cfg.cache_size);
			baseEv.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				m_input_target_y.data(), m_input_target_u.data(), m_input_target_v.data());
			baseEv.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_input_target_y8.data(), m_input_target_u8.data(), m_input_target_v8.data());
			baseEv.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);
			std::vector<const line_cache_result*> tmpRes(m_height, nullptr);
			std::vector<const unsigned char*> otherRows((size_t)m_height, (const unsigned char*)nullptr);
			for (int y = 0; y < m_height; ++y) otherRows[y] = (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) ? m_created_picture_B[y].data() : nullptr;
			raster_picture a = m_eval_gstate.m_best_pic;
			baseEv.RecachePicture(&a);
			distance_accum_t baseCost = baseEv.ExecuteRasterProgramDual(&a, tmpRes.data(), otherRows, /*mutateB*/false);
			{
				std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
				double C = (double)baseCost;
				// Use bootstrap_solutions for history size during reseed (will be restored to original_solutions before alternating)
				size_t hist_size = (bootstrap_solutions > 0) ? (size_t)bootstrap_solutions : 1ULL;
				a.uncache_insns();
				m_eval_gstate.m_best_pic = a;  // Store re-evaluated picture that produced baseline cost
				// CRITICAL: Reset m_best_result to dual baseline cost C measured against input-based target.
				// If resuming from saved state, previous m_best_result may be from quantized target phase.
				// Alternating phase uses dual evaluation against original high-color input (m_input_target_*),
				// which is INCOMPATIBLE with quantized target metrics - different scales, cannot compare.
				m_eval_gstate.m_best_result = C;
				m_eval_gstate.m_previous_results.assign(hist_size, C);
				m_eval_gstate.m_previous_results_index = 0;
				m_eval_gstate.m_current_cost = C;
				m_eval_gstate.m_cost_max = C;
				m_eval_gstate.m_N = (int)hist_size;
				m_eval_gstate.m_last_best_evaluation.store(m_eval_gstate.m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
				m_eval_gstate.m_current_norm_drift = 0.0;
				m_eval_gstate.m_initialized = true;
				m_needs_history_reconfigure = false;
				m_eval_gstate.m_update_improvement = true;
				UpdateCreatedFromResults(tmpRes, m_eval_gstate.m_created_picture);
				UpdateTargetsFromResults(tmpRes, m_eval_gstate.m_created_picture_targets);
			}
			m_eval_gstate.m_condvar_update.notify_one();
			Message("[Dual] Baseline seeded to input-target cost");
		}

		// Initialize fixed frame pointer buffers for alternating phase (use B as initial fixed)
		m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
		m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
		for (int i = 0; i < 2; ++i) {
			m_eval_gstate.m_dual_fixed_rows_buf[i].resize(m_height);
		}
		{
			int init_idx = 0;
			for (int y = 0; y < m_height; ++y) {
				if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
					m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_created_picture_B[y].data();
				} else {
					m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
					m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
				}
			}
			m_eval_gstate.m_dual_fixed_rows_active_index.store(init_idx, std::memory_order_release);
			m_eval_gstate.m_dual_fixed_frame_is_A.store(false, std::memory_order_relaxed);
		}

		// Immediately show frame in dual mode
		if (!quiet) { m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); }

		// Prepare alternating phase state
		m_eval_gstate.m_dual_stage_focus_B.store(false, std::memory_order_relaxed);
		m_eval_gstate.m_dual_stage_counter.store(0, std::memory_order_relaxed);
		m_eval_gstate.m_dual_phase.store(EvalGlobalState::DUAL_PHASE_ALTERNATING, std::memory_order_relaxed);
	}

	if (!skip_bootstrap) {
	// Bootstrap A using single-frame evaluation for first_dual_steps
	raster_picture bestA = m_eval_gstate.m_best_pic;
	std::vector<const line_cache_result*> resultsA(m_height, nullptr);
	DBG_PRINT("[RASTA] Bootstrap A: calling ExecuteRasterProgram ...");
	bootstrapEval.RecachePicture(&bestA);
	distance_accum_t bestCostA = bootstrapEval.ExecuteRasterProgram(&bestA, resultsA.data());
		DBG_PRINT("[RASTA] Bootstrap A initial cost=%g", (double)bestCostA);
		{
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			bestA.uncache_insns();
			m_eval_gstate.m_best_pic = bestA;
			m_eval_gstate.m_best_result = (double)bestCostA;
			// Reseed history with actual bootstrap cost (history was initialized to size 1 above)
			m_eval_gstate.m_previous_results.assign(bootstrap_solutions, (double)bestCostA);
			m_eval_gstate.m_previous_results_index = 0;
			m_eval_gstate.m_current_cost = (double)bestCostA;
			m_eval_gstate.m_cost_max = (double)bestCostA;
			m_eval_gstate.m_N = bootstrap_solutions;
			if (m_eval_gstate.m_last_best_evaluation == 0ULL)
				m_eval_gstate.m_last_best_evaluation.store(m_eval_gstate.m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
		UpdateCreatedFromResults(resultsA, m_eval_gstate.m_created_picture);
		UpdateTargetsFromResults(resultsA, m_eval_gstate.m_created_picture_targets);
		memcpy(&m_eval_gstate.m_sprites_memory, &bootstrapEval.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
		m_eval_gstate.m_initialized = true;
		m_eval_gstate.m_update_initialized = true;
		m_eval_gstate.m_condvar_update.notify_one();
	}

	// Multi-threaded bootstrap for A
	const unsigned long long targetE_A = m_eval_gstate.m_evaluations + cfg.first_dual_steps;
	int boot_threads = std::max(1, cfg.threads);
	std::vector<std::thread> bootWorkersA;
	bootWorkersA.reserve(boot_threads);
	for (int tid = 0; tid < boot_threads; ++tid) {
		bootWorkersA.emplace_back([this, tid, targetE_A, &bestA, &bestCostA]() {
			Evaluator& ev = m_evaluators[tid];
			std::vector<const line_cache_result*> line_results(m_height, nullptr);
			raster_picture localBest = bestA;
			// Keep a local view of accepted cost to detect external improvements
			double localAcceptedCost; {
				std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
				localAcceptedCost = m_eval_gstate.m_best_result;
			}
			while (true) {
				// Early stop check (lock free read)
				if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_A) break;
				// Sync local baseline if another thread improved
				if (m_eval_gstate.m_best_result < localAcceptedCost) {
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						localBest = m_eval_gstate.m_best_pic;
						localAcceptedCost = m_eval_gstate.m_best_result;
					}
				}
				raster_picture cand = localBest;
				ev.MutateRasterProgram(&cand);
				distance_accum_t cost = ev.ExecuteRasterProgram(&cand, line_results.data());
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_A) break;
					++m_eval_gstate.m_evaluations;
					Evaluator::AcceptanceOutcome out = ev.ApplyAcceptanceCore((double)cost, false);
					ev.RecordMutationOutcome(out, (double)cost);
					// Progress local baseline even if not a global improvement
					if (out.accepted && !out.improved) {
						localBest = cand;
					}
					if (out.improved) {
						m_eval_gstate.m_last_best_evaluation.store(m_eval_gstate.m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
						m_eval_gstate.m_best_result = (double)cost;
						m_eval_gstate.m_best_pic = cand; m_eval_gstate.m_best_pic.uncache_insns();
						UpdateCreatedFromResults(line_results, m_eval_gstate.m_created_picture);
						UpdateTargetsFromResults(line_results, m_eval_gstate.m_created_picture_targets);
						memcpy(&m_eval_gstate.m_sprites_memory, &ev.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
						m_eval_gstate.m_update_improvement = true;
						m_eval_gstate.m_condvar_update.notify_one();
						// Update local baseline to the newly accepted cand
						localBest = cand;
						localAcceptedCost = (double)cost;
					}
					if (m_eval_gstate.m_save_period > 0 && (m_eval_gstate.m_evaluations % (unsigned long long)m_eval_gstate.m_save_period) == 0ULL) {
						m_eval_gstate.m_update_autosave = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
				}
			}
		});
	}

	// UI loop for bootstrap A
	last_rate_check_tp = std::chrono::steady_clock::now();
	last_eval = m_eval_gstate.m_evaluations;
	if (!quiet) { m_dual_display = DualDisplayMode::A; }
	while (!m_eval_gstate.m_finished && m_eval_gstate.m_evaluations < targetE_A) {
		// An interrupt is a stop here too; m_finished is what every dual worker
		// already watches, so raising it unwinds the same way the Stop button
		// does and the caller still saves.
		if (interrupts::StopRequested()) {
			Message("Interrupted - saving.");
			m_eval_gstate.m_finished = true;
			break;
		}
		if (!quiet) {
			switch (gui.NextFrame()) {
				case GUI_command::SAVE: SaveBestSolution(); break;
				case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
				case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
				case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); PublishLiveStats(false, false); break;
				default: break;
			}
		}
		auto next_rate_check_tp = std::chrono::steady_clock::now();
		double secs = std::chrono::duration<double>(next_rate_check_tp - last_rate_check_tp).count();
		if (secs > 0.25) {
			m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) / secs;
			last_rate_check_tp = next_rate_check_tp;
			last_eval = m_eval_gstate.m_evaluations;
			if (cfg.save_period == -1) {
				using namespace std::literals::chrono_literals;
				auto now = std::chrono::steady_clock::now();
				if ( now - m_previous_save_time > 30s ) { m_previous_save_time = now; SaveBestSolution(); }
			}
			else if (m_eval_gstate.m_update_autosave) { m_eval_gstate.m_update_autosave = false; SaveBestSolution(); }
			// Periodic preview refresh of A for GUI
			if (!quiet) {
				raster_picture previewA;
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					previewA = m_eval_gstate.m_best_pic;
				}
				bootstrapEval.RecachePicture(&previewA);
				std::vector<const line_cache_result*> tickResultsA(m_height, nullptr);
				(void)bootstrapEval.ExecuteRasterProgram(&previewA, tickResultsA.data());
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					UpdateCreatedFromResults(tickResultsA, m_eval_gstate.m_created_picture);
					UpdateTargetsFromResults(tickResultsA, m_eval_gstate.m_created_picture_targets);
					memcpy(&m_eval_gstate.m_sprites_memory, &bootstrapEval.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
					m_eval_gstate.m_update_improvement = true;
					m_eval_gstate.m_condvar_update.notify_one();
				}
				ShowLastCreatedPictureDual();
			}
			if (!quiet) {
				ShowMutationStats();
				PublishLiveStats(false, false);
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	for (auto &t : bootWorkersA) { if (t.joinable()) t.join(); }

	// Bootstrap B
	m_eval_gstate.m_dual_phase.store(EvalGlobalState::DUAL_PHASE_BOOTSTRAP_B, std::memory_order_relaxed);
	if (cfg.after_dual_steps == "copy") {
		// Copy the latest best A into B
		{
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			m_best_pic_B = m_eval_gstate.m_best_pic;
		}
		m_best_pic_B.uncache_insns(); m_genB++; // copy program
		m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
		m_created_picture_B = m_eval_gstate.m_created_picture; // copy created for seed
		m_created_picture_targets_B = m_eval_gstate.m_created_picture_targets; // copy targets
		memcpy(&m_sprites_memory_B, &m_eval_gstate.m_sprites_memory, sizeof m_sprites_memory_B); // copy sprites
		// REMOVED: Old snapshot system - using efficient fixed frame system in alternation phase
		m_eval_gstate.m_dual_bootstrap_b_copied.store(true, std::memory_order_relaxed);
	} else {
		m_eval_gstate.m_dual_bootstrap_b_copied.store(false, std::memory_order_relaxed);
		// fresh random init for B and run single-frame for first_dual_steps evaluations
		m_best_pic_B = raster_picture(m_height);
		CreateRandomRasterPicture(&m_best_pic_B);
		std::vector<const line_cache_result*> resultsB(m_height, nullptr);
		DBG_PRINT("[RASTA] Bootstrap B: calling ExecuteRasterProgram ...");
		bootstrapEval.RecachePicture(&m_best_pic_B);
		distance_accum_t bestCostB = bootstrapEval.ExecuteRasterProgram(&m_best_pic_B, resultsB.data());
		DBG_PRINT("[RASTA] Bootstrap B initial cost=%g", (double)bestCostB);
		UpdateCreatedFromResults(resultsB, m_created_picture_B);
		UpdateTargetsFromResults(resultsB, m_created_picture_targets_B);
		memcpy(&m_sprites_memory_B, &bootstrapEval.GetSpritesMemory(), sizeof m_sprites_memory_B);

		// Reset optimizer state (LAHC/DLAS) for B bootstrap baseline so acceptance is consistent
		// Use bootstrap_solutions=1 during bootstrap for effective optimization even with short bootstrap
		{
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			m_eval_gstate.m_previous_results.clear();
			m_eval_gstate.m_previous_results.resize(bootstrap_solutions, (double)bestCostB);
			m_eval_gstate.m_previous_results_index = 0;
			m_eval_gstate.m_current_cost = (double)bestCostB;
			m_eval_gstate.m_cost_max = (double)bestCostB;
			m_eval_gstate.m_N = bootstrap_solutions;
		}

		// Multi-threaded bootstrap for B using the same evaluators
		const unsigned long long targetE_B = m_eval_gstate.m_evaluations + cfg.first_dual_steps;
		std::vector<std::thread> bootWorkersB;
		bootWorkersB.reserve(boot_threads);
		for (int tid = 0; tid < boot_threads; ++tid) {
			bootWorkersB.emplace_back([this, tid, targetE_B, &bestCostB]() {
				Evaluator& ev = m_evaluators[tid];
				std::vector<const line_cache_result*> line_results(m_height, nullptr);
				raster_picture localB = m_best_pic_B;
				// Keep a local view of accepted cost to detect external improvements (global best)
				double localAcceptedCost; {
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					localAcceptedCost = m_eval_gstate.m_best_result;
				}
				// Track per-B generation to adopt newer B best across threads
				unsigned long long localGenB = m_eval_gstate.m_dual_generation_B.load(std::memory_order_relaxed);
				while (true) {
					if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_B) break;
					// Sync local baseline if another thread improved GLOBAL best (rare in B bootstrap)
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						if (m_eval_gstate.m_best_result < localAcceptedCost) {
							localB = m_best_pic_B;
							localAcceptedCost = m_eval_gstate.m_best_result;
						}
					}
					// Also adopt latest B best when another thread found a per-B improvement
					unsigned long long genB = m_eval_gstate.m_dual_generation_B.load(std::memory_order_acquire);
					if (genB != localGenB) {
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						localB = m_best_pic_B;
						localGenB = genB;
					}
					raster_picture cand = localB;
					ev.MutateRasterProgram(&cand);
					distance_accum_t cost = ev.ExecuteRasterProgram(&cand, line_results.data());
					{
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_B) break;
						++m_eval_gstate.m_evaluations;
						Evaluator::AcceptanceOutcome out = ev.ApplyAcceptanceCore((double)cost, false);
						ev.RecordMutationOutcome(out, (double)cost);
						if (out.accepted && !out.improved) {
							localB = cand;
						}
						if (out.improved) {
							m_eval_gstate.m_last_best_evaluation.store(m_eval_gstate.m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							UpdateCreatedFromResults(line_results, m_created_picture_B);
							UpdateTargetsFromResults(line_results, m_created_picture_targets_B);
							memcpy(&m_sprites_memory_B, &ev.GetSpritesMemory(), sizeof m_sprites_memory_B);
							m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
							m_eval_gstate.m_update_improvement = true;
							m_eval_gstate.m_condvar_update.notify_one();
							// Update local baseline to the newly accepted cand
							localB = m_best_pic_B;
							bestCostB = (double)cost;
							localGenB = m_eval_gstate.m_dual_generation_B.load(std::memory_order_relaxed);
						}
						// Even if not a new GLOBAL best, record B's own best for UI/future and resync threads
						else if ((double)cost < bestCostB) {
							bestCostB = (double)cost;
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							UpdateCreatedFromResults(line_results, m_created_picture_B);
							UpdateTargetsFromResults(line_results, m_created_picture_targets_B);
							memcpy(&m_sprites_memory_B, &ev.GetSpritesMemory(), sizeof m_sprites_memory_B);
							m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
							m_eval_gstate.m_update_improvement = true;
							m_eval_gstate.m_condvar_update.notify_one();
							// Adopt locally
							localB = m_best_pic_B;
							localGenB = m_eval_gstate.m_dual_generation_B.load(std::memory_order_relaxed);
						}
						if (m_eval_gstate.m_save_period > 0 && (m_eval_gstate.m_evaluations % (unsigned long long)m_eval_gstate.m_save_period) == 0ULL) {
							m_eval_gstate.m_update_autosave = true;
							m_eval_gstate.m_condvar_update.notify_one();
						}
					}
				}
			});
		}

		// UI loop for bootstrap B
		last_rate_check_tp = std::chrono::steady_clock::now();
		last_eval = m_eval_gstate.m_evaluations;
		if (!quiet) { m_dual_display = DualDisplayMode::B; }
		while (!m_eval_gstate.m_finished && m_eval_gstate.m_evaluations < targetE_B) {
		// An interrupt is a stop here too; m_finished is what every dual worker
		// already watches, so raising it unwinds the same way the Stop button
		// does and the caller still saves.
		if (interrupts::StopRequested()) {
			Message("Interrupted - saving.");
			m_eval_gstate.m_finished = true;
			break;
		}
			if (!quiet) {
				switch (gui.NextFrame()) {
					case GUI_command::SAVE: SaveBestSolution(); break;
					case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
					case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
					case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
					case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
					case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); PublishLiveStats(false, false); gui.Present(); break;
					default: break;
				}
			}
			auto next_rate_check_tp = std::chrono::steady_clock::now();
			double secs = std::chrono::duration<double>(next_rate_check_tp - last_rate_check_tp).count();
			if (secs > 0.25) {
				m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) / secs;
				last_rate_check_tp = next_rate_check_tp;
				last_eval = m_eval_gstate.m_evaluations;
				if (cfg.save_period == -1) {
					using namespace std::literals::chrono_literals;
					auto now = std::chrono::steady_clock::now();
					if ( now - m_previous_save_time > 30s ) { m_previous_save_time = now; SaveBestSolution(); }
				}
				else if (m_eval_gstate.m_update_autosave) { m_eval_gstate.m_update_autosave = false; SaveBestSolution(); }
				// Periodic preview refresh of B for GUI
				if (!quiet) {
					raster_picture previewB;
					{
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						previewB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
					}
					bootstrapEval.RecachePicture(&previewB);
					std::vector<const line_cache_result*> tickResultsB(m_height, nullptr);
					(void)bootstrapEval.ExecuteRasterProgram(&previewB, tickResultsB.data());
					{
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						UpdateCreatedFromResults(tickResultsB, m_created_picture_B);
						UpdateTargetsFromResults(tickResultsB, m_created_picture_targets_B);
						memcpy(&m_sprites_memory_B, &bootstrapEval.GetSpritesMemory(), sizeof m_sprites_memory_B);
						m_eval_gstate.m_update_improvement = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
					ShowLastCreatedPictureDual();
				}
				if (!quiet) {
					ShowMutationStats();
					PublishLiveStats(false, false);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		for (auto &t : bootWorkersB) { if (t.joinable()) t.join(); }
		}
		// (pointer wiring moved below, after reseed passes to avoid stale pointers)

		// Two-pass baseline reseed after bootstrap
		{
			auto baseEvBOwner = std::make_unique<Evaluator>();
			Evaluator& baseEvB = *baseEvBOwner;
			baseEvB.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, bootstrap_solutions, cfg.initial_seed + 2222, cfg.cache_size);
			baseEvB.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				 m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				 m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				 m_input_target_y.data(), m_input_target_u.data(), m_input_target_v.data());
			baseEvB.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_input_target_y8.data(), m_input_target_u8.data(), m_input_target_v8.data());
			baseEvB.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);
			std::vector<const line_cache_result*> resB(m_height, nullptr);
			std::vector<const unsigned char*> fixedARows((size_t)m_height, (const unsigned char*)nullptr);
			for (int y = 0; y < m_height; ++y) fixedARows[y] = (y < (int)m_eval_gstate.m_created_picture.size() && !m_eval_gstate.m_created_picture[y].empty()) ? m_eval_gstate.m_created_picture[y].data() : nullptr;
			raster_picture bprog = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
			baseEvB.RecachePicture(&bprog);
			(void)baseEvB.ExecuteRasterProgramDual(&bprog, resB.data(), fixedARows, /*mutateB*/true);
			UpdateCreatedFromResults(resB, m_created_picture_B);
			UpdateTargetsFromResults(resB, m_created_picture_targets_B);
		}

		// Second pass: A with B fixed, seed baseline
		{
			auto baseEvOwner = std::make_unique<Evaluator>();
			Evaluator& baseEv = *baseEvOwner;
			baseEv.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, bootstrap_solutions, cfg.initial_seed + 1337, cfg.cache_size);
			baseEv.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				m_input_target_y.data(), m_input_target_u.data(), m_input_target_v.data());
			baseEv.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_input_target_y8.data(), m_input_target_u8.data(), m_input_target_v8.data());
			baseEv.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);
			std::vector<const line_cache_result*> tmpRes(m_height, nullptr);
			std::vector<const unsigned char*> otherRows((size_t)m_height, (const unsigned char*)nullptr);
			for (int y = 0; y < m_height; ++y) otherRows[y] = (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) ? m_created_picture_B[y].data() : nullptr;
			raster_picture a = m_eval_gstate.m_best_pic;
			baseEv.RecachePicture(&a);
			distance_accum_t baseCost = baseEv.ExecuteRasterProgramDual(&a, tmpRes.data(), otherRows, /*mutateB*/false);
			{
				std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
				double C = (double)baseCost;
				// Use bootstrap_solutions for history size during reseed (will be restored to original_solutions before alternating)
				size_t hist_size = (bootstrap_solutions > 0) ? (size_t)bootstrap_solutions : 1ULL;
				a.uncache_insns();
				m_eval_gstate.m_best_pic = a;
				// CRITICAL: Reset m_best_result to dual baseline cost C measured against input-based target.
				// Bootstrap phase accumulated m_best_result using single-frame evaluation against
				// palette-quantized target (m_picture), which is INCOMPATIBLE with alternating phase
				// that uses dual evaluation against original high-color input (m_input_target_*).
				// These metrics have different scales and cannot be compared, so we must reset.
				m_eval_gstate.m_best_result = C;
				m_eval_gstate.m_previous_results.assign(hist_size, C);
				m_eval_gstate.m_previous_results_index = 0;
				m_eval_gstate.m_current_cost = C;
				m_eval_gstate.m_cost_max = C;
				m_eval_gstate.m_N = (int)hist_size;
				m_eval_gstate.m_last_best_evaluation.store(m_eval_gstate.m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
				m_eval_gstate.m_current_norm_drift = 0.0;
				m_eval_gstate.m_initialized = true;
				m_needs_history_reconfigure = false;
				m_eval_gstate.m_update_improvement = true;
				UpdateCreatedFromResults(tmpRes, m_eval_gstate.m_created_picture);
				UpdateTargetsFromResults(tmpRes, m_eval_gstate.m_created_picture_targets);
			}
			m_eval_gstate.m_condvar_update.notify_one();
		}

		// Initialize fixed frame pointer buffers for alternating phase (use B as initial fixed)
		m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
		m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
		for (int i = 0; i < 2; ++i) {
			m_eval_gstate.m_dual_fixed_rows_buf[i].resize(m_height);
		}
		{
			int init_idx = 0;
			for (int y = 0; y < m_height; ++y) {
				if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
					m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_created_picture_B[y].data();
				} else {
					m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
					m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
				}
			}
			m_eval_gstate.m_dual_fixed_rows_active_index.store(init_idx, std::memory_order_release);
			m_eval_gstate.m_dual_fixed_frame_is_A.store(false, std::memory_order_relaxed);
		}

		// Immediately show initial frame to ensure output is visible in dual mode (only if we didn't already)
		if (!skip_bootstrap && !quiet) {
			m_dual_display = DualDisplayMode::MIX;
			ShowLastCreatedPictureDual();
		}
	}

	// CRITICAL: Restore original solutions value before alternating phase
	// Bootstrap used solutions=1 for effective optimization even with short bootstrap
	// Now restore the user's configured /s value for alternating phase
	// Must restore BEFORE reconfigureAcceptanceHistory() since it uses the global solutions variable
	solutions = original_solutions;

	// If resuming with changed optimizer/solutions/distance, reconfigure history now
	// (after restoring solutions so it uses the correct value)
	bool history_was_reconfigured = false;
	if (cfg.continue_processing && m_needs_history_reconfigure) {
		reconfigureAcceptanceHistory();
		// Check if reconfigureAcceptanceHistory() actually reseeded (it can return early without reseeding)
		// It sets m_needs_history_reconfigure = false when done, and reseeds to target_len based on solutions
		size_t expected_size = (solutions > 0) ? (size_t)solutions : 1ULL;
		{
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			history_was_reconfigured = (m_eval_gstate.m_previous_results.size() == expected_size && !m_needs_history_reconfigure);
		}
	}
	DBG_PRINT("[RASTA] Restored solutions=%d for alternating phase (was %d during bootstrap)", solutions, bootstrap_solutions);

	// Reseed optimizer history with restored solutions size
	// History was size 1 during bootstrap, now expand to user's configured /s value
	// Skip if reconfigureAcceptanceHistory() already did this (it resizes to target_len based on solutions)
	if (!history_was_reconfigured) {
		std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
		double current_baseline = m_eval_gstate.m_best_result;
		size_t new_hist_size = (solutions > 0) ? (size_t)solutions : 1ULL;
		m_eval_gstate.m_previous_results.assign(new_hist_size, current_baseline);
		m_eval_gstate.m_previous_results_index = 0;
		m_eval_gstate.m_N = (int)new_hist_size;
		// Update cost_max to match baseline (history is all filled with baseline)
		m_eval_gstate.m_cost_max = current_baseline;
	}

	// CRITICAL: Clear all caches before alternating phase
	// Bootstrap phase used single-frame evaluation against palette-quantized target (m_picture)
	// Alternating phase uses dual evaluation against original high-color input (m_input_target_*)
	// These are incompatible metrics, so all caches must be cleared to avoid stale results
	DBG_PRINT("[RASTA] Clearing all caches before alternating phase (target metric changed)");
	int num_workers = std::max(1, cfg.threads);
	{
		std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
		// Clear all evaluator caches
		for (int tid = 0; tid < num_workers; ++tid) {
			m_evaluators[tid].ClearAllCaches();
		}
		// Invalidate cache_key pointers in global raster_picture objects
		// (cache_key points into evaluator's m_insn_seq_cache which we just cleared)
		m_eval_gstate.m_best_pic.uncache_insns();
		m_best_pic_B.uncache_insns();
	}

	// Loop until finished/max_evals using worker threads and snapshots
	std::vector<std::thread> workers;
	workers.reserve(num_workers);

	// Sync each worker evaluator's local best to global best after reseed to prevent legacy mode acceptance guard mismatch
	// NOTE: m_best_result was already reset to dual baseline cost C (line 537) measured against input-based target
	// This is correct because bootstrap used incompatible metric (quantized palette vs original input)
	{
		std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
		for (int tid = 0; tid < num_workers; ++tid) {
			m_evaluators[tid].SyncLocalBestToGlobal();
		}
	}

	// Initialize atomic stage coordination for alternation
	m_eval_gstate.m_dual_stage_focus_B.store(false, std::memory_order_relaxed);
	m_eval_gstate.m_dual_stage_counter.store(0, std::memory_order_relaxed);
	m_eval_gstate.m_dual_phase.store(EvalGlobalState::DUAL_PHASE_ALTERNATING, std::memory_order_relaxed);

	for (int tid = 0; tid < num_workers; ++tid) {
		workers.emplace_back([this, tid]() {
			// Use long-lived evaluator to preserve legacy acceptance state
			Evaluator& ev = m_evaluators[tid];
			// Configure dual input-based targets for alternating phase
			ev.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				 m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				 m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				 m_input_target_y.data(), m_input_target_u.data(), m_input_target_v.data());
			ev.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_input_target_y8.data(), m_input_target_u8.data(), m_input_target_v8.data());
			ev.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);

			const bool islandMode = m_eval_gstate.m_optimizer != EvalGlobalState::OPT_LEGACY;

			// Local working state for this thread (NO sharing between threads)
			std::vector<const line_cache_result*> line_results(m_height, nullptr);
			DualOptimizerState<raster_picture> islandState;
			raster_picture currentA;
			raster_picture currentB;
			std::vector<color_index_line> currentRowsA;
			std::vector<color_index_line> currentRowsB;
			std::vector<line_target> currentTargetsA;
			std::vector<line_target> currentTargetsB;
			std::vector<const unsigned char*> rowPointersA((size_t)m_height, nullptr);
			std::vector<const unsigned char*> rowPointersB((size_t)m_height, nullptr);
			sprites_memory_t currentSpritesA{};
			sprites_memory_t currentSpritesB{};
			double localAcceptedCost = DBL_MAX;
			unsigned long long observedBestVersion = 0;
			unsigned long long localIterations = 0;
			unsigned long long localCandidateFullCopies = 0;
			unsigned long long localUndoCandidates = 0;
			unsigned long long localUndoLineSnapshots = 0;
			unsigned long long localUndoRestores = 0;
			unsigned long long localPublicationCopyEvents = 0;
			unsigned long long localPublicationCopyNs = 0;
			unsigned long long localMigrationCopyEvents = 0;
			unsigned long long localMigrationCopyNs = 0;
			RasterMutationTransaction mutationTransaction;
			constexpr unsigned long long migrationCheckInterval = 256;
			{
				std::unique_lock<std::mutex> stateLock{m_eval_gstate.m_mutex};
				currentA = m_eval_gstate.m_best_pic;
				currentB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
				currentRowsA = m_eval_gstate.m_created_picture;
				currentRowsB = m_created_picture_B;
				currentTargetsA = m_eval_gstate.m_created_picture_targets;
				currentTargetsB = m_created_picture_targets_B;
				memcpy(&currentSpritesA, &m_eval_gstate.m_sprites_memory, sizeof currentSpritesA);
				memcpy(&currentSpritesB, &m_sprites_memory_B, sizeof currentSpritesB);
				localAcceptedCost = m_eval_gstate.m_best_result.load(std::memory_order_relaxed);
				observedBestVersion = m_eval_gstate.m_best_state_version.load(std::memory_order_relaxed);
			}
			islandState.Initialize(currentA, currentB, localAcceptedCost,
				static_cast<std::size_t>(std::max(solutions, 1)));
			auto rebuildRowPointers = [this](const std::vector<color_index_line>& rows,
				std::vector<const unsigned char*>& pointers) {
				pointers.resize((size_t)m_height);
				for (int y = 0; y < m_height; ++y)
					pointers[y] = y < (int)rows.size() && !rows[y].empty() ? rows[y].data() : nullptr;
			};
			rebuildRowPointers(currentRowsA, rowPointersA);
			rebuildRowPointers(currentRowsB, rowPointersB);

			// Track current phase to detect switches for simple fixed frame snapshots
			bool local_mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);

			while (true) {
				if (m_eval_gstate.m_finished.load(std::memory_order_acquire)
					|| (cfg.max_evals > 0
						&& m_eval_gstate.m_evaluations.load(std::memory_order_relaxed)
							>= m_eval_gstate.m_max_evals))
					break;
				++localIterations;
				// STAGE 1: Simple atomic stage coordination
				bool mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);
				unsigned long long stage_counter = m_eval_gstate.m_dual_stage_counter.fetch_add(1, std::memory_order_relaxed) + 1;

				// Detect phase switch and update fixed frame snapshots
				if (stage_counter >= (unsigned long long)cfg.altering_dual_steps) {
					// Phase switch triggered - coordinate globally using exchange so only one flips
					if (m_eval_gstate.m_dual_stage_counter.exchange(0, std::memory_order_relaxed)
						>= (unsigned long long)cfg.altering_dual_steps) {
						const bool newFocusB = !mutateB;
						m_eval_gstate.m_dual_stage_focus_B.store(newFocusB, std::memory_order_relaxed);
						// Bump the 'other' frame generation to force dual cache invalidation on identity flip
						if (newFocusB) {
							// Now focusing on B (mutateB=true), so A becomes the fixed 'other' frame
							m_eval_gstate.m_dual_generation_A.fetch_add(1, std::memory_order_acq_rel);
						} else {
							// Now focusing on A (mutateB=false), so B becomes the fixed 'other' frame
							m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
						}
					}
				}

				// Quick re-read after potential update
				mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);

				const bool focusChanged = local_mutateB != mutateB;
				// A focus switch changes only which member of the worker's accepted
				// pair is mutated. Legacy mode retains its shared fixed-frame wiring.
				if (focusChanged) {
					if (islandMode) {
						// Materialize the frame just optimized once at the stage boundary.
						// Its rendered rows then remain fixed while the opposite program is
						// mutated; accepted hot-path moves need only replace the program.
						const auto& fixedRows = local_mutateB ? rowPointersA : rowPointersB;
						raster_picture& completedProgram = islandState.Current(local_mutateB);
						(void)ev.ExecuteRasterProgramDual(&completedProgram, line_results.data(),
							fixedRows, local_mutateB);
						if (local_mutateB) {
							UpdateCreatedFromResults(line_results, currentRowsB);
							UpdateTargetsFromResults(line_results, currentTargetsB);
							memcpy(&currentSpritesB, &ev.GetSpritesMemory(), sizeof currentSpritesB);
							rebuildRowPointers(currentRowsB, rowPointersB);
						} else {
							UpdateCreatedFromResults(line_results, currentRowsA);
							UpdateTargetsFromResults(line_results, currentTargetsA);
							memcpy(&currentSpritesA, &ev.GetSpritesMemory(), sizeof currentSpritesA);
							rebuildRowPointers(currentRowsA, rowPointersA);
						}
					} else {
						std::unique_lock<std::mutex> syncLock{m_eval_gstate.m_mutex};
						if (m_eval_gstate.m_best_result < localAcceptedCost) {
							currentA = m_eval_gstate.m_best_pic;
							currentB = m_best_pic_B;
							localAcceptedCost = m_eval_gstate.m_best_result;
						}
						std::unique_lock<std::mutex> fixedLock{m_eval_gstate.m_dual_fixed_frame_mutex};
						const int nextIndex = 1 - m_eval_gstate.m_dual_fixed_rows_active_index.load(std::memory_order_acquire);
						auto& fixedRows = m_eval_gstate.m_dual_fixed_rows_buf[nextIndex];
						fixedRows.resize((size_t)m_height);
						const auto& rows = mutateB ? m_eval_gstate.m_created_picture : m_created_picture_B;
						for (int y = 0; y < m_height; ++y)
							fixedRows[y] = y < (int)rows.size() && !rows[y].empty() ? rows[y].data() : nullptr;
						m_eval_gstate.m_dual_fixed_frame_is_A.store(mutateB, std::memory_order_relaxed);
						m_eval_gstate.m_dual_fixed_rows_active_index.store(nextIndex, std::memory_order_release);
					}
					local_mutateB = mutateB;
				}

				const bool migrationCheckDue = focusChanged
					|| localIterations % migrationCheckInterval == 0;
				if (islandMode && migrationCheckDue) {
					const unsigned long long publishedVersion =
						m_eval_gstate.m_best_state_version.load(std::memory_order_acquire);
					if (publishedVersion != observedBestVersion) {
						bool migrated = false;
						std::unique_lock<std::mutex> stateLock{m_eval_gstate.m_mutex};
						const double publishedCost = m_eval_gstate.m_best_result.load(std::memory_order_relaxed);
						if (publishedCost < islandState.optimizer.currentCost) {
							const auto copyStart = std::chrono::steady_clock::now();
							currentA = m_eval_gstate.m_best_pic;
							currentB = m_best_pic_B;
							currentRowsA = m_eval_gstate.m_created_picture;
							currentRowsB = m_created_picture_B;
							currentTargetsA = m_eval_gstate.m_created_picture_targets;
							currentTargetsB = m_created_picture_targets_B;
							memcpy(&currentSpritesA, &m_eval_gstate.m_sprites_memory, sizeof currentSpritesA);
							memcpy(&currentSpritesB, &m_sprites_memory_B, sizeof currentSpritesB);
							localMigrationCopyNs += static_cast<unsigned long long>(
								std::chrono::duration_cast<std::chrono::nanoseconds>(
									std::chrono::steady_clock::now() - copyStart).count());
							++localMigrationCopyEvents;
							localAcceptedCost = publishedCost;
							migrated = true;
						}
						observedBestVersion = m_eval_gstate.m_best_state_version.load(std::memory_order_relaxed);
						stateLock.unlock();
						if (migrated) {
							islandState.Initialize(std::move(currentA), std::move(currentB), localAcceptedCost,
								static_cast<std::size_t>(std::max(solutions, 1)));
							rebuildRowPointers(currentRowsA, rowPointersA);
							rebuildRowPointers(currentRowsB, rowPointersB);
							ev.InvalidateDualCache();
							m_eval_gstate.m_single_migrations.fetch_add(1, std::memory_order_relaxed);
						}
					}
				} else if (!islandMode && m_eval_gstate.m_best_result < localAcceptedCost) {
					std::unique_lock<std::mutex> syncLock{m_eval_gstate.m_mutex};
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						currentA = m_eval_gstate.m_best_pic;
						currentB = m_best_pic_B;
						localAcceptedCost = m_eval_gstate.m_best_result;
					}
				}

				raster_picture cand;
				raster_picture* evaluatedCandidate = nullptr;
				bool transactionalCandidate = false;
				if (islandMode && k_dual_transactional_mutation) {
					evaluatedCandidate = &islandState.Current(mutateB);
					ev.BeginMutationTransaction(*evaluatedCandidate, mutationTransaction);
					transactionalCandidate = true;
					++localUndoCandidates;
				} else {
					cand = islandMode ? islandState.Current(mutateB)
						: (mutateB ? currentB : currentA);
					evaluatedCandidate = &cand;
					++localCandidateFullCopies;
				}
				const std::vector<const unsigned char*>* otherRows = nullptr;
				if (islandMode) {
					otherRows = mutateB ? &rowPointersA : &rowPointersB;
				} else {
					const int readIndex = m_eval_gstate.m_dual_fixed_rows_active_index.load(std::memory_order_acquire);
					otherRows = &m_eval_gstate.m_dual_fixed_rows_buf[readIndex];
				}
				std::vector<const unsigned char*> fallbackRows;
				if ((int)otherRows->size() != m_height) {
					fallbackRows.assign((size_t)m_height, nullptr);
					otherRows = &fallbackRows;
				}
				ev.SetDualMutationOtherRows(*otherRows);
				ev.MutateRasterProgram(evaluatedCandidate,
					transactionalCandidate ? &mutationTransaction : nullptr);
				if (transactionalCandidate)
					localUndoLineSnapshots += mutationTransaction.SavedLineCount();
				const distance_accum_t cost = ev.ExecuteRasterProgramDual(
					evaluatedCandidate, line_results.data(), *otherRows, mutateB);

				Evaluator::AcceptanceOutcome out{false, false, localAcceptedCost};
				if (islandMode) {
					if (m_eval_gstate.m_finished.load(std::memory_order_acquire)) {
						if (transactionalCandidate) {
							ev.RestoreMutationTransaction(mutationTransaction);
							++localUndoRestores;
						}
						break;
					}
					const unsigned long long evaluationNumber =
						m_eval_gstate.m_evaluations.fetch_add(1, std::memory_order_relaxed) + 1ULL;
					const OptimizerKind kind = m_eval_gstate.m_optimizer == EvalGlobalState::OPT_LAHC
						? OptimizerKind::LAHC : OptimizerKind::DLAS;
					const double previousCost = islandState.optimizer.currentCost;
					const double drift = ev.CalculateAcceptanceDrift();
					out.accepted = transactionalCandidate
						? islandState.ApplyInPlace(kind, (double)cost, drift)
						: islandState.Apply(kind, (double)cost, mutateB, std::move(cand), drift);
					out.previousCost = previousCost;
					if (out.accepted) {
						m_eval_gstate.m_single_accepted.fetch_add(1, std::memory_order_relaxed);
						localAcceptedCost = (double)cost;
					}

					if (out.accepted && (double)cost < m_eval_gstate.m_best_result.load(std::memory_order_acquire)) {
						std::unique_lock<std::mutex> publishLock{m_eval_gstate.m_mutex};
					if ((double)cost < m_eval_gstate.m_best_result.load(std::memory_order_relaxed)) {
						out.improved = true;
						const auto copyStart = std::chrono::steady_clock::now();
							// The active candidate's line results are still live here. Pair
							// them with the already materialized opposite frame before
							// publishing the complete A/B state.
							if (mutateB) {
								UpdateCreatedFromResults(line_results, currentRowsB);
								UpdateTargetsFromResults(line_results, currentTargetsB);
								memcpy(&currentSpritesB, &ev.GetSpritesMemory(), sizeof currentSpritesB);
								rebuildRowPointers(currentRowsB, rowPointersB);
							} else {
								UpdateCreatedFromResults(line_results, currentRowsA);
								UpdateTargetsFromResults(line_results, currentTargetsA);
								memcpy(&currentSpritesA, &ev.GetSpritesMemory(), sizeof currentSpritesA);
								rebuildRowPointers(currentRowsA, rowPointersA);
							}
							m_eval_gstate.m_single_global_improvements.fetch_add(1, std::memory_order_relaxed);
							m_eval_gstate.m_last_best_evaluation.store(evaluationNumber, std::memory_order_relaxed);
							m_eval_gstate.m_best_pic = islandState.currentA;
							m_best_pic_B = islandState.currentB;
							m_eval_gstate.m_best_pic.uncache_insns();
							m_best_pic_B.uncache_insns();
							m_eval_gstate.m_created_picture = currentRowsA;
							m_created_picture_B = currentRowsB;
							m_eval_gstate.m_created_picture_targets = currentTargetsA;
							m_created_picture_targets_B = currentTargetsB;
							memcpy(&m_eval_gstate.m_sprites_memory, &currentSpritesA, sizeof currentSpritesA);
							memcpy(&m_sprites_memory_B, &currentSpritesB, sizeof currentSpritesB);
							m_eval_gstate.m_best_result.store((double)cost, std::memory_order_release);
							m_eval_gstate.m_previous_results = islandState.optimizer.history;
							m_eval_gstate.m_previous_results_index = islandState.optimizer.historyIndex;
						m_eval_gstate.m_current_cost = islandState.optimizer.currentCost;
						m_eval_gstate.m_cost_max = islandState.optimizer.costMax;
						m_eval_gstate.m_N = islandState.optimizer.maxCount;
						localPublicationCopyNs += static_cast<unsigned long long>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - copyStart).count());
						++localPublicationCopyEvents;
						observedBestVersion = m_eval_gstate.m_best_state_version.fetch_add(1, std::memory_order_acq_rel) + 1ULL;
							m_eval_gstate.m_update_improvement = true;
							m_eval_gstate.m_condvar_update.notify_one();
						}
					}
					ev.RecordMutationOutcome(out, (double)cost);
					if (transactionalCandidate && !out.accepted) {
						ev.RestoreMutationTransaction(mutationTransaction);
						++localUndoRestores;
					}
					if (m_eval_gstate.m_save_period > 0 && evaluationNumber % (unsigned long long)m_eval_gstate.m_save_period == 0ULL) {
						std::unique_lock<std::mutex> eventLock{m_eval_gstate.m_mutex};
						m_eval_gstate.m_update_autosave = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
					if (cfg.max_evals > 0 && evaluationNumber >= m_eval_gstate.m_max_evals)
						m_eval_gstate.m_finished.store(true, std::memory_order_release);
				} else {
					std::unique_lock<std::mutex> lock{m_eval_gstate.m_mutex};
					if (m_eval_gstate.m_finished || (cfg.max_evals > 0 && m_eval_gstate.m_evaluations >= m_eval_gstate.m_max_evals))
						break;
					++m_eval_gstate.m_evaluations;
					out = ev.ApplyAcceptanceCore((double)cost, false);
					ev.RecordMutationOutcome(out, (double)cost);
					if (out.accepted && !out.improved) {
						if (mutateB) currentB = cand; else currentA = cand;
						localAcceptedCost = (double)cost;
					}
					if (out.improved) {
						m_eval_gstate.m_last_best_evaluation.store(m_eval_gstate.m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
						m_eval_gstate.m_best_result = (double)cost;
						if (mutateB) {
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							UpdateCreatedFromResults(line_results, m_created_picture_B);
							UpdateTargetsFromResults(line_results, m_created_picture_targets_B);
							memcpy(&m_sprites_memory_B, &ev.GetSpritesMemory(), sizeof m_sprites_memory_B);
							currentB = m_best_pic_B;
						} else {
							m_eval_gstate.m_best_pic = cand; m_eval_gstate.m_best_pic.uncache_insns();
							UpdateCreatedFromResults(line_results, m_eval_gstate.m_created_picture);
							UpdateTargetsFromResults(line_results, m_eval_gstate.m_created_picture_targets);
							memcpy(&m_eval_gstate.m_sprites_memory, &ev.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
							currentA = m_eval_gstate.m_best_pic;
						}
						m_eval_gstate.m_update_improvement = true;
						m_eval_gstate.m_condvar_update.notify_one();
						localAcceptedCost = (double)cost;
					}
				}
				if (out.improved)
					ev.FlushMutationStatsToGlobal();
			}
			m_eval_gstate.m_single_candidate_full_copies.fetch_add(
				localCandidateFullCopies, std::memory_order_relaxed);
			m_eval_gstate.m_single_undo_candidates.fetch_add(
				localUndoCandidates, std::memory_order_relaxed);
			m_eval_gstate.m_single_undo_line_snapshots.fetch_add(
				localUndoLineSnapshots, std::memory_order_relaxed);
			m_eval_gstate.m_single_undo_restores.fetch_add(
				localUndoRestores, std::memory_order_relaxed);
			m_eval_gstate.m_publication_copy_events.fetch_add(
				localPublicationCopyEvents, std::memory_order_relaxed);
			m_eval_gstate.m_publication_copy_ns.fetch_add(
				localPublicationCopyNs, std::memory_order_relaxed);
			m_eval_gstate.m_migration_copy_events.fetch_add(
				localMigrationCopyEvents, std::memory_order_relaxed);
			m_eval_gstate.m_migration_copy_ns.fetch_add(
				localMigrationCopyNs, std::memory_order_relaxed);
		});
	}

	// UI loop while workers progress
	while (!m_eval_gstate.m_finished && (cfg.max_evals == 0 || m_eval_gstate.m_evaluations < m_eval_gstate.m_max_evals)) {
		// An interrupt is a stop here too; m_finished is what every dual worker
		// already watches, so raising it unwinds the same way the Stop button
		// does and the caller still saves.
		if (interrupts::StopRequested()) {
			Message("Interrupted - saving.");
			m_eval_gstate.m_finished = true;
			break;
		}
		// UI update
		if (!quiet) {
			switch (gui.NextFrame()) {
				case GUI_command::SAVE: SaveBestSolution(); break;
				case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
				case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
				case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); PublishLiveStats(false, false); gui.Present(); break;
				default: if (m_dual_display == DualDisplayMode::MIX) ShowLastCreatedPictureDual(); break;
			}
		}

		// Periodic stats/UI similar to single-frame
		auto next_rate_check_tp = std::chrono::steady_clock::now();
		double secs = std::chrono::duration<double>(next_rate_check_tp - last_rate_check_tp).count();
		if (secs > 0.25) {
			m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) / secs;
			last_rate_check_tp = next_rate_check_tp;
			last_eval = m_eval_gstate.m_evaluations;
			if (cfg.save_period == -1) {
				using namespace std::literals::chrono_literals;
				auto now = std::chrono::steady_clock::now();
				if ( now - m_previous_save_time > 30s ) { m_previous_save_time = now; SaveBestSolution(); }
			} else if (m_eval_gstate.m_update_autosave) {
				m_eval_gstate.m_update_autosave = false;
				SaveBestSolution();
			}
			if (!quiet) {
				ShowMutationStats();
				PublishLiveStats(false, false);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	for (auto &t : workers) { if (t.joinable()) t.join(); }
	for (Evaluator& evaluator : m_evaluators)
	{
		evaluator.FlushMutationDiagnosticsToGlobal();
		evaluator.FlushCacheDiagnosticsToGlobal();
	}
}
