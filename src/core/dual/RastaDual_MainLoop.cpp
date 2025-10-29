// Dual-mode main loop extracted from RastaDual.cpp
#include "rasta.h"
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

	// Dedicated evaluator for preview/initial calculations during bootstrap
	m_eval_gstate.m_dual_phase.store(EvalGlobalState::DUAL_PHASE_BOOTSTRAP_A, std::memory_order_relaxed);
	Evaluator bootstrapEval; bootstrapEval.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, cfg.initial_seed+101, cfg.cache_size);

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

		// Initialize fixed frame pointer buffers for alternating phase (use B as initial fixed)
		m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
		m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
		for (int i = 0; i < 2; ++i) m_eval_gstate.m_dual_fixed_rows_buf[i].resize(m_height);
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

		// Establish a baseline dual cost from current A with fixed B (for UI/acceptance info)
		{
			Evaluator baseEv;
			baseEv.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, cfg.initial_seed + 777, cfg.cache_size);
			baseEv.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				m_target_y.data(), m_target_u.data(), m_target_v.data());
			baseEv.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_target_y8.data(), m_target_u8.data(), m_target_v8.data());
			baseEv.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);
			std::vector<const line_cache_result*> tmpRes(m_height, nullptr);
			std::vector<const unsigned char*> otherRows((size_t)m_height, (const unsigned char*)nullptr);
			for (int y = 0; y < m_height; ++y) otherRows[y] = (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) ? m_created_picture_B[y].data() : nullptr;
			raster_picture a = m_eval_gstate.m_best_pic;
			baseEv.RecachePicture(&a);
			distance_accum_t baseCost = baseEv.ExecuteRasterProgramDual(&a, tmpRes.data(), otherRows, /*mutateB*/false);
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			m_eval_gstate.m_best_result = (double)baseCost;
			// Do not clobber a loaded last-best if present
			if (m_eval_gstate.m_last_best_evaluation == 0ULL)
				m_eval_gstate.m_last_best_evaluation = m_eval_gstate.m_evaluations;
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
		if (m_eval_gstate.m_last_best_evaluation == 0ULL)
			m_eval_gstate.m_last_best_evaluation = m_eval_gstate.m_evaluations;
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
					if (out.improved) {
						m_eval_gstate.m_last_best_evaluation = m_eval_gstate.m_evaluations;
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
		if (!quiet) {
			switch (gui.NextFrame()) {
				case GUI_command::SAVE: SaveBestSolution(); break;
				case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
				case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
				case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); break;
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
			if (!quiet) ShowMutationStats();
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
		{
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			m_eval_gstate.m_previous_results.clear();
			m_eval_gstate.m_previous_results.resize(solutions, (double)bestCostB);
			m_eval_gstate.m_previous_results_index = 0;
			m_eval_gstate.m_current_cost = (double)bestCostB;
			m_eval_gstate.m_cost_max = (double)bestCostB;
			m_eval_gstate.m_N = solutions;
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
						if (out.improved) {
							m_eval_gstate.m_last_best_evaluation = m_eval_gstate.m_evaluations;
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							m_eval_gstate.m_best_result = (double)cost;
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
			if (!quiet) {
				switch (gui.NextFrame()) {
					case GUI_command::SAVE: SaveBestSolution(); break;
					case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
					case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
					case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
					case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
					case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); gui.Present(); break;
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
				if (!quiet) ShowMutationStats();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		for (auto &t : bootWorkersB) { if (t.joinable()) t.join(); }
		// OPTIMAL: Initialize fixed frame pointer buffers for alternating phase (no data copies)
		m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
		m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
		for (int i = 0; i < 2; ++i) {
			m_eval_gstate.m_dual_fixed_rows_buf[i].resize(m_height);
		}
		// Initialize B as the initial fixed frame (since we start with mutateB=false)
		int init_idx = 0;
		for (int y = 0; y < m_height; ++y) {
			if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
				// Wire pointer directly to existing B created picture row
				m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_created_picture_B[y].data();
			} else {
				// Fallback storage to ensure a valid pointer
				m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
				m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
			}
		}
		m_eval_gstate.m_dual_fixed_rows_active_index.store(init_idx, std::memory_order_release);
		m_eval_gstate.m_dual_fixed_frame_is_A.store(false, std::memory_order_relaxed); // B is initially fixed
	}
}

	// Immediately show initial frame to ensure output is visible in dual mode (only if we didn't already)
	if (!skip_bootstrap && !quiet) { m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); }

	if (cfg.continue_processing && m_needs_history_reconfigure) {
		reconfigureAcceptanceHistory();
	}

	// Loop until finished/max_evals using worker threads and snapshots
	const unsigned long long E0 = m_eval_gstate.m_evaluations;
	std::vector<std::thread> workers;
	int num_workers = std::max(1, cfg.threads);
	workers.reserve(num_workers);

	// Initialize atomic stage coordination for alternation
	m_eval_gstate.m_dual_stage_focus_B.store(false, std::memory_order_relaxed);
	m_eval_gstate.m_dual_stage_counter.store(0, std::memory_order_relaxed);
	m_eval_gstate.m_dual_phase.store(EvalGlobalState::DUAL_PHASE_ALTERNATING, std::memory_order_relaxed);

	for (int tid = 0; tid < num_workers; ++tid) {
		workers.emplace_back([this, E0, tid]() {
			// HIGH-PERFORMANCE WORKER THREAD - Minimal critical sections like fork
			Evaluator ev;
			ev.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, cfg.initial_seed + 4242ULL + (unsigned long long)tid * 133ULL, cfg.cache_size, tid);
			ev.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
				 m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
				 m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
				 m_target_y.data(), m_target_u.data(), m_target_v.data());
			ev.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_target_y8.data(), m_target_u8.data(), m_target_v8.data());
			ev.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);

			// Local working state for this thread (NO sharing between threads)
			std::vector<const line_cache_result*> line_results(m_height, nullptr);
			
			// Local copies of current best programs (avoid lock contention)
			raster_picture currentA = m_eval_gstate.m_best_pic;
			raster_picture currentB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;

			// Track local accepted cost for proper acceptance logic (like fork)
			double localAcceptedCost = m_eval_gstate.m_best_result;
			
			// Track current phase to detect switches for simple fixed frame snapshots
			bool local_mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);

			while (true) {
				// STAGE 1: Simple atomic stage coordination 
				bool mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);
				unsigned long long stage_counter = m_eval_gstate.m_dual_stage_counter.fetch_add(1, std::memory_order_relaxed) + 1;
				
				// Detect phase switch and update fixed frame snapshots
				if (stage_counter >= (unsigned long long)cfg.altering_dual_steps) {
					// Phase switch triggered - coordinate globally using exchange so only one flips
					if (m_eval_gstate.m_dual_stage_counter.exchange(0, std::memory_order_relaxed) 
						>= (unsigned long long)cfg.altering_dual_steps) {
						m_eval_gstate.m_dual_stage_focus_B.store(!mutateB, std::memory_order_relaxed);
					}
				}

				// Quick re-read after potential update
				mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);

				// If phase switched, update local state and fixed frame snapshot
				if (local_mutateB != mutateB) {
					std::unique_lock<std::mutex> sync_lock{ m_eval_gstate.m_mutex };
					
					// Sync local copies with shared state
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						currentA = m_eval_gstate.m_best_pic;
						currentB = m_best_pic_B;
						localAcceptedCost = m_eval_gstate.m_best_result;
					}
					
					// SIMPLE: Update the pointer array for the fixed frame using double buffering
					std::unique_lock<std::mutex> fixed_lock{ m_eval_gstate.m_dual_fixed_frame_mutex };
					m_eval_gstate.m_dual_fixed_frame_is_A.store(mutateB, std::memory_order_relaxed); // If mutating B, A is fixed

					int next_idx = 1 - m_eval_gstate.m_dual_fixed_rows_active_index.load(std::memory_order_acquire);
					if ((int)m_eval_gstate.m_dual_fixed_rows_buf[next_idx].size() != m_height) {
						m_eval_gstate.m_dual_fixed_rows_buf[next_idx].assign((size_t)m_height, (const unsigned char*)nullptr);
					}
					if (mutateB) {
						// B will be mutated, so A is fixed - wire pointers to current A rows
						if ((int)m_eval_gstate.m_dual_fixed_frame_A.size() != m_height) {
							m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
						}
						for (int y = 0; y < m_height; ++y) {
							if (y < (int)m_eval_gstate.m_created_picture.size() && !m_eval_gstate.m_created_picture[y].empty()) {
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_eval_gstate.m_created_picture[y].data();
							} else {
								m_eval_gstate.m_dual_fixed_frame_A[y].assign(m_width, 0);
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_eval_gstate.m_dual_fixed_frame_A[y].data();
							}
						}
					} else {
						// A will be mutated, so B is fixed - wire pointers to current B rows
						if ((int)m_eval_gstate.m_dual_fixed_frame_B.size() != m_height) {
							m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
						}
						for (int y = 0; y < m_height; ++y) {
							if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_created_picture_B[y].data();
							} else {
								m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
							}
						}
					}
					m_eval_gstate.m_dual_fixed_rows_active_index.store(next_idx, std::memory_order_release);
					
					local_mutateB = mutateB;
				}

				// STAGE 2: Lock-free quick sync using a version check to avoid per-iteration lock
				if (m_eval_gstate.m_best_result < localAcceptedCost) {
					std::unique_lock<std::mutex> sync_lock{ m_eval_gstate.m_mutex };
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						currentA = m_eval_gstate.m_best_pic;
						currentB = m_best_pic_B;
						localAcceptedCost = m_eval_gstate.m_best_result;
					}
				}
				
				raster_picture cand = mutateB ? currentB : currentA;

				// STAGE 3: ZERO-COPY access to fixed frame - lock-free pointer array snapshot
				int read_idx = m_eval_gstate.m_dual_fixed_rows_active_index.load(std::memory_order_acquire);
				const auto& other_rows_ref = m_eval_gstate.m_dual_fixed_rows_buf[read_idx];

				// If the buffer isn't yet sized (e.g., transient during phase switch), use a local fallback
				distance_accum_t cost;
				if ((int)other_rows_ref.size() != m_height) {
					std::vector<const unsigned char*> fallback_rows((size_t)m_height, (const unsigned char*)nullptr);
					// STAGE 4: Heavy computation outside locks
					// Provide other frame rows for dual-aware mutations during this mutation call
					ev.SetDualMutationOtherRows(fallback_rows);
					ev.MutateRasterProgram(&cand);
					cost = ev.ExecuteRasterProgramDual(&cand, line_results.data(), fallback_rows, mutateB);
				} else {
					// STAGE 4: Heavy computation outside locks
					// Provide other frame rows for dual-aware mutations during this mutation call
					ev.SetDualMutationOtherRows(other_rows_ref);
					ev.MutateRasterProgram(&cand);
					cost = ev.ExecuteRasterProgramDual(&cand, line_results.data(), other_rows_ref, mutateB);
				}

				// STAGE 5: Shared acceptance core (under lock)
				Evaluator::AcceptanceOutcome out;
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					if (m_eval_gstate.m_finished || (cfg.max_evals > 0 && m_eval_gstate.m_evaluations >= m_eval_gstate.m_max_evals)) {
						break;
				}
				++m_eval_gstate.m_evaluations;
				out = ev.ApplyAcceptanceCore((double)cost, false);
				if (out.improved) {
						m_eval_gstate.m_last_best_evaluation = m_eval_gstate.m_evaluations;
						m_eval_gstate.m_best_result = (double)cost;
						if (mutateB) {
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							UpdateCreatedFromResults(line_results, m_created_picture_B);
							UpdateTargetsFromResults(line_results, m_created_picture_targets_B);
							memcpy(&m_sprites_memory_B, &ev.GetSpritesMemory(), sizeof m_sprites_memory_B);
							m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
							currentB = m_best_pic_B;
						} else {
							m_eval_gstate.m_best_pic = cand; m_eval_gstate.m_best_pic.uncache_insns();
							UpdateCreatedFromResults(line_results, m_eval_gstate.m_created_picture);
							UpdateTargetsFromResults(line_results, m_eval_gstate.m_created_picture_targets);
							memcpy(&m_eval_gstate.m_sprites_memory, &ev.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
							m_eval_gstate.m_dual_generation_A.fetch_add(1, std::memory_order_acq_rel);
							currentA = m_eval_gstate.m_best_pic;
						}
						m_eval_gstate.m_update_improvement = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
					if (m_eval_gstate.m_save_period > 0 && (m_eval_gstate.m_evaluations % (unsigned long long)m_eval_gstate.m_save_period) == 0ULL) {
						m_eval_gstate.m_update_autosave = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
				}
				// Flush mutation stats after improvement (outside lock to avoid deadlock)
				if (out.improved) {
					ev.FlushMutationStatsToGlobal();
				}
			}
		});
	}

	// UI loop while workers progress
	while (!m_eval_gstate.m_finished && (cfg.max_evals == 0 || m_eval_gstate.m_evaluations < m_eval_gstate.m_max_evals)) {
		// UI update
		if (!quiet) {
			switch (gui.NextFrame()) {
				case GUI_command::SAVE: SaveBestSolution(); break;
				case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
				case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
				case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); gui.Present(); break;
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
			if (!quiet) ShowMutationStats();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	for (auto &t : workers) { if (t.joinable()) t.join(); }
}


