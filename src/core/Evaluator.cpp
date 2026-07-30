#include <assert.h>
#include <algorithm>
#include <iterator>
#include <numeric>
#include <functional>
#include <thread>
#include "Evaluator.h"
#include "Program.h"
#include "RegisterState.h"
#include "LinearAllocator.h"
#include "LineCache.h"
#include "OptimizerState.h"
#include "TargetPicture.h"
#include "StructuredSolver.h"
#include "prng_xoroshiro.h"
#include <cfloat>
#include <chrono>
#include "debug_log.h"
#include "debug_log.h"

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define RASTA_HAS_ARM_NEON 1
#else
#define RASTA_HAS_ARM_NEON 0
#endif

void RasterMutationTransaction::Begin(raster_picture& picture, unsigned long long allocatorEpoch)
{
	for (unsigned index : m_touched)
		m_saved[index] = 0;
	m_touched.clear();
	if (m_snapshots.size() != picture.raster_lines.size())
	{
		m_snapshots.resize(picture.raster_lines.size());
		m_saved.assign(picture.raster_lines.size(), 0);
	}
	m_picture = &picture;
	m_memory_saved = false;
	m_allocator_epoch = allocatorEpoch;
}

void RasterMutationTransaction::SaveMemory()
{
	if (!m_picture || m_memory_saved)
		return;
	memcpy(m_memory_snapshot, m_picture->mem_regs_init, sizeof m_memory_snapshot);
	m_attribute_snapshot = m_picture->antic4_attributes;
	m_memory_saved = true;
}

void RasterMutationTransaction::SaveLine(int y)
{
	if (!m_picture || y < 0 || y >= static_cast<int>(m_picture->raster_lines.size()))
		return;
	const unsigned index = static_cast<unsigned>(y);
	if (m_saved[index])
		return;
	m_snapshots[index] = m_picture->raster_lines[index];
	m_saved[index] = 1;
	m_touched.push_back(index);
}

void RasterMutationTransaction::SaveMutationNeighborhood(int y)
{
	SaveLine(y - 1);
	SaveLine(y);
	SaveLine(y + 1);
}

void RasterMutationTransaction::Restore(unsigned long long allocatorEpoch)
{
	if (!m_picture)
		return;
	if (m_memory_saved)
	{
		memcpy(m_picture->mem_regs_init, m_memory_snapshot, sizeof m_memory_snapshot);
		m_picture->antic4_attributes = m_attribute_snapshot;
	}
	for (unsigned index : m_touched)
	{
		m_picture->raster_lines[index].swap(m_snapshots[index]);
		// Restore the original cache pointer when its allocator is still
		// alive. After a clear, recache lazily instead of using stale memory.
		if (allocatorEpoch != m_allocator_epoch)
			m_picture->raster_lines[index].cache_key = NULL;
	}
}

unsigned long long RasterMutationTransaction::SavedLineCount() const
{
	return static_cast<unsigned long long>(m_touched.size());
}

namespace
{
struct PmgPixelSnapshot
{
	unsigned char color_regs[E_TARGET_MAX];
	unsigned char shift_regs[4];
	unsigned char shift_emitted[4];
};

struct PmgHposEvent
{
	unsigned char sprite;
	int old_x;
	int new_x;
	int check_x;
};

int StoredRegisterValue(const SRasterInstruction& instruction,
	unsigned char reg_a, unsigned char reg_x, unsigned char reg_y)
{
	switch (instruction.loose.instruction)
	{
	case E_RASTER_STA: return reg_a;
	case E_RASTER_STX: return reg_x;
	case E_RASTER_STY: return reg_y;
	default: return -1;
	}
}
}

static void AtomicMaxRelaxed(std::atomic<unsigned long long>& target,
	unsigned long long value)
{
	unsigned long long current = target.load(std::memory_order_relaxed);
	while (current < value &&
		!target.compare_exchange_weak(current, value,
			std::memory_order_relaxed, std::memory_order_relaxed))
	{
	}
}

static void AtomicAddRelaxed(std::atomic<double>& target, double value)
{
	double current = target.load(std::memory_order_relaxed);
	while (!target.compare_exchange_weak(current, current + value,
		std::memory_order_relaxed, std::memory_order_relaxed))
	{
	}
}

static void AtomicMaxRelaxed(std::atomic<double>& target, double value)
{
	double current = target.load(std::memory_order_relaxed);
	while (current < value &&
		!target.compare_exchange_weak(current, value,
			std::memory_order_relaxed, std::memory_order_relaxed))
	{
	}
}

EvalGlobalState::EvalGlobalState()
	: m_update_autosave(false)
	, m_update_improvement(false)
	, m_update_initialized(false)
	, m_initialized(false)
	, m_finished(false)
	, m_threads_active(0)
	, m_save_period(0)
	, m_max_evals(0)
	, m_evaluations(0)
	, m_last_best_evaluation(0)
	, m_best_result(DBL_MAX)
	, m_previous_results_index(0)
	, m_cost_max(DBL_MAX)
	, m_N(0)
	, m_current_cost(DBL_MAX)
	, m_time_start(0)
	, m_mutex()
	, m_condvar_update()
	, m_thread_count(1) 
{
	memset(m_mutation_stats, 0, sizeof(m_mutation_stats));
}

EvalGlobalState::~EvalGlobalState()
{
}

Evaluator::Evaluator()
	: m_currently_mutated_y(0)
	, m_best_result(DBL_MAX)
{
	memset(m_mutation_accepted_count, 0, sizeof(m_mutation_accepted_count));
	memset(m_mutation_attempt_count, 0, sizeof(m_mutation_attempt_count));
	memset(m_mutation_applied_count, 0, sizeof(m_mutation_applied_count));
	memset(m_mutation_improving_count, 0, sizeof(m_mutation_improving_count));
	memset(m_mutation_improvement_credit_total, 0, sizeof(m_mutation_improvement_credit_total));
	memset(m_mutation_improvement_credit_max, 0, sizeof(m_mutation_improvement_credit_max));
	memset(m_selector_attempt_count, 0, sizeof(m_selector_attempt_count));
	memset(m_selector_applied_count, 0, sizeof(m_selector_applied_count));
	memset(m_mutation_diag_flushed_attempted, 0, sizeof(m_mutation_diag_flushed_attempted));
	memset(m_mutation_diag_flushed_applied, 0, sizeof(m_mutation_diag_flushed_applied));
	memset(m_mutation_diag_flushed_accepted, 0, sizeof(m_mutation_diag_flushed_accepted));
	memset(m_mutation_diag_flushed_improving, 0, sizeof(m_mutation_diag_flushed_improving));
	memset(m_mutation_improvement_credit_total_flushed, 0,
		sizeof(m_mutation_improvement_credit_total_flushed));
}

void Evaluator::SetDualTables(const float* paletteY, const float* paletteU, const float* paletteV,
	const float* pairYsum, const float* pairUsum, const float* pairVsum,
	const float* pairYdiff, const float* pairUdiff, const float* pairVdiff,
	const float* targetY, const float* targetU, const float* targetV)
{
#ifdef _DEBUG
    assert(paletteY && paletteU && paletteV);
    assert(pairYsum && pairUsum && pairVsum);
    assert(pairYdiff && pairUdiff && pairVdiff);
    assert(targetY && targetU && targetV);
#endif
    m_dual_paletteY = paletteY;
    m_dual_paletteU = paletteU;
    m_dual_paletteV = paletteV;
    m_dual_pairYsum = pairYsum;
    m_dual_pairUsum = pairUsum;
    m_dual_pairVsum = pairVsum;
    m_dual_pairYdiff = pairYdiff;
    m_dual_pairUdiff = pairUdiff;
    m_dual_pairVdiff = pairVdiff;
    m_dual_targetY = targetY;
    m_dual_targetU = targetU;
    m_dual_targetV = targetV;
}

void Evaluator::SetDualTables8(
    const unsigned char* pairYsum8,
    const unsigned char* pairUsum8,
    const unsigned char* pairVsum8,
    const unsigned char* pairYdiff8,
    const unsigned char* pairUdiff8,
    const unsigned char* pairVdiff8,
    const unsigned char* targetY8,
    const unsigned char* targetU8,
    const unsigned char* targetV8)
{
    m_dual_pairYsum8 = pairYsum8;
    m_dual_pairUsum8 = pairUsum8;
    m_dual_pairVsum8 = pairVsum8;
    m_dual_pairYdiff8 = pairYdiff8;
    m_dual_pairUdiff8 = pairUdiff8;
    m_dual_pairVdiff8 = pairVdiff8;
    m_dual_targetY8 = targetY8;
    m_dual_targetU8 = targetU8;
    m_dual_targetV8 = targetV8;
	RebuildDualTemporalPenalty8();
}

void Evaluator::SetDualTemporalWeights(float luma, float chroma)
{
	m_dual_lambda_luma = luma;
	m_dual_lambda_chroma = chroma;
	RebuildDualTemporalPenalty8();
}

void Evaluator::RebuildDualTemporalPenalty8()
{
	if (!m_dual_pairYdiff8 || !m_dual_pairUdiff8 || !m_dual_pairVdiff8)
	{
		m_dual_temporal_penalty8.clear();
		return;
	}
	m_dual_temporal_penalty8.resize(128U * 128U);
	for (unsigned pair = 0; pair < 128U * 128U; ++pair)
	{
		const unsigned dyt = m_dual_pairYdiff8[pair];
		const unsigned dut = m_dual_pairUdiff8[pair];
		const unsigned dvt = m_dual_pairVdiff8[pair];
		const double penalty = static_cast<double>(m_dual_lambda_luma) * m_sq_lut[dyt]
			+ static_cast<double>(m_dual_lambda_chroma)
				* (static_cast<double>(m_sq_lut[dut]) + m_sq_lut[dvt]);
		m_dual_temporal_penalty8[pair] = static_cast<distance_t>(penalty);
	}
}

void Evaluator::FlushMutationStatsToGlobal()
{
	if (!m_gstate) return;
	int local[E_MUTATION_MAX];
	bool any = false;
	for (int i = 0; i < E_MUTATION_MAX; ++i) {
		local[i] = m_current_mutations[i];
		if (local[i]) any = true;
	}
	if (!any) return;
	std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };
	for (int i = 0; i < E_MUTATION_MAX; ++i) {
		if (local[i]) {
			m_gstate->m_mutation_stats[i] += local[i];
		}
	}
}

void Evaluator::RecordMutationOutcome(const AcceptanceOutcome& outcome, double result)
{
	if (!outcome.accepted)
		return;

	unsigned long long appliedOccurrences = 0;
	for (int i = 0; i < E_MUTATION_MAX; ++i)
		appliedOccurrences += static_cast<unsigned long long>(m_current_mutations[i]);
	const ImprovementMagnitudeCredit credit = CalculateImprovementMagnitudeCredit(
		outcome.previousCost, result, appliedOccurrences);
	for (int i = 0; i < E_MUTATION_MAX; ++i)
	{
		const unsigned long long applied = static_cast<unsigned long long>(m_current_mutations[i]);
		if (!applied)
			continue;
		m_mutation_accepted_count[i] += applied;
		if (credit.improving)
		{
			m_mutation_improving_count[i] += applied;
			m_mutation_improvement_credit_total[i] +=
				credit.perOccurrence * static_cast<double>(applied);
			m_mutation_improvement_credit_max[i] = std::max(
				m_mutation_improvement_credit_max[i], credit.perOccurrence);
		}
	}
	if (credit.improving)
	{
		++m_improvement_event_count;
		m_improvement_total += credit.delta;
		m_improvement_max = std::max(m_improvement_max, credit.delta);
	}
}

void Evaluator::FlushMutationDiagnosticsToGlobal()
{
	if (!m_gstate)
		return;
	for (int i = 0; i < E_MUTATION_MAX; ++i)
	{
		const unsigned long long attempted = m_mutation_attempt_count[i];
		const unsigned long long applied = m_mutation_applied_count[i];
		const unsigned long long accepted = m_mutation_accepted_count[i];
		const unsigned long long improving = m_mutation_improving_count[i];
		m_gstate->m_mutation_attempted[i].fetch_add(
			attempted - m_mutation_diag_flushed_attempted[i], std::memory_order_relaxed);
		m_gstate->m_mutation_applied[i].fetch_add(
			applied - m_mutation_diag_flushed_applied[i], std::memory_order_relaxed);
		m_gstate->m_mutation_accepted[i].fetch_add(
			accepted - m_mutation_diag_flushed_accepted[i], std::memory_order_relaxed);
		m_gstate->m_mutation_improving[i].fetch_add(
			improving - m_mutation_diag_flushed_improving[i], std::memory_order_relaxed);
		AtomicAddRelaxed(m_gstate->m_mutation_improvement_credit_total[i],
			m_mutation_improvement_credit_total[i]
				- m_mutation_improvement_credit_total_flushed[i]);
		AtomicMaxRelaxed(m_gstate->m_mutation_improvement_credit_max[i],
			m_mutation_improvement_credit_max[i]);
		m_mutation_diag_flushed_attempted[i] = attempted;
		m_mutation_diag_flushed_applied[i] = applied;
		m_mutation_diag_flushed_accepted[i] = accepted;
		m_mutation_diag_flushed_improving[i] = improving;
		m_mutation_improvement_credit_total_flushed[i] =
			m_mutation_improvement_credit_total[i];
	}
	m_gstate->m_improvement_events.fetch_add(
		m_improvement_event_count - m_improvement_events_flushed,
		std::memory_order_relaxed);
	AtomicAddRelaxed(m_gstate->m_improvement_total,
		m_improvement_total - m_improvement_total_flushed);
	AtomicMaxRelaxed(m_gstate->m_improvement_max, m_improvement_max);
	m_improvement_events_flushed = m_improvement_event_count;
	m_improvement_total_flushed = m_improvement_total;
}

void Evaluator::FlushCacheDiagnosticsToGlobal()
{
	if (!m_gstate)
		return;
	m_gstate->m_single_cache_partial_clears.fetch_add(m_cache_partial_clears, std::memory_order_relaxed);
	m_gstate->m_single_cache_full_clears.fetch_add(m_cache_full_clears, std::memory_order_relaxed);
	m_gstate->m_cache_lookups.fetch_add(m_local_cache_lookups, std::memory_order_relaxed);
	m_gstate->m_cache_hits.fetch_add(m_local_cache_hits, std::memory_order_relaxed);
	m_gstate->m_cache_misses.fetch_add(m_local_cache_misses, std::memory_order_relaxed);
	m_gstate->m_cache_lookup_probes.fetch_add(m_local_cache_lookup_probes, std::memory_order_relaxed);
	AtomicMaxRelaxed(m_gstate->m_cache_max_lookup_probes, m_local_cache_max_lookup_probes);
	m_gstate->m_cache_inserts.fetch_add(m_local_cache_inserts, std::memory_order_relaxed);
	m_gstate->m_cache_hash_blocks.fetch_add(m_local_cache_hash_blocks, std::memory_order_relaxed);
	m_gstate->m_cache_entry_bytes.fetch_add(
		m_insn_allocator.allocated_bytes(linear_allocator::LINE_CACHE_ENTRY), std::memory_order_relaxed);
	m_gstate->m_cache_hash_block_bytes.fetch_add(
		m_insn_allocator.allocated_bytes(linear_allocator::LINE_CACHE_HASH_BLOCK), std::memory_order_relaxed);
	m_gstate->m_cache_color_row_bytes.fetch_add(
		m_insn_allocator.allocated_bytes(linear_allocator::LINE_CACHE_COLOR_ROW), std::memory_order_relaxed);
	m_gstate->m_cache_target_row_bytes.fetch_add(
		m_insn_allocator.allocated_bytes(linear_allocator::LINE_CACHE_TARGET_ROW), std::memory_order_relaxed);
	m_gstate->m_insn_cache_hash_block_bytes.fetch_add(
		m_insn_allocator.allocated_bytes(linear_allocator::INSN_CACHE_HASH_BLOCK), std::memory_order_relaxed);
	m_gstate->m_insn_cache_data_bytes.fetch_add(
		m_insn_allocator.allocated_bytes(linear_allocator::INSN_CACHE_DATA), std::memory_order_relaxed);
	m_gstate->m_cache_evaluations.fetch_add(m_local_cache_evaluations, std::memory_order_relaxed);
	m_gstate->m_cache_recomputed_lines.fetch_add(m_local_cache_recomputed_lines, std::memory_order_relaxed);
	m_gstate->m_antic4_attribute_cache_evaluations.fetch_add(
		m_local_antic4_attribute_cache_evaluations, std::memory_order_relaxed);
	m_gstate->m_antic4_attribute_recomputed_lines.fetch_add(
		m_local_antic4_attribute_recomputed_lines, std::memory_order_relaxed);
	AtomicMaxRelaxed(m_gstate->m_cache_max_recomputed_lines, m_local_cache_max_recomputed_lines);
	m_gstate->m_cache_propagation_span.fetch_add(m_local_cache_propagation_span, std::memory_order_relaxed);
	AtomicMaxRelaxed(m_gstate->m_cache_max_propagation_span, m_local_cache_max_propagation_span);
	m_gstate->m_cache_pmg_restarts.fetch_add(m_local_cache_pmg_restarts, std::memory_order_relaxed);
	m_gstate->m_lru_updates.fetch_add(m_local_lru_updates, std::memory_order_relaxed);
	m_gstate->m_lru_search_steps.fetch_add(m_local_lru_search_steps, std::memory_order_relaxed);

	std::unique_lock<std::mutex> lock{m_gstate->m_mutex};
	if (m_gstate->m_cache_hits_by_line.size() != m_height)
	{
		m_gstate->m_cache_hits_by_line.assign(m_height, 0);
		m_gstate->m_cache_misses_by_line.assign(m_height, 0);
	}
	for (unsigned y = 0; y < m_height; ++y)
	{
		m_gstate->m_cache_hits_by_line[y] += m_local_cache_hits_by_line[y];
		m_gstate->m_cache_misses_by_line[y] += m_local_cache_misses_by_line[y];
	}
}

void Evaluator::RecordCacheEvaluation(unsigned recomputedLines,
	int firstMissLine, int lastMissLine)
{
	++m_local_cache_evaluations;
	m_local_cache_recomputed_lines += recomputedLines;
	if (m_current_mutations[E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE] != 0)
	{
		++m_local_antic4_attribute_cache_evaluations;
		m_local_antic4_attribute_recomputed_lines += recomputedLines;
	}
	m_local_cache_max_recomputed_lines = std::max(
		m_local_cache_max_recomputed_lines,
		static_cast<unsigned long long>(recomputedLines));
	const unsigned long long propagationSpan = firstMissLine >= 0
		? static_cast<unsigned long long>(lastMissLine - firstMissLine + 1)
		: 0ULL;
	m_local_cache_propagation_span += propagationSpan;
	m_local_cache_max_propagation_span = std::max(
		m_local_cache_max_propagation_span, propagationSpan);
}

Evaluator::AcceptanceOutcome Evaluator::ApplyAcceptanceCore(double result, bool force_best,
	const raster_picture* new_picture, const line_cache_result** line_results)
{
	AcceptanceOutcome out{false, false, m_gstate ? m_gstate->m_current_cost : result};
	if (!m_gstate) return out;

	// Apply normalized drift to acceptance thresholds if stuck beyond /unstuck_after
	// This gradually allows worse solutions until one is accepted, then resets to explore from new position
    double drift = 0.0;
    bool drift_active = false;
    if (m_gstate->m_unstuck_drift_norm > 0.0 && m_gstate->m_unstuck_after > 0) {
		if (m_gstate->m_evaluations > m_gstate->m_last_best_evaluation) {
			unsigned long long plateau = m_gstate->m_evaluations - m_gstate->m_last_best_evaluation;
			if (plateau >= m_gstate->m_unstuck_after) {
				// Accumulate normalized drift per evaluation since threshold
				unsigned long long evals_since_threshold = plateau - m_gstate->m_unstuck_after + 1ULL;
                double norm_drift_total = m_gstate->m_unstuck_drift_norm * (double)evals_since_threshold;
                // Use precomputed scale to minimize per-iteration work
                drift = norm_drift_total * m_drift_scale;
				m_gstate->m_current_norm_drift = norm_drift_total;
				drift_active = true;
			}
		}
	}
	else {
		m_gstate->m_current_norm_drift = 0.0;
	}

    if (m_gstate->m_previous_results.empty()) {
        m_gstate->m_current_cost = result;
        size_t seed_size = (m_solutions > 0) ? (size_t)m_solutions : 1;
        m_gstate->m_previous_results.resize(seed_size, result);
        m_gstate->m_cost_max = result;
        m_gstate->m_N = static_cast<int>(seed_size);
    }

	if (m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY) {
		// Original legacy LAHC algorithm - EXACT match to !legacy/Evaluator.cpp:164-208
		size_t v = m_gstate->m_previous_results_index % m_solutions;
		double current_distance = m_gstate->m_previous_results[v];
		
		// Original acceptance condition: (result < current_distance && m_best_result == m_gstate->m_best_result) || force_best
		bool accept = (result < current_distance && m_best_result == m_gstate->m_best_result) || force_best;
		
		if (accept) {
			// Store NEW result in history (original behavior)
			m_gstate->m_previous_results[v] = result;
			
			// IMMEDIATE global best update (just like original) - this is CRITICAL for thread sync!
			m_gstate->m_last_best_evaluation.store(m_gstate->m_evaluations.load(std::memory_order_relaxed), std::memory_order_relaxed);
			if (new_picture) {
				m_gstate->m_best_pic = *new_picture;
				m_gstate->m_best_pic.uncache_insns();
			}
			m_gstate->m_best_result = result;  // ← CRITICAL: immediate global update for thread sync
			m_gstate->m_current_cost = result; // ← FIX: Update current_cost for proper statistics collection
			
			// Update created picture and targets (original behavior)
			if (line_results) {
				m_gstate->m_created_picture.resize(m_height);
				m_gstate->m_created_picture_targets.resize(m_height);
				
				for(int y = 0; y < (int)m_height; ++y) {
					const line_cache_result& lcr = *line_results[y];
					m_gstate->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_width);
					m_gstate->m_created_picture_targets[y].resize(m_width);
					lcr.copy_target_row(m_gstate->m_created_picture_targets[y].data(), m_width);
				}
			}
			
			// Update sprites memory (original behavior)
			memcpy(&m_gstate->m_sprites_memory, m_sprites_memory, sizeof m_gstate->m_sprites_memory);
			
			// Update mutation stats (original behavior)
			for(int i = 0; i < E_MUTATION_MAX; ++i) {
				if (m_current_mutations[i]) {
					m_gstate->m_mutation_stats[i] += m_current_mutations[i];
				}
			}
			
			m_gstate->m_update_improvement = true;
			
			// Check for perfect solution (original behavior)
			if (result == 0) {
				m_gstate->m_finished = true;
			}
			
			m_gstate->m_condvar_update.notify_one();
		}
		
		// Update index AFTER processing (original legacy timing)
		++m_gstate->m_previous_results_index;
		
		// Update local best to match global (original behavior)
		if (m_best_result != m_gstate->m_best_result) {
			m_best_result = m_gstate->m_best_result;
			m_best_pic = m_gstate->m_best_pic;
			m_best_pic.recache_insns(m_insn_seq_cache, m_insn_allocator);
		}
		
		out.accepted = accept;
		out.improved = accept; // In legacy mode, acceptance == improvement
	} else {
		// Non-legacy algorithms use drift and pre-increment index
		size_t history_size = m_gstate->m_previous_results.size();
		if (history_size == 0) {
			// History may be empty when switching optimizers mid-run; seed it now
			history_size = (size_t)((m_solutions > 0) ? m_solutions : 1);
			m_gstate->m_previous_results.resize(history_size, result);
			m_gstate->m_current_cost = result;
			m_gstate->m_cost_max = result;
			m_gstate->m_N = (int)history_size;
		}
		size_t l = (history_size > 0) ? (m_gstate->m_previous_results_index % history_size) : 0;
		if (history_size > 0) {
			m_gstate->m_previous_results_index = (l + 1) % history_size;
		} else {
			m_gstate->m_previous_results_index = 0;
		}
		double prev_cost = m_gstate->m_current_cost;

		if (m_gstate->m_optimizer == EvalGlobalState::OPT_LAHC) {
			// Pure LAHC: Accept ONLY if better than historical cost (allows diversity)
        			bool would_accept_without_drift = (result <= m_gstate->m_previous_results[l]);
        			bool accept = (result <= m_gstate->m_previous_results[l] + drift);
        			
        			if (accept) {
        				m_gstate->m_current_cost = result;
        				
        				// Reset drift when it facilitates acceptance (move to new search region)
        				if (drift_active && !would_accept_without_drift) {
        					// Drift helped us accept a solution - reset to continue optimizing from this new position

        					m_gstate->m_current_norm_drift = 0.0;
        				}
        			}
        			
        			// Store current cost AFTER acceptance decision (matches reference LAHC algorithm)
        			m_gstate->m_previous_results[l] = m_gstate->m_current_cost;
        			out.accepted = accept;
		} else {
			// DLAS algorithm
        			bool would_accept_without_drift = (result <= m_gstate->m_current_cost) || (result < m_gstate->m_cost_max);
        			bool accept_dlas = (result <= m_gstate->m_current_cost + drift) || (result < m_gstate->m_cost_max + drift);
        			
        			if (accept_dlas) {
        				m_gstate->m_current_cost = result;
        				
        				// Reset drift when it facilitates acceptance (move to new search region) 
        				if (drift_active && !would_accept_without_drift) {
        					// Drift helped us accept a solution - reset to continue optimizing from this new position

        					m_gstate->m_current_norm_drift = 0.0;
        				}
        			}
			double old_value = m_gstate->m_previous_results[l];
			double currentF = m_gstate->m_current_cost;
			if (currentF > old_value) {
				m_gstate->m_previous_results[l] = currentF;
				if (currentF > m_gstate->m_cost_max) { m_gstate->m_cost_max = currentF; m_gstate->m_N = 1; }
				else if (currentF == m_gstate->m_cost_max) { if (old_value != m_gstate->m_cost_max) ++m_gstate->m_N; }
				else if (old_value == m_gstate->m_cost_max) {
					--m_gstate->m_N;
					if (m_gstate->m_N <= 0) {
						m_gstate->m_cost_max = *std::max_element(m_gstate->m_previous_results.begin(), m_gstate->m_previous_results.end());
						m_gstate->m_N = std::count(m_gstate->m_previous_results.begin(), m_gstate->m_previous_results.end(), m_gstate->m_cost_max);
					}
				}
			} else if (currentF < old_value && currentF < prev_cost) {
				if (old_value == m_gstate->m_cost_max) --m_gstate->m_N;
				m_gstate->m_previous_results[l] = currentF;
				if (m_gstate->m_N <= 0) {
					m_gstate->m_cost_max = *std::max_element(m_gstate->m_previous_results.begin(), m_gstate->m_previous_results.end());
					m_gstate->m_N = std::count(m_gstate->m_previous_results.begin(), m_gstate->m_previous_results.end(), m_gstate->m_cost_max);
				}
			}
			out.accepted = accept_dlas;
		}
	}

	out.improved = (result < m_gstate->m_best_result);
	return out;
}

double Evaluator::CalculateAcceptanceDrift()
{
	double drift = 0.0;
	m_gstate->m_current_norm_drift = 0.0;
	if (m_gstate->m_unstuck_drift_norm <= 0.0 || m_gstate->m_unstuck_after == 0)
		return drift;
	if (m_gstate->m_evaluations <= m_gstate->m_last_best_evaluation)
		return drift;

	const unsigned long long plateau =
		m_gstate->m_evaluations - m_gstate->m_last_best_evaluation;
	if (plateau < m_gstate->m_unstuck_after)
		return drift;

	const unsigned long long evaluationsSinceThreshold =
		plateau - m_gstate->m_unstuck_after + 1ULL;
	const double normalizedDrift = m_gstate->m_unstuck_drift_norm
		* static_cast<double>(evaluationsSinceThreshold);
	m_gstate->m_current_norm_drift = normalizedDrift;
	return normalizedDrift * m_drift_scale;
}

Evaluator::AcceptanceOutcome Evaluator::ApplyIslandAcceptance(
	double result, OptimizerState& state, double drift)
{
	AcceptanceOutcome outcome{false, false, state.currentCost};
	if (!state.initialized)
		state.Initialize(result, static_cast<std::size_t>(std::max(m_solutions, 1)));

	if (m_gstate->m_optimizer == EvalGlobalState::OPT_LAHC)
	{
		outcome.accepted = state.Apply(OptimizerKind::LAHC, result, drift);
	}
	else
	{
		outcome.accepted = state.Apply(OptimizerKind::DLAS, result, drift);
	}

	// The drift value is a threshold snapshot. CalculateAcceptanceDrift()
	// publishes UI reporting through atomic state.
	return outcome;
}
#if defined(_MSC_VER)
#define RASTA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define RASTA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define RASTA_ALWAYS_INLINE inline
#endif

RASTA_ALWAYS_INLINE e_target Evaluator::FindClosestColorRegisterDual(sprites_row_memory_t& spriterow,
	const unsigned char* other_row, unsigned picture_row_index, int x,
	bool& restart_line, distance_t& best_error)
{
	distance_t best_err = DISTANCE_MAX;
	e_target best_reg = E_COLBAK;
	int best_sprite_bit = 0;
	bool sprite_covers_colbak = false;
	const unsigned char idx_other = other_row ? other_row[x] : 0;
	const unsigned pix = picture_row_index + static_cast<unsigned>(x);
	const bool use_quantized = m_dual_pairYsum8 && m_dual_pairUsum8 && m_dual_pairVsum8
		&& m_dual_targetY8 && m_dual_targetU8 && m_dual_targetV8;

	auto pair_distance = [&](int target) -> distance_t
	{
		const unsigned char idx_self = static_cast<unsigned char>(m_mem_regs[target] >> 1);
		const unsigned pair = (static_cast<unsigned>(idx_self) << 7) | idx_other;
		if (use_quantized)
		{
			const unsigned char ty = m_dual_targetY8[pix];
			const unsigned char tu = m_dual_targetU8[pix];
			const unsigned char tv = m_dual_targetV8[pix];
			const unsigned char yab = m_dual_pairYsum8[pair];
			const unsigned char uab = m_dual_pairUsum8[pair];
			const unsigned char vab = m_dual_pairVsum8[pair];
			const unsigned dy = yab > ty ? yab - ty : ty - yab;
			const unsigned du = uab > tu ? uab - tu : tu - uab;
			const unsigned dv = vab > tv ? vab - tv : tv - vab;
			unsigned sum = static_cast<unsigned>(m_sq_lut[dy])
				+ static_cast<unsigned>(m_sq_lut[du])
				+ static_cast<unsigned>(m_sq_lut[dv]);
			if (!m_dual_temporal_penalty8.empty())
				sum += m_dual_temporal_penalty8[pair];
			return static_cast<distance_t>(sum);
		}

		const float dy = m_dual_pairYsum[pair] - m_dual_targetY[pix];
		const float du = m_dual_pairUsum[pair] - m_dual_targetU[pix];
		const float dv = m_dual_pairVsum[pair] - m_dual_targetV[pix];
		double distance = static_cast<double>(dy * dy + du * du + dv * dv);
		if (m_dual_pairYdiff && m_dual_pairUdiff && m_dual_pairVdiff)
		{
			const float dyt = m_dual_pairYdiff[pair];
			const float dut = m_dual_pairUdiff[pair];
			const float dvt = m_dual_pairVdiff[pair];
			distance += static_cast<double>(m_dual_lambda_luma) * (dyt * dyt)
				+ static_cast<double>(m_dual_lambda_chroma) * (dut * dut + dvt * dvt);
		}
		return static_cast<distance_t>(distance);
	};

	for (int target = E_COLPM0; target <= E_COLPM3; ++target)
	{
		const int sprite_pos = m_sprite_shift_regs[target - E_COLPM0];
		const int sprite_x = sprite_pos - SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);
		const unsigned x_offset = static_cast<unsigned>(x - sprite_x);
		if (x_offset >= sprite_size)
			continue;

		const int sprite_bit = static_cast<int>(x_offset >> 2);
		assert(sprite_bit >= 0 && sprite_bit < 8);
		sprite_covers_colbak = true;
		int sprite_leftover_pixel = 0;
		const int sprite_leftover = static_cast<int>(x_offset)
			+ m_sprite_shift_emitted[target - E_COLPM0];
		if (sprite_leftover < sprite_size)
		{
			const int sprite_leftover_bit = sprite_leftover >> 2;
			if (sprite_leftover_bit >= 0 && sprite_leftover_bit < 8)
				sprite_leftover_pixel = spriterow[target - E_COLPM0][sprite_leftover_bit];
		}

		const distance_t distance = pair_distance(target);
		if (spriterow[target - E_COLPM0][sprite_bit] || sprite_leftover_pixel)
		{
			best_sprite_bit = sprite_bit;
			best_reg = static_cast<e_target>(target);
			best_err = distance;
			break;
		}
		if (distance < best_err)
		{
			best_sprite_bit = sprite_bit;
			best_reg = static_cast<e_target>(target);
			best_err = distance;
		}
	}

	const int last_color_register = sprite_covers_colbak ? E_COLOR2 : E_COLBAK;
	bool used_four_candidate_kernel = false;

#if RASTA_HAS_ARM_NEON
	if (m_use_dual_neon && use_quantized && !sprite_covers_colbak)
	{
		alignas(8) unsigned char pair_y[8]{};
		alignas(8) unsigned char pair_u[8]{};
		alignas(8) unsigned char pair_v[8]{};
		alignas(16) distance_t penalties[4]{};
		alignas(16) distance_t distances[4]{};
		for (int lane = 0; lane < 4; ++lane)
		{
			const unsigned idx_self = m_mem_regs[E_COLOR0 + lane] >> 1;
			const unsigned pair = (idx_self << 7) | idx_other;
			pair_y[lane] = m_dual_pairYsum8[pair];
			pair_u[lane] = m_dual_pairUsum8[pair];
			pair_v[lane] = m_dual_pairVsum8[pair];
			if (!m_dual_temporal_penalty8.empty())
				penalties[lane] = m_dual_temporal_penalty8[pair];
		}

		const uint8x8_t target_y = vdup_n_u8(m_dual_targetY8[pix]);
		const uint8x8_t target_u = vdup_n_u8(m_dual_targetU8[pix]);
		const uint8x8_t target_v = vdup_n_u8(m_dual_targetV8[pix]);
		const uint8x8_t delta_y = vabd_u8(vld1_u8(pair_y), target_y);
		const uint8x8_t delta_u = vabd_u8(vld1_u8(pair_u), target_u);
		const uint8x8_t delta_v = vabd_u8(vld1_u8(pair_v), target_v);
		const uint32x4_t square_y = vmovl_u16(vget_low_u16(vmull_u8(delta_y, delta_y)));
		const uint32x4_t square_u = vmovl_u16(vget_low_u16(vmull_u8(delta_u, delta_u)));
		const uint32x4_t square_v = vmovl_u16(vget_low_u16(vmull_u8(delta_v, delta_v)));
		const uint32x4_t sum = vaddq_u32(
			vaddq_u32(square_y, square_u),
			vaddq_u32(square_v, vld1q_u32(penalties)));
		vst1q_u32(distances, sum);
		for (int lane = 0; lane < 4; ++lane)
		{
			if (distances[lane] < best_err)
			{
				best_err = distances[lane];
				best_reg = static_cast<e_target>(E_COLOR0 + lane);
			}
		}
		used_four_candidate_kernel = true;
	}
#endif
	if (!used_four_candidate_kernel)
	{
		for (int target = E_COLOR0; target <= last_color_register; ++target)
		{
			const distance_t distance = pair_distance(target);
			if (distance < best_err)
			{
				best_err = distance;
				best_reg = static_cast<e_target>(target);
			}
		}
	}

	if (best_reg >= E_COLPM0 && best_reg <= E_COLPM3
		&& !spriterow[best_reg - E_COLPM0][best_sprite_bit])
	{
		restart_line = true;
		spriterow[best_reg - E_COLPM0][best_sprite_bit] = true;
	}
	best_error = best_err;
	return best_reg;
}

#undef RASTA_ALWAYS_INLINE

distance_accum_t Evaluator::ExecuteRasterProgramDual(raster_picture *pic, const line_cache_result **results_array, const std::vector<const unsigned char*>& other_rows, bool mutateB)
{
	static constexpr int k_max_visible_width = 176;
	static constexpr int k_max_hpos_events = 64;
	const int visible_width = std::min(static_cast<int>(m_width), k_max_visible_width);
	PmgPixelSnapshot pmg_snapshots[k_max_visible_width];
	PmgHposEvent pmg_hpos_events[k_max_hpos_events];
	int pmg_hpos_event_count = 0;

#ifdef _DEBUG
    assert(m_dual_paletteY && m_dual_paletteU && m_dual_paletteV);
    assert(m_dual_pairYsum && m_dual_pairUsum && m_dual_pairVsum);
    assert(m_dual_targetY && m_dual_targetU && m_dual_targetV);
    assert((int)other_rows.size() == (int)m_height);
#endif
    // Ensure dual cache storage exists
    if ((int)m_line_caches_dual.size() != (int)m_height) {
        m_line_caches_dual.clear();
        m_line_caches_dual.resize(m_height);
    }

    // Snapshot generation counter of the OTHER frame (for cache invalidation)
    m_dual_gen_other_snapshot = mutateB ? m_gstate->m_dual_generation_A.load(std::memory_order_acquire)
                                        : m_gstate->m_dual_generation_B.load(std::memory_order_acquire);
    if (m_dual_last_other_generation != m_dual_gen_other_snapshot) {
		ClearLineCacheGeneration();
        m_dual_last_other_generation = m_dual_gen_other_snapshot;
    }

    DBG_PRINT("[EVAL] ExecuteRasterProgramDual enter: pic=%p h=%u w=%u", (void*)pic, m_height, m_width);
    // Memory guard similar to single-run to prevent unbounded growth
    if (m_cache_allocator_stats.resident_bytes > m_cache_size) {
        std::unique_lock<std::mutex> cache_lock(m_gstate->m_cache_mutex);
        if (m_cache_allocator_stats.resident_bytes > m_cache_size) {
			ClearLineCacheGeneration();
			++m_cache_partial_clears;
			if (m_insn_allocator.size() > m_cache_size / k_instruction_cache_budget_divisor) {
                ++m_cache_full_clears;
                m_insn_seq_cache.clear();
                m_insn_allocator.clear();
				++m_allocator_epoch;
				if (pic) {
					const size_t lines = pic->raster_lines.size();
					for (size_t i = 0; i < lines; ++i) pic->raster_lines[i].cache_key = NULL;
				}
				ClearLineActivity();
            }
        }
    }
    int x,y; // currently processed pixel
    int cycle;
    int next_instr_offset;
    int ip; // instruction pointer
    const SRasterInstruction *__restrict instr;

    m_reg_a=0; m_reg_x=0; m_reg_y=0;
    if (m_onoff) TurnOffRegisters(pic);
    memset(m_sprite_shift_regs,0,sizeof(m_sprite_shift_regs));
    memcpy(m_mem_regs,pic->mem_regs_init,sizeof(pic->mem_regs_init));
    memset(m_sprites_memory,0,sizeof(m_sprites_memory));

    bool restart_line=false;
    bool shift_start_array_dirty = true;
    distance_accum_t total_error = 0;

    unsigned recomputedLines = 0;
    int firstMissLine = -1;
    int lastMissLine = -1;
    for (y=0; y<(int)m_height; ++y)
    {
		pmg_hpos_event_count = 0;
        const unsigned char* __restrict other_row = other_rows[y];
		StoreLineRegs();

        raster_line& rline = pic->raster_lines[y];
        line_cache_key lck; CaptureRegisterState(lck.entry_state);
        // Ensure instruction sequence pointer is valid before hashing/lookup
        if (!rline.cache_key) { rline.recache_insns(m_insn_seq_cache, m_insn_allocator); }
        lck.insn_seq = rline.cache_key;
        const uint32_t lck_hash = lck.hash();

        unsigned char * __restrict created_picture_row = &m_created_picture[y][0];
        unsigned char * __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

        // Try dual cache first (separate from single-frame cache)
        unsigned lookupProbes = 0;
        const line_cache_result* cached_line_result = m_line_caches_dual[y].find(lck, lck_hash, &lookupProbes);
        ++m_local_cache_lookups;
        m_local_cache_lookup_probes += lookupProbes;
        m_local_cache_max_lookup_probes = std::max(
            m_local_cache_max_lookup_probes, static_cast<unsigned long long>(lookupProbes));
        if (cached_line_result)
        {
            ++m_local_cache_hits;
            ++m_local_cache_hits_by_line[y];
            results_array[y] = cached_line_result;
            ApplyRegisterState(cached_line_result->new_state);
            memcpy(m_sprites_memory[y], cached_line_result->sprite_data, sizeof m_sprites_memory[y]);
            shift_start_array_dirty = true;
            UpdateLRU(y);
            total_error += cached_line_result->line_error;
            continue;
        }

        ++m_local_cache_misses;
        ++m_local_cache_misses_by_line[y];
        ++recomputedLines;
        if (firstMissLine < 0) firstMissLine = y;
        lastMissLine = y;

        if (shift_start_array_dirty) { shift_start_array_dirty = false; ResetSpriteShiftStartArray(); }

        const int rastinsncnt = (int)rline.instructions.size();
        const SRasterInstruction *__restrict rastinsns = rastinsncnt ? &rline.instructions[0] : nullptr;

        restart_line=false; ip=0; cycle=0;
		next_instr_offset = rastinsncnt
			? RasterInstructionCompletionOffset(
				screen_cycles, cycle, rastinsns[ip], false)
			: 1000;
        memset(m_sprite_shift_regs,0,sizeof(m_sprite_shift_regs));

        const int picture_row_index = m_width * y;
        distance_accum_t total_line_error = 0;
        sprites_row_memory_t& spriterow = m_sprites_memory[y];

        for (x = -SpriteScreenColorCycleStart(
				m_active_raster_picture ? m_active_raster_picture->graphics_mode
					: GraphicsMode::AnticE);
			x < static_cast<int>(m_width) + 16; ++x)
        {
            const int sprite_check_x = x + SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);
            const unsigned char sprite_start_mask = m_sprite_shift_start_array[sprite_check_x];
            if (sprite_start_mask)
            {
                if (sprite_start_mask & 1) StartSpriteShift(E_HPOSP0);
                if (sprite_start_mask & 2) StartSpriteShift(E_HPOSP1);
                if (sprite_start_mask & 4) StartSpriteShift(E_HPOSP2);
                if (sprite_start_mask & 8) StartSpriteShift(E_HPOSP3);
            }

            while(next_instr_offset<x && ip<rastinsncnt)
            {
                instr = &rastinsns[ip++];
				const unsigned hpos_index =
					static_cast<unsigned>(instr->loose.target - E_HPOSP0);
				if (hpos_index < 4)
				{
					const int new_x = StoredRegisterValue(
						*instr, m_reg_a, m_reg_x, m_reg_y);
					const int old_x = m_mem_regs[instr->loose.target];
					const int visible_left = SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE) - sprite_size;
					const int visible_right = SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE) + m_width - 1;
					if (new_x >= 0 && old_x != new_x
						&& new_x >= visible_left && new_x <= visible_right)
					{
						assert(pmg_hpos_event_count < k_max_hpos_events);
						PmgHposEvent& event = pmg_hpos_events[pmg_hpos_event_count++];
						event.sprite = static_cast<unsigned char>(hpos_index);
						event.old_x = old_x;
						event.new_x = new_x;
						event.check_x = sprite_check_x;
					}
				}
                ExecuteInstruction(*instr, sprite_check_x, spriterow, total_line_error);
                cycle+=GetInstructionCycles(*instr);
                next_instr_offset = ip < rastinsncnt
					? RasterInstructionCompletionOffset(
						screen_cycles, cycle, rastinsns[ip], false)
					: 1000;
            }

            if ((unsigned)x < (unsigned)m_width)
            {
				PmgPixelSnapshot& snapshot = pmg_snapshots[x];
				memcpy(snapshot.color_regs, m_mem_regs, sizeof snapshot.color_regs);
				memcpy(snapshot.shift_regs, m_sprite_shift_regs, sizeof snapshot.shift_regs);
				memcpy(snapshot.shift_emitted, m_sprite_shift_emitted, sizeof snapshot.shift_emitted);

				distance_t closest_dist;
				const e_target closest_register = FindClosestColorRegisterDual(
					spriterow, other_row, static_cast<unsigned>(picture_row_index),
					x, restart_line, closest_dist);
				total_line_error += closest_dist;
				created_picture_row[x] = m_mem_regs[closest_register] >> 1;
				created_picture_targets_row[x] = closest_register;
            }
        }

		if (restart_line)
		{
			++m_local_cache_pmg_restarts;
			register_state outgoing_state;
			CaptureRegisterState(outgoing_state);
			bool added_bits;
			do
			{
				added_bits = false;
				total_line_error = 0;
				for (x = 0; x < visible_width; ++x)
				{
					const PmgPixelSnapshot& snapshot = pmg_snapshots[x];
					memcpy(m_mem_regs, snapshot.color_regs, sizeof snapshot.color_regs);
					memcpy(m_sprite_shift_regs, snapshot.shift_regs, sizeof snapshot.shift_regs);
					memcpy(m_sprite_shift_emitted, snapshot.shift_emitted, sizeof snapshot.shift_emitted);

					bool added_bit = false;
					distance_t closest_dist;
					const e_target closest_register = FindClosestColorRegisterDual(
						spriterow, other_row, static_cast<unsigned>(picture_row_index),
						x, added_bit, closest_dist);
					added_bits = added_bits || added_bit;
					total_line_error += closest_dist;
					created_picture_row[x] = m_mem_regs[closest_register] >> 1;
					created_picture_targets_row[x] = closest_register;
				}
				if (added_bits)
					++m_local_cache_pmg_restarts;
			} while (added_bits);

			for (int event_index = 0; event_index < pmg_hpos_event_count; ++event_index)
			{
				const PmgHposEvent& event = pmg_hpos_events[event_index];
				bool sprite_has_data = false;
				for (int bit = 7; bit >= 0; --bit)
				{
					if (spriterow[event.sprite][bit])
					{
						sprite_has_data = true;
						break;
					}
				}
				if (!sprite_has_data)
					continue;
				if (event.old_x - event.check_x <= 6 && event.old_x - event.check_x > 0)
					total_line_error += 100000;
				if (event.new_x - event.check_x <= 6 && event.new_x - event.check_x > 0)
					total_line_error += 100000;
			}
			ApplyRegisterState(outgoing_state);
			restart_line = false;
		}

		total_error += total_line_error;
		bool allocatedBlock = false;
		line_cache_result& result_state = m_line_caches_dual[y].insert(
			lck, lck_hash, m_line_allocator, &allocatedBlock);
		++m_local_cache_inserts;
		if (allocatedBlock) ++m_local_cache_hash_blocks;
		UpdateLRU(y);
		result_state.line_error = total_line_error;
		CaptureRegisterState(result_state.new_state);
		result_state.color_row = (unsigned char *)m_line_allocator.allocate(
			m_width, linear_allocator::LINE_CACHE_COLOR_ROW);
		memcpy(result_state.color_row, created_picture_row, m_width);
		const size_t targetBytes = line_cache_result::packed_target_bytes(m_width);
		result_state.packed_target_row = (unsigned char *)m_line_allocator.allocate(
			targetBytes, linear_allocator::LINE_CACHE_TARGET_ROW);
		line_cache_result::pack_target_row(
			result_state.packed_target_row, created_picture_targets_row, m_width);
		memcpy(result_state.sprite_data, m_sprites_memory[y], sizeof result_state.sprite_data);
		results_array[y] = &result_state;
    }
    RecordCacheEvaluation(recomputedLines, firstMissLine, lastMissLine);
    return total_error;
}

#if RASTA_TRACK_LINE_LRU
void Evaluator::UpdateLRU(int line_index) {
	++m_local_lru_updates;
	if (m_lru_set.find(line_index) != m_lru_set.end()) {
		auto it = m_lru_lines.begin();
		for (; it != m_lru_lines.end(); ++it) {
			++m_local_lru_search_steps;
			if (*it == line_index) break;
		}
		if (it != m_lru_lines.end()) m_lru_lines.erase(it);
	} else {
		m_lru_set.insert(line_index);
	}
	m_lru_lines.push_back(line_index);
}

void Evaluator::ClearLineActivity()
{
	m_lru_lines.clear();
	m_lru_set.clear();
}
#endif

void Evaluator::ClearLineCacheGeneration()
{
	for (auto& cache : m_line_caches)
		cache.clear();
	for (auto& cache : m_line_caches_dual)
		cache.clear();
	m_line_allocator.clear();
	ClearLineActivity();
}

void Evaluator::RecachePicture(raster_picture* pic, bool force)
{
    if (!pic) return;
    bool needsRecache = force;
    const size_t lines = pic->raster_lines.size();
    for (size_t i = 0; i < lines; ++i)
    {
        if (!force && pic->raster_lines[i].cache_key == NULL) { needsRecache = true; break; }
    }
    if (needsRecache)
    {
        pic->recache_insns(m_insn_seq_cache, m_insn_allocator);
    }
}

int Evaluator::SelectMutation()
{
    // Consider all mutations but gate dual-only one if no dual context
    const int active_mutations = E_MUTATION_MAX;

    // Cache stuck state with TTL to avoid repeated recompute
    bool stuck = false;
    if (m_gstate) {
        if (m_gstate->m_evaluations >= m_stuck_valid_until_eval) {
            unsigned long long thr = m_gstate->m_unstuck_after;
            if (thr > 0 && m_gstate->m_evaluations > m_gstate->m_last_best_evaluation) {
                m_cached_stuck = (m_gstate->m_evaluations - m_gstate->m_last_best_evaluation) >= thr;
            } else {
                m_cached_stuck = false;
            }
            m_stuck_valid_until_eval = m_gstate->m_evaluations + k_stuck_ttl_evals;
        }
        stuck = m_cached_stuck;
    }

    // Recompute weights rarely when not stuck; recompute immediately when stuck
    // Detect dual availability for gating
    bool dual_ok_now = (m_dual_pairYsum || m_dual_pairYsum8) && m_dual_mutation_other_rows != nullptr;
    bool need_recompute = (m_cached_total_weight <= 0.0) || stuck || (m_gstate && m_gstate->m_evaluations >= m_weights_valid_until_eval) || (dual_ok_now != m_last_dual_ok);
    if (need_recompute) {
        m_cached_total_weight = 0.0;
        for (int i = 0; i < active_mutations; i++) {
            // Gate dual-only mutation unless dual LUTs and rows are available
            if (i == E_MUTATION_COMPLEMENT_VALUE_DUAL) {
                if (!dual_ok_now) { m_cached_weights[i] = 0.0; continue; }
            }
			if (i == E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE)
			{
				m_cached_weights[i] = 0.0;
				continue;
			}

            // Preserve the established fallback-chain feasibility signal.
            // Evaluated accepted/improving credit is recorded separately; both
            // outcome-weighted policies tested so far reduced long-run quality.
            double feasibility_rate = (m_selector_attempt_count[i] > 10) ?
                (double)m_selector_applied_count[i] / m_selector_attempt_count[i] : 0.1;
            double w = 0.1 + 0.9 * feasibility_rate;
            if (stuck) {
                if (i == E_MUTATION_ADD_INSTRUCTION ||
                    i == E_MUTATION_REMOVE_INSTRUCTION ||
                    i == E_MUTATION_CHANGE_VALUE_TO_COLOR ||
                    i == E_MUTATION_COMPLEMENT_VALUE_DUAL ||
                    i == E_MUTATION_SWAP_LINE_WITH_PREV_ONE ||
                    i == E_MUTATION_COPY_LINE_TO_NEXT_ONE) {
                    w *= 2.0;
                }
            }
            m_cached_weights[i] = w;
            m_cached_total_weight += w;
        }
        // set TTL only in not-stuck mode to amortize cost and record dual gate state
        if (m_gstate && !stuck) {
            m_weights_valid_until_eval = m_gstate->m_evaluations + k_weights_ttl_evals;
        } else {
			m_weights_valid_until_eval = m_gstate ? m_gstate->m_evaluations.load(std::memory_order_relaxed) : 0ULL;
        }
        m_last_dual_ok = dual_ok_now;
    }

    auto draw = [&]() {
        if (m_cached_total_weight <= 0.0) return Random(active_mutations);
        double r = (double)Random(10000) / 10000.0 * m_cached_total_weight;
        double sum = 0.0;
        for (int i = 0; i < active_mutations; i++) {
            sum += m_cached_weights[i];
            if (r <= sum) return i;
        }
        return Random(active_mutations);
    };

	return draw();
}

void Evaluator::Init(unsigned width, unsigned height, const distance_t* const* errmap,
	const screen_line* picture, const OnOffMap* onoff, EvalGlobalState* gstate,
	int solutions, unsigned long long randseed, size_t cache_size, int thread_id,
	const screen_line* scoring_picture,
	const std::vector<double>* allocation_line_weights,
	unsigned allocation_global_period)
{
	m_randseed = randseed;
	m_width = width;
	m_height = height;
	m_picture_all_errors = errmap;
	const char* dualNeon = std::getenv("RASTA_DUAL_NEON");
#if RASTA_HAS_ARM_NEON
	m_use_dual_neon = dualNeon == nullptr
		|| !(dualNeon[0] == '0' && dualNeon[1] == '\0');
#else
	(void)dualNeon;
	m_use_dual_neon = false;
#endif
	m_picture = picture;
	m_scoring_picture = scoring_picture != nullptr ? scoring_picture : picture;
	m_onoff = onoff;
	m_gstate = gstate;
	m_solutions = solutions;
	m_cache_size = cache_size;
	m_thread_id = thread_id;
	m_allocation_line_weights = allocation_line_weights != nullptr
		? *allocation_line_weights : std::vector<double>();
	m_allocation_global_period = std::max(2U, allocation_global_period);
	m_primary_mutation_count = 0;
	if (scoring_picture != nullptr)
	{
		m_visual_objective.Init(width, height, scoring_picture, atari_palette);
	}

	m_currently_mutated_y = 0;

	memset(m_current_mutations, 0, sizeof(m_current_mutations));

	m_line_caches.resize(m_height);

	m_created_picture.resize(m_height);
	for (int i = 0; i < (int)m_height; ++i)
		m_created_picture[i].resize(m_width, 0);

	m_created_picture_targets.resize(height);
	for (size_t y = 0; y < height; ++y)
	{
		m_created_picture_targets[y].resize(width);
	}
	m_local_cache_hits_by_line.assign(m_height, 0);
	m_local_cache_misses_by_line.assign(m_height, 0);
	ClearLineActivity();

	// Initialize squared difference LUT for optional 8-bit dual distance
	for (int i=0;i<256;++i) { m_sq_lut[i] = (unsigned short)(i*i); }

	// Precompute drift scale once (NormalizeScore(raw) = raw / (w*h*(MAX_COLOR_DISTANCE/10000)))
	// So raw = norm * w*h*(MAX_COLOR_DISTANCE/10000).
	m_drift_scale = (double)m_width * (double)m_height * (MAX_COLOR_DISTANCE/10000.0);
}

distance_accum_t Evaluator::EvaluateSingle(raster_picture* pic,
	const line_cache_result** line_results)
{
	return ExecuteRasterProgram(pic, line_results);
}

distance_accum_t Evaluator::EvaluateUnweightedSource(raster_picture* pic)
{
	if (!m_visual_objective.IsInitialized() || pic == nullptr)
		return 0;
	std::vector<const line_cache_result*> lineResults(m_height, nullptr);
	RecachePicture(pic, true);
	ExecuteRasterProgram(pic, lineResults.data());
	std::vector<const unsigned char*> rows(m_height, nullptr);
	for (unsigned y = 0; y < m_height; ++y)
		rows[y] = lineResults[y]->color_row;
	return m_visual_objective.DirectMeanScore(rows.data());
}

Evaluator::StructuredWindowComparison Evaluator::CompareStructuredWindow(
	const raster_picture& baseline,
	size_t first_line,
	const std::vector<raster_line>& structured_lines)
{
	StructuredWindowComparison comparison;
	if (baseline.raster_lines.size() != m_height
		|| structured_lines.empty())
		return comparison;
	StructuredWindowResult window;
	window.feasible = true;
	window.lines = structured_lines;
	raster_picture candidate;
	if (!BuildStructuredWindowComparisonCandidate(
		baseline, first_line, window, candidate))
		return comparison;

	raster_picture baseline_copy = baseline;
	std::vector<const line_cache_result*> baseline_results(m_height, nullptr);
	std::vector<const line_cache_result*> candidate_results(m_height, nullptr);
	comparison.baseline_score = EvaluateSingle(
		&baseline_copy, baseline_results.data());
	comparison.structured_score = EvaluateSingle(
		&candidate, candidate_results.data());
	comparison.feasible = true;
	return comparison;
}

Evaluator::StructuredWindowComparison Evaluator::CompareStructuredSourceWindow(
	const raster_picture& baseline,
	size_t first_line,
	size_t line_count,
	size_t alternate_count,
	const StructuredBeamOptions& options)
{
	if (baseline.raster_lines.size() != m_height)
		return {};
	raster_picture target_picture = baseline;
	std::vector<const line_cache_result*> target_results(m_height, nullptr);
	EvaluateSingle(&target_picture, target_results.data());
	const std::vector<line_target> target_rows = m_created_picture_targets;
	std::vector<const unsigned char*> target_row_pointers(m_height, nullptr);
	for (std::size_t line = 0; line < target_rows.size(); ++line)
		target_row_pointers[line] = target_rows[line].data();
	StructuredWindowResult window;
	if (!ExtractStructuredSourceWindow(baseline, first_line, line_count,
			m_picture_all_errors, static_cast<int>(m_width), alternate_count,
			options, window, target_row_pointers.data()))
		return {};
	return CompareStructuredWindow(baseline, first_line, window.lines);
}

Evaluator::StructuredWindowComparison Evaluator::ApplyStructuredSourceWindowIfBetter(
	raster_picture& baseline,
	size_t first_line,
	size_t line_count,
	size_t alternate_count,
	const StructuredBeamOptions& options,
	bool requireSourceOklabImprovement)
{
	StructuredWindowComparison comparison;
	if (baseline.raster_lines.size() != m_height)
		return comparison;

	std::vector<const line_cache_result*> baseline_results(m_height, nullptr);
	comparison.baseline_score = EvaluateSingle(&baseline, baseline_results.data());
	std::vector<const unsigned char*> baseline_color_rows(m_height, nullptr);
	for (std::size_t line = 0; line < baseline_results.size(); ++line)
		baseline_color_rows[line] = baseline_results[line]->color_row;
	comparison.baseline_source_oklab = m_visual_objective.DirectMeanScore(
		baseline_color_rows.data());
	const std::vector<line_target> target_rows = m_created_picture_targets;
	std::vector<const unsigned char*> target_row_pointers(m_height, nullptr);
	for (std::size_t line = 0; line < target_rows.size(); ++line)
		target_row_pointers[line] = target_rows[line].data();

	StructuredWindowResult window;
	if (!ExtractStructuredSourceWindow(baseline, first_line, line_count,
			m_picture_all_errors, static_cast<int>(m_width), alternate_count,
			options, window, target_row_pointers.data()))
		return comparison;
	raster_picture candidate;
	if (!BuildStructuredWindowComparisonCandidate(
			baseline, first_line, window, candidate))
		return comparison;

	std::vector<const line_cache_result*> candidate_results(m_height, nullptr);
	comparison.structured_score = EvaluateSingle(
		&candidate, candidate_results.data());
	std::vector<const unsigned char*> candidate_color_rows(m_height, nullptr);
	for (std::size_t line = 0; line < candidate_results.size(); ++line)
		candidate_color_rows[line] = candidate_results[line]->color_row;
	comparison.structured_source_oklab = m_visual_objective.DirectMeanScore(
		candidate_color_rows.data());
	comparison.feasible = true;
	if (comparison.structured_score < comparison.baseline_score
		&& (!requireSourceOklabImprovement
			|| comparison.structured_source_oklab
				< comparison.baseline_source_oklab))
	{
		baseline = std::move(candidate);
		comparison.accepted = true;
	}
	return comparison;
}

Evaluator::DualStructuredWindowComparison Evaluator::CompareDualStructuredWindow(
	const raster_picture& baselineA,
	const raster_picture& baselineB,
	size_t first_line,
	const StructuredPairedWindowResult& window)
{
	DualStructuredWindowComparison comparison;
	comparison.legal_a = ValidateRasterPicture(baselineA) == E_RASTER_VALID;
	comparison.legal_b = ValidateRasterPicture(baselineB) == E_RASTER_VALID;
	if (!comparison.legal_a || !comparison.legal_b || m_scoring_picture == nullptr
		|| baselineA.raster_lines.size() != m_height
		|| baselineB.raster_lines.size() != m_height)
		return comparison;
	raster_picture candidateA;
	raster_picture candidateB;
	if (!BuildStructuredPairedWindowComparisonCandidates(baselineA, baselineB,
			first_line, window, candidateA, candidateB))
		return comparison;
	comparison.legal_a = ValidateRasterPicture(candidateA) == E_RASTER_VALID;
	comparison.legal_b = ValidateRasterPicture(candidateB) == E_RASTER_VALID;
	if (!comparison.legal_a || !comparison.legal_b)
		return comparison;

	auto render = [this](raster_picture picture,
		std::vector<color_index_line>& rows,
		std::vector<const unsigned char*>& pointers) {
		std::vector<const line_cache_result*> results(m_height, nullptr);
		ExecuteRasterProgram(&picture, results.data());
		rows.resize(m_height);
		pointers.resize(m_height);
		for (size_t line = 0; line < m_height; ++line)
		{
			rows[line].assign(results[line]->color_row,
				results[line]->color_row + m_width);
			pointers[line] = rows[line].data();
		}
	};
	std::vector<color_index_line> baselineRowsA, baselineRowsB;
	std::vector<color_index_line> candidateRowsA, candidateRowsB;
	std::vector<const unsigned char*> baselinePointersA, baselinePointersB;
	std::vector<const unsigned char*> candidatePointersA, candidatePointersB;
	render(baselineA, baselineRowsA, baselinePointersA);
	render(baselineB, baselineRowsB, baselinePointersB);
	render(candidateA, candidateRowsA, candidatePointersA);
	render(candidateB, candidateRowsB, candidatePointersB);
	DualFrameObjective objective;
	objective.Init(m_width, m_height, m_scoring_picture, atari_palette,
		m_dual_lambda_luma, m_dual_lambda_chroma);
	comparison.baseline = objective.Score(
		baselinePointersA.data(), baselinePointersB.data());
	comparison.structured = objective.Score(
		candidatePointersA.data(), candidatePointersB.data());
	comparison.feasible = true;
	return comparison;
}

bool Evaluator::PopulateDualStructuredWindowCosts(
	const raster_picture& baselineA,
	const raster_picture& baselineB,
	size_t first_line,
	StructuredPairedWindowProblem& problem,
	const StructuredBeamOptions& options)
{
	if (problem.lines.empty())
		return false;
	auto singletonLines = problem.lines;
	for (auto& line : singletonLines)
		for (auto& segment : line)
		{
			if (segment.values.empty()) return false;
			segment.values = {segment.values.front()};
		}
	const StructuredPairedWindowResult retained = SearchStructuredPairedWindowBeam(
		problem.incoming_a, problem.incoming_b, singletonLines, options,
		&problem.required_outgoing_a, &problem.required_outgoing_b);
	if (!retained.feasible)
		return false;
	const DualStructuredWindowComparison retainedComparison =
		CompareDualStructuredWindow(
			baselineA, baselineB, first_line, retained);
	if (!retainedComparison.feasible)
		return false;

	for (size_t line = 0; line < problem.lines.size(); ++line)
		for (size_t slot = 0; slot < problem.lines[line].size(); ++slot)
		{
			auto& values = problem.lines[line][slot].values;
			std::vector<StructuredPairedSegmentValue> feasibleValues;
			feasibleValues.reserve(values.size());
			for (const StructuredPairedSegmentValue& value : values)
			{
				auto trialLines = singletonLines;
				trialLines[line][slot].values = {value};
				const StructuredPairedWindowResult trial =
					SearchStructuredPairedWindowBeam(problem.incoming_a,
						problem.incoming_b, trialLines, options,
						&problem.required_outgoing_a,
						&problem.required_outgoing_b);
				if (!trial.feasible) continue;
				const DualStructuredWindowComparison comparison =
					CompareDualStructuredWindow(
						baselineA, baselineB, first_line, trial);
				if (!comparison.feasible) continue;
				StructuredPairedSegmentValue scored = value;
				scored.visual_cost = comparison.structured.visual
					- retainedComparison.structured.visual;
				scored.flicker_cost = comparison.structured.flicker
					- retainedComparison.structured.flicker;
				feasibleValues.push_back(scored);
			}
			if (feasibleValues.empty()) return false;
			values = std::move(feasibleValues);
		}
	return true;
}

void Evaluator::SyncLocalBestToGlobal()
{
	if (!m_gstate) return;
	m_best_result = m_gstate->m_best_result;
	m_best_pic = m_gstate->m_best_pic;
	m_best_pic.recache_insns(m_insn_seq_cache, m_insn_allocator);
}

void Evaluator::ClearAllCaches()
{
	// Clear all caches - used when switching between incompatible optimization phases
	// (e.g., bootstrap uses single-frame/quantized target, alternating uses dual/original input target)
	m_line_caches.clear();
	m_line_caches_dual.clear();
	m_line_allocator.clear();
	m_insn_seq_cache.clear();
	m_insn_allocator.clear();
	++m_allocator_epoch;
	ClearLineActivity();
	// Reset dual cache generation tracking
	m_dual_last_other_generation = 0ULL;
	m_dual_gen_other_snapshot = 0ULL;
	// Resize caches to proper size (will be populated on first use)
	// Note: m_height is guaranteed to be initialized (set in Init() before bootstrap)
	m_line_caches.resize(m_height);
	m_line_caches_dual.resize(m_height);
}

void Evaluator::InvalidateDualCache()
{
	ClearLineCacheGeneration();
	m_dual_last_other_generation = 0ULL;
	m_dual_gen_other_snapshot = 0ULL;
}

void Evaluator::Start()
{
	++m_gstate->m_threads_active;

	std::thread thread{ std::bind( &Evaluator::Run, this ) };
	thread.detach();
}

void Evaluator::Run() {
	const std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> initialSnapshot =
		std::atomic_load_explicit(&m_gstate->m_best_snapshot, std::memory_order_acquire);
	m_best_pic = initialSnapshot ? initialSnapshot->picture : m_gstate->m_best_pic;
	m_best_pic.recache_insns(m_insn_seq_cache, m_insn_allocator);
	raster_picture currentPicture = m_best_pic;
	OptimizerState islandState;
	unsigned long long observedBestVersion =
		m_gstate->m_best_state_version.load(std::memory_order_acquire);
	unsigned long long observedObjectiveGeneration =
		m_gstate->m_objective_generation.load(std::memory_order_acquire);
	if (m_gstate->m_best_result != DBL_MAX)
	{
		// Saved optimizer history cannot be resumed coherently without its
		// corresponding current picture. Start each island from the saved best.
		islandState.Initialize(m_gstate->m_best_result,
			static_cast<std::size_t>(std::max(m_solutions, 1)));
	}

	unsigned last_eval = 0;
	bool clean_first_evaluation = true;
	auto last_rate_check_tp = std::chrono::steady_clock::now();

	raster_picture new_picture;
	std::vector<const line_cache_result*> line_results(m_height);
	unsigned long long localEvaluations = 0;
	unsigned long long localAccepted = 0;
	unsigned long long localGlobalImprovements = 0;
	unsigned long long localMigrations = 0;
	unsigned long long localLockSamples = 0;
	unsigned long long localLockWaitNs = 0;
	unsigned long long localLockHoldNs = 0;
	unsigned long long localCopySamples = 0;
	unsigned long long localCopyNs = 0;
	unsigned long long localPublicationCopyEvents = 0;
	unsigned long long localPublicationCopyNs = 0;
	unsigned long long localMigrationCopyEvents = 0;
	unsigned long long localMigrationCopyNs = 0;
	unsigned long long localMigrationLinesCopied = 0;
	unsigned long long localMigrationLinesReused = 0;
	unsigned long long localCandidateFullCopies = 0;
	unsigned long long localUndoCandidates = 0;
	unsigned long long localUndoLineSnapshots = 0;
	unsigned long long localUndoRestores = 0;
	RasterMutationTransaction mutationTransaction;

	for (;;) {
		if (m_gstate->m_pause_requested.load(std::memory_order_acquire)) {
			std::unique_lock<std::mutex> pauseLock{m_gstate->m_mutex};
			++m_gstate->m_threads_paused;
			m_gstate->m_condvar_update.notify_all();
			m_gstate->m_condvar_update.wait(pauseLock, [this] {
				return !m_gstate->m_pause_requested.load(std::memory_order_acquire)
					|| m_gstate->m_finished.load(std::memory_order_acquire);
			});
			--m_gstate->m_threads_paused;
			const unsigned long long generation =
				m_gstate->m_objective_generation.load(std::memory_order_acquire);
			if (generation != observedObjectiveGeneration) {
				const std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> snapshot =
					std::atomic_load_explicit(&m_gstate->m_best_snapshot,
						std::memory_order_acquire);
				m_best_pic = snapshot ? snapshot->picture : m_gstate->m_best_pic;
				currentPicture = m_best_pic;
				m_best_pic.recache_insns(m_insn_seq_cache, m_insn_allocator);
				currentPicture.recache_insns(m_insn_seq_cache, m_insn_allocator);
				islandState.Initialize(
					m_gstate->m_best_result.load(std::memory_order_acquire),
					static_cast<std::size_t>(std::max(m_solutions, 1)));
				observedBestVersion =
					m_gstate->m_best_state_version.load(std::memory_order_acquire);
				observedObjectiveGeneration = generation;
			}
			if (m_gstate->m_finished.load(std::memory_order_acquire))
				break;
		}
		if (m_cache_allocator_stats.resident_bytes > m_cache_size) {
			// Acquire a mutex to coordinate cache clearing
			std::unique_lock<std::mutex> cache_lock(m_gstate->m_cache_mutex);

			// Check again after acquiring the lock (another thread might have cleared)
			if (m_cache_allocator_stats.resident_bytes > m_cache_size) {
				ClearLineCacheGeneration();
				++m_cache_partial_clears;
				if (m_insn_allocator.size() > m_cache_size / k_instruction_cache_budget_divisor) {
					++m_cache_full_clears;
					m_insn_seq_cache.clear();
					m_insn_allocator.clear();
					++m_allocator_epoch;
					ClearLineActivity();
					m_best_pic.recache_insns(m_insn_seq_cache, m_insn_allocator);
					if (m_gstate->m_optimizer != EvalGlobalState::OPT_LEGACY)
						currentPicture.recache_insns(m_insn_seq_cache, m_insn_allocator);
				}
			}
		}

		if (m_gstate->m_optimizer != EvalGlobalState::OPT_LEGACY)
		{
			const unsigned long long publishedVersion =
				m_gstate->m_best_state_version.load(std::memory_order_acquire);
			if (publishedVersion != observedBestVersion)
			{
				const std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> publishedSnapshot =
					std::atomic_load_explicit(&m_gstate->m_best_snapshot, std::memory_order_acquire);
				if (publishedSnapshot && publishedSnapshot->version != observedBestVersion)
				{
					if (!islandState.initialized || publishedSnapshot->cost < islandState.currentCost)
					{
						const auto copyStart = std::chrono::steady_clock::now();
						const raster_patch_stats patchStats = patch_raster_picture(
							currentPicture, publishedSnapshot->picture);
						currentPicture.recache_missing_insns(m_insn_seq_cache, m_insn_allocator);
						localMigrationCopyNs += static_cast<unsigned long long>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - copyStart).count());
						++localMigrationCopyEvents;
						localMigrationLinesCopied += patchStats.copied_lines;
						localMigrationLinesReused += patchStats.reused_lines;
						++localMigrations;
						islandState.Initialize(publishedSnapshot->cost,
							static_cast<std::size_t>(std::max(m_solutions, 1)));
					}
					observedBestVersion = publishedSnapshot->version;
				}
			}
		}

		bool force_best = false;
		raster_picture* evaluatedPicture = nullptr;
		bool transactionalCandidate = false;
		bool reconstructingSavedPicture = false;
		if (clean_first_evaluation) {
			bool previousResultsEmpty;
			{
				// m_previous_results is a plain std::vector mutated under m_mutex
				// (initialization above and publish below); read it under the same
				// lock rather than racing with those writers.
				std::unique_lock<std::mutex> historyGuard{m_gstate->m_mutex};
				previousResultsEmpty = m_gstate->m_previous_results.empty();
			}
			reconstructingSavedPicture = m_gstate->m_best_result == DBL_MAX
				&& (m_gstate->m_evaluations.load(std::memory_order_relaxed) > 0
					|| !previousResultsEmpty);
			clean_first_evaluation = false;
			force_best = true;
			if (m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY)
			{
				new_picture = m_best_pic;
				++localCandidateFullCopies;
				evaluatedPicture = &new_picture;
			}
			else
			{
				evaluatedPicture = &currentPicture;
			}
		}
		else if (m_gstate->m_optimizer != EvalGlobalState::OPT_LEGACY && !m_onoff) {
			// Mutate the worker's current solution directly. Only the affected
			// line neighborhood is snapshotted so a rejection can be rolled back.
			mutationTransaction.Begin(currentPicture, m_allocator_epoch);
			MutateRasterProgram(&currentPicture, &mutationTransaction);
			evaluatedPicture = &currentPicture;
			transactionalCandidate = true;
			++localUndoCandidates;
			localUndoLineSnapshots += mutationTransaction.SavedLineCount();
		}
		else {
			new_picture = (m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY)
				? m_best_pic : currentPicture;
			++localCandidateFullCopies;
			MutateRasterProgram(&new_picture);
			evaluatedPicture = &new_picture;
		}

		double result = (double)EvaluateSingle(evaluatedPicture, line_results.data());

		++localEvaluations;
		if (m_gstate->m_optimizer != EvalGlobalState::OPT_LEGACY)
		{
			// A resumed program must be rendered once to rebuild its cached rows and
			// validate its score. That reconstruction is not a new search evaluation.
			const unsigned long long evaluationNumber = reconstructingSavedPicture
				? m_gstate->m_evaluations.load(std::memory_order_relaxed)
				: m_gstate->m_evaluations.fetch_add(1, std::memory_order_relaxed) + 1ULL;

			if (!m_gstate->m_initialized.load(std::memory_order_acquire))
			{
				std::unique_lock<std::mutex> initLock{m_gstate->m_mutex};
				if (!m_gstate->m_initialized.load(std::memory_order_relaxed))
				{
					if (m_gstate->m_previous_results.empty())
					{
						m_gstate->m_current_cost = result;
						m_gstate->m_previous_results.resize(m_solutions, result);
						m_gstate->m_cost_max = result;
						m_gstate->m_N = m_solutions;
					}
					m_gstate->m_initialized.store(true, std::memory_order_release);
					m_gstate->m_update_initialized = true;
					m_gstate->m_condvar_update.notify_one();
				}
			}

			const double drift = CalculateAcceptanceDrift();
			const double bestSnapshot = m_gstate->m_best_result.load(std::memory_order_acquire);
			const bool potentialGlobalImprovement = result < bestSnapshot;
			const bool statisticsDue = evaluationNumber % 10000ULL == 0ULL;

			if (!reconstructingSavedPicture && m_gstate->m_save_period
				&& evaluationNumber % m_gstate->m_save_period == 0)
			{
				std::unique_lock<std::mutex> eventLock{m_gstate->m_mutex};
				m_gstate->m_update_autosave = true;
				m_gstate->m_condvar_update.notify_one();
			}
			if (evaluationNumber >= m_gstate->m_max_evals)
			{
				m_gstate->m_finished.store(true, std::memory_order_release);
				m_gstate->m_condvar_update.notify_one();
			}
			const bool stopAfterIteration = m_gstate->m_finished.load(std::memory_order_acquire);

			if (!islandState.initialized)
				islandState.Initialize(result,
					static_cast<std::size_t>(std::max(m_solutions, 1)));
			Evaluator::AcceptanceOutcome out = ApplyIslandAcceptance(result, islandState, drift);
			RecordMutationOutcome(out, result);
			if (out.accepted)
			{
				++localAccepted;
				if (!transactionalCandidate)
				{
					const bool sampleCopy = (localAccepted & 255ULL) == 0;
					std::chrono::steady_clock::time_point copyStart;
					if (sampleCopy)
						copyStart = std::chrono::steady_clock::now();
					currentPicture = *evaluatedPicture;
					if (sampleCopy)
					{
						localCopyNs += static_cast<unsigned long long>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now() - copyStart).count());
						++localCopySamples;
					}
				}
			}

			if (potentialGlobalImprovement)
			{
				std::unique_lock<std::mutex> publishLock{m_gstate->m_mutex};
				if (result < m_gstate->m_best_result.load(std::memory_order_relaxed))
				{
					const auto copyStart = std::chrono::steady_clock::now();
					++localGlobalImprovements;
					m_gstate->m_last_best_evaluation.store(evaluationNumber, std::memory_order_relaxed);
					std::shared_ptr<EvalGlobalState::PublishedBestSnapshot> snapshot =
						std::make_shared<EvalGlobalState::PublishedBestSnapshot>();
					snapshot->picture = *evaluatedPicture;
					snapshot->picture.uncache_insns();
					snapshot->cost = result;
					m_gstate->m_best_result.store(result, std::memory_order_release);
					m_gstate->m_previous_results = islandState.history;
					m_gstate->m_previous_results_index = islandState.historyIndex;
					m_gstate->m_current_cost = islandState.currentCost;
					m_gstate->m_cost_max = islandState.costMax;
					m_gstate->m_N = islandState.maxCount;
					observedBestVersion =
						m_gstate->m_best_state_version.load(std::memory_order_relaxed) + 1;
					snapshot->version = observedBestVersion;
					std::atomic_store_explicit(&m_gstate->m_best_snapshot,
						std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot>(std::move(snapshot)),
						std::memory_order_release);
					m_gstate->m_best_state_version.store(observedBestVersion, std::memory_order_release);
					m_gstate->m_created_picture.resize(m_height);
					m_gstate->m_created_picture_targets.resize(m_height);
					for (int y = 0; y < (int)m_height; ++y) {
						const line_cache_result& lcr = *line_results[y];
						m_gstate->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_width);
						m_gstate->m_created_picture_targets[y].resize(m_width);
						lcr.copy_target_row(m_gstate->m_created_picture_targets[y].data(), m_width);
					}
					memcpy(&m_gstate->m_sprites_memory, m_sprites_memory, sizeof m_gstate->m_sprites_memory);
					localPublicationCopyNs += static_cast<unsigned long long>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - copyStart).count());
					++localPublicationCopyEvents;
					m_gstate->m_update_improvement = true;
					for (int i = 0; i < E_MUTATION_MAX; ++i) {
						if (m_current_mutations[i]) {
							m_gstate->m_mutation_stats[i] += m_current_mutations[i];
						}
					}
					m_gstate->m_condvar_update.notify_one();
				}
				if (statisticsDue) {
					statistics_point stats;
					stats.evaluations = evaluationNumber;
					stats.seconds = (unsigned)(time(NULL) - m_gstate->m_time_start);
					stats.distance = m_gstate->m_best_result.load(std::memory_order_relaxed);
					m_gstate->m_statistics.push_back(stats);
				}
			}
			else if (statisticsDue)
			{
				std::unique_lock<std::mutex> statsLock{m_gstate->m_mutex};
				statistics_point stats;
				stats.evaluations = evaluationNumber;
				stats.seconds = (unsigned)(time(NULL) - m_gstate->m_time_start);
				stats.distance = m_gstate->m_best_result.load(std::memory_order_relaxed);
				m_gstate->m_statistics.push_back(stats);
			}

			if (transactionalCandidate && !out.accepted)
			{
				mutationTransaction.Restore(m_allocator_epoch);
				++localUndoRestores;
			}

			if (stopAfterIteration)
				break;
			continue;
		}

		const bool sampleStateLock = (localEvaluations & 1023ULL) == 0;
		std::chrono::steady_clock::time_point lockWaitStart;
		if (sampleStateLock)
			lockWaitStart = std::chrono::steady_clock::now();
		std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };
		std::chrono::steady_clock::time_point lockAcquired;
		if (sampleStateLock)
		{
			lockAcquired = std::chrono::steady_clock::now();
			localLockWaitNs += static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(lockAcquired - lockWaitStart).count());
			++localLockSamples;
		}

		const unsigned long long evaluationNumber = reconstructingSavedPicture
			? m_gstate->m_evaluations.load(std::memory_order_relaxed)
			: ++m_gstate->m_evaluations;

		// Initialize DLAS on first evaluation
		if (!m_gstate->m_initialized) {
			if (m_gstate->m_previous_results.empty()) {
				if (m_gstate->m_optimizer == EvalGlobalState::OPT_LAHC || m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY) {
					m_gstate->m_current_cost = result;
					m_gstate->m_previous_results.resize(m_solutions, result);
					m_gstate->m_cost_max = result; // not used by LAHC/Legacy, kept for completeness
					m_gstate->m_N = m_solutions;
				} else {
					// DLAS initialization per paper: fill history with F, set Φmax = F, N = L
					m_gstate->m_cost_max = result;
					m_gstate->m_current_cost = result;
					m_gstate->m_previous_results.resize(m_solutions, result);
					m_gstate->m_N = m_solutions;
				}
			}
			m_gstate->m_initialized = true;
			m_gstate->m_update_initialized = true;
			m_gstate->m_condvar_update.notify_one();
		}

		Evaluator::AcceptanceOutcome out{false, false, result};
		if (m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY)
		{
			out = ApplyAcceptanceCore(result, force_best, evaluatedPicture, line_results.data());
			RecordMutationOutcome(out, result);

			// Legacy acceptance and global publication are intentionally kept in
			// one critical section to preserve the original synchronization model.
			if (!reconstructingSavedPicture && m_gstate->m_save_period
				&& evaluationNumber % m_gstate->m_save_period == 0) {
				m_gstate->m_update_autosave = true;
				m_gstate->m_condvar_update.notify_one();
			}
			if (evaluationNumber >= m_gstate->m_max_evals) {
				m_gstate->m_finished = true;
				m_gstate->m_condvar_update.notify_one();
			}
			if (evaluationNumber % 10000ULL == 0ULL) {
				statistics_point stats;
				stats.evaluations = evaluationNumber;
				stats.seconds = (unsigned)(time(NULL) - m_gstate->m_time_start);
				stats.distance = m_gstate->m_best_result;
				m_gstate->m_statistics.push_back(stats);
			}
			const bool stopAfterIteration = m_gstate->m_finished;
			if (sampleStateLock)
			{
				localLockHoldNs += static_cast<unsigned long long>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - lockAcquired).count());
			}
			lock.unlock();
			if (stopAfterIteration)
				break;
			continue;
		}

	}

	FlushMutationDiagnosticsToGlobal();
	FlushCacheDiagnosticsToGlobal();
	std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };
	m_gstate->m_single_accepted.fetch_add(localAccepted, std::memory_order_relaxed);
	m_gstate->m_single_global_improvements.fetch_add(localGlobalImprovements, std::memory_order_relaxed);
	m_gstate->m_single_migrations.fetch_add(localMigrations, std::memory_order_relaxed);
	m_gstate->m_single_state_lock_samples.fetch_add(localLockSamples, std::memory_order_relaxed);
	m_gstate->m_single_state_lock_wait_ns.fetch_add(localLockWaitNs, std::memory_order_relaxed);
	m_gstate->m_single_state_lock_hold_ns.fetch_add(localLockHoldNs, std::memory_order_relaxed);
	m_gstate->m_single_copy_samples.fetch_add(localCopySamples, std::memory_order_relaxed);
	m_gstate->m_single_copy_ns.fetch_add(localCopyNs, std::memory_order_relaxed);
	m_gstate->m_publication_copy_events.fetch_add(localPublicationCopyEvents, std::memory_order_relaxed);
	m_gstate->m_publication_copy_ns.fetch_add(localPublicationCopyNs, std::memory_order_relaxed);
	m_gstate->m_migration_copy_events.fetch_add(localMigrationCopyEvents, std::memory_order_relaxed);
	m_gstate->m_migration_copy_ns.fetch_add(localMigrationCopyNs, std::memory_order_relaxed);
	m_gstate->m_migration_lines_copied.fetch_add(localMigrationLinesCopied, std::memory_order_relaxed);
	m_gstate->m_migration_lines_reused.fetch_add(localMigrationLinesReused, std::memory_order_relaxed);
	m_gstate->m_single_candidate_full_copies.fetch_add(localCandidateFullCopies, std::memory_order_relaxed);
	m_gstate->m_single_undo_candidates.fetch_add(localUndoCandidates, std::memory_order_relaxed);
	m_gstate->m_single_undo_line_snapshots.fetch_add(localUndoLineSnapshots, std::memory_order_relaxed);
	m_gstate->m_single_undo_restores.fetch_add(localUndoRestores, std::memory_order_relaxed);
	--m_gstate->m_threads_active;
	m_gstate->m_condvar_update.notify_one();
}

e_target Evaluator::FindClosestColorRegister(sprites_row_memory_t& spriterow,
	int index, int x, int y, bool& restart_line, distance_t& best_error,
	unsigned char& output_color)
{
	const bool antic4 = m_active_raster_picture
		&& m_active_raster_picture->graphics_mode == GraphicsMode::Antic4;
	if (antic4)
	{
		struct PlayerPixel
		{
			bool covered = false;
			bool active = false;
			int bit = 0;
		};
		PlayerPixel players[4];
		unsigned activeMask = 0;
		for (int player = 0; player < 4; ++player)
		{
			const int spriteX = m_sprite_shift_regs[player]
				- SpriteScreenColorCycleStart(GraphicsMode::Antic4);
			const unsigned xOffset = static_cast<unsigned>(x - spriteX);
			if (xOffset >= sprite_size)
				continue;

			PlayerPixel& pixel = players[player];
			pixel.covered = true;
			pixel.bit = static_cast<int>(xOffset >> 2);
			assert(pixel.bit >= 0 && pixel.bit < 8);
			pixel.active = spriterow[player][pixel.bit];

			const int leftover = static_cast<int>(xOffset)
				+ m_sprite_shift_emitted[player];
			if (leftover < sprite_size)
			{
				const int leftoverBit = leftover >> 2;
				if (leftoverBit >= 0 && leftoverBit < 8)
					pixel.active =
						pixel.active || spriterow[player][leftoverBit];
			}
			if (pixel.active)
				activeMask |= 1u << player;
		}

		const bool alternate = m_active_raster_picture->antic4_attribute(
			y / 8, x / 4);
		const e_target playfieldTargets[4] = {
			E_COLBAK, E_COLOR0, E_COLOR1,
			alternate ? E_COLOR3 : E_COLOR2
		};
		e_target bestPlayfield = E_COLBAK;
		int bestAddedPlayer = -1;
		distance_t bestDistance = DISTANCE_MAX;
		unsigned char bestColor = 0;

		auto consider = [&](unsigned playerMask, int addedPlayer)
		{
			for (e_target playfield : playfieldTargets)
			{
				const unsigned char color =
					ResolveGtiaPriority0ColorIndex(
						m_mem_regs, playfield, playerMask);
				const distance_t distance =
					m_picture_all_errors[color][index];
				if (distance < bestDistance)
				{
					bestDistance = distance;
					bestPlayfield = playfield;
					bestAddedPlayer = addedPlayer;
					bestColor = color;
				}
			}
		};

		consider(activeMask, -1);
		for (int player = 0; player < 4; ++player)
		{
			if (players[player].covered && !players[player].active
				&& !spriterow[player][players[player].bit])
			{
				consider(activeMask | (1u << player), player);
			}
		}

		if (bestAddedPlayer >= 0)
		{
			PlayerPixel& pixel = players[bestAddedPlayer];
			assert(pixel.covered && !spriterow[bestAddedPlayer][pixel.bit]);
			spriterow[bestAddedPlayer][pixel.bit] = true;
			restart_line = true;
		}
		best_error = bestDistance;
		output_color = bestColor;
		return bestPlayfield;
	}

	distance_t distance;
	int sprite_bit;
	int best_sprite_bit;
	e_target result=E_COLBAK;
	distance_t min_distance = DISTANCE_MAX;
	bool sprite_covers_colbak=false;

	// check sprites

	// Sprites priority is 0,1,2,3

	for (int temp=E_COLPM0;temp<=E_COLPM3;++temp)
	{
		int sprite_pos=m_sprite_shift_regs[temp-E_COLPM0];

		int sprite_x=sprite_pos-SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);

		unsigned x_offset = (unsigned)(x - sprite_x);
		if (x_offset < sprite_size)		// (x>=sprite_x && x<sprite_x+sprite_size)
		{
			sprite_bit=x_offset >> 2; // bit of this sprite memory
			assert(sprite_bit<8);

			sprite_covers_colbak=true;

			// never shifted out remaining sprite pixels combine with sprite memory
			int sprite_leftover_pixel = 0;
			int sprite_leftover = x_offset + m_sprite_shift_emitted[temp - E_COLPM0];
			if (sprite_leftover < sprite_size)
			{
				int sprite_leftover_bit = sprite_leftover >> 2;
				sprite_leftover_pixel = spriterow[temp - E_COLPM0][sprite_leftover_bit];
			}

//			distance = T_distance_function(pixel,atari_palette[mem_regs[temp]/2]);
			distance = m_picture_all_errors[m_mem_regs[temp]/2][index];
			if (spriterow[temp-E_COLPM0][sprite_bit] || sprite_leftover_pixel)
			{
				// priority of sprites - next sprites are hidden below that one, so they are not processed
				best_sprite_bit=sprite_bit;
				result=(e_target) temp;
				min_distance = distance;
				break;
			}
			if (distance<min_distance)
			{
				best_sprite_bit=sprite_bit;
				result=(e_target) temp;
				min_distance=distance;
			}
		}
	}

	// check standard colors

	const bool alternate = false;
	const e_target playfieldTargets[4] = {
		E_COLOR0, E_COLOR1, alternate ? E_COLOR3 : E_COLOR2, E_COLBAK
	};
	const int playfieldCount = sprite_covers_colbak ? 3 : 4;
	for (int candidate = 0; candidate < playfieldCount; ++candidate)
	{
		const e_target temp = playfieldTargets[candidate];
//		distance = T_distance_function(pixel,atari_palette[mem_regs[temp]/2]);
		distance = m_picture_all_errors[m_mem_regs[temp]/2][index];
		if (distance<min_distance)
		{
			min_distance=distance;
			result=(e_target) temp;
		}
	}

	// the best color is in sprite, then set the proper bit of the sprite memory and then restart this line
	if (result>=E_COLPM0 && result<=E_COLPM3)
	{
		// if PMG bit has been modified, then restart this line, because previous pixels of COLBAK may be covered
		if (spriterow[result-E_COLPM0][best_sprite_bit]==false)
		{
			restart_line=true;
			spriterow[result-E_COLPM0][best_sprite_bit]=true;
		}

	}

	best_error = min_distance;
	output_color = static_cast<unsigned char>(m_mem_regs[result] >> 1);

	return result;
}

void Evaluator::TurnOffRegisters(raster_picture *pic)
{
	for (size_t i=0;i<E_TARGET_MAX;++i)
	{
		if (m_onoff->on_off[0][i]==false)
			pic->mem_regs_init[i]=0;
	}

	for (int y=0; y<(int)m_height;++y)
	{
		size_t size=pic->raster_lines[y].instructions.size();
		SRasterInstruction *__restrict rastinsns = &pic->raster_lines[y].instructions[0];
		for (size_t i=0;i<size;++i)
		{
			unsigned char target=rastinsns[i].loose.target;
			if (target<E_TARGET_MAX && m_onoff->on_off[y][target]==false)
				rastinsns[i].loose.target=E_TARGET_MAX;
		}		
	}
}

distance_accum_t Evaluator::ExecuteRasterProgram(raster_picture *pic, const line_cache_result **results_array)
{
	static constexpr int k_max_visible_width = 176;
	static constexpr int k_max_hpos_events = 64;
	const int visible_width = std::min(static_cast<int>(m_width), k_max_visible_width);
	PmgPixelSnapshot pmg_snapshots[k_max_visible_width];
	PmgHposEvent pmg_hpos_events[k_max_hpos_events];
	int pmg_hpos_event_count = 0;

	int x,y; // currently processed pixel
#if defined(_DEBUG) || !defined(NDEBUG)
	if (!pic) { DBG_PRINT("[EVAL] ExecuteRasterProgram: pic=null"); return 0; }
#endif

	// Memory guard: keep single-frame path bounded like worker loop and dual path
	// Keep line-result generations plus stable instruction interning within the
	// configured per-evaluator budget.
	if (m_cache_allocator_stats.resident_bytes > m_cache_size)
	{
		// Acquire a mutex to coordinate cache clearing across evaluators
		std::unique_lock<std::mutex> cache_lock(m_gstate->m_cache_mutex);
		// Check again after acquiring the lock (another thread might have cleared)
		if (m_cache_allocator_stats.resident_bytes > m_cache_size)
		{
			ClearLineCacheGeneration();
			++m_cache_partial_clears;
			if (m_insn_allocator.size() > m_cache_size / k_instruction_cache_budget_divisor)
			{
				++m_cache_full_clears;
				m_insn_seq_cache.clear();
				m_insn_allocator.clear();
				++m_allocator_epoch;
				if (pic)
				{
					const size_t lines = pic->raster_lines.size();
					for (size_t i = 0; i < lines; ++i) pic->raster_lines[i].cache_key = NULL;
				}
				ClearLineActivity();
			}
		}
	}

	// Single-frame path: keep hot; picture should have been recached by caller (m_best_pic.recache_insns).
	m_active_raster_picture = pic;

	int cycle;
	int next_instr_offset;
	int ip; // instruction pointer

	const SRasterInstruction *__restrict instr;

	m_reg_a=0;
	m_reg_x=0;
	m_reg_y=0;

	if (m_onoff)
		TurnOffRegisters(pic);

	memset(m_sprite_shift_regs,0,sizeof(m_sprite_shift_regs));
	memcpy(m_mem_regs,pic->mem_regs_init,sizeof(pic->mem_regs_init));
	memset(m_sprites_memory,0,sizeof(m_sprites_memory));
	
	bool restart_line=false;
	bool shift_start_array_dirty = true;
	distance_accum_t total_error = 0;
	unsigned recomputedLines = 0;
	int firstMissLine = -1;
	int lastMissLine = -1;

	for (y=0; y<(int)m_height; ++y)
	{
		const RasterLineSchedule lineSchedule =
			GetRasterLineSchedule(pic->graphics_mode, y, m_height);
		pmg_hpos_event_count = 0;
		if (restart_line)
		{
			RestoreLineRegs();
			shift_start_array_dirty = true;
		}
		else
		{
			StoreLineRegs();
		}

		// snapshot current machine state
		raster_line& rline = pic->raster_lines[y];
		// Ensure instruction sequence pointer is valid before hashing/lookup in case allocator was cleared above
		if (!rline.cache_key) { rline.recache_insns(m_insn_seq_cache, m_insn_allocator); }

		line_cache_key lck;
		CaptureRegisterState(lck.entry_state);
		lck.insn_seq = rline.cache_key;
		antic4_line_cache_key antic4_lck;
		const bool antic4Cache = pic->graphics_mode == GraphicsMode::Antic4;
		if (antic4Cache)
		{
			static_cast<line_cache_key&>(antic4_lck) = lck;
			antic4_lck.attribute_row = pic->antic4_attributes[y / 8];
		}
		const uint32_t lck_hash =
			antic4Cache ? antic4_lck.hash() : lck.hash();

		// check line cache
		unsigned char * __restrict created_picture_row = &m_created_picture[y][0];
		unsigned char * __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

		unsigned lookupProbes = 0;
		const line_cache_result* cached_line_result = antic4Cache
			? m_line_caches[y].find(antic4_lck, lck_hash, &lookupProbes)
			: m_line_caches[y].find(lck, lck_hash, &lookupProbes);
		++m_local_cache_lookups;
		m_local_cache_lookup_probes += lookupProbes;
		m_local_cache_max_lookup_probes = std::max(
			m_local_cache_max_lookup_probes,
			static_cast<unsigned long long>(lookupProbes));
		if (cached_line_result)
		{
			++m_local_cache_hits;
			++m_local_cache_hits_by_line[y];
			// sweet! cache hit!!
			results_array[y] = cached_line_result;
			ApplyRegisterState(cached_line_result->new_state);
			memcpy(m_sprites_memory[y], cached_line_result->sprite_data, sizeof m_sprites_memory[y]);
			shift_start_array_dirty = true;
			UpdateLRU(y);

			total_error += cached_line_result->line_error;
			continue;
		}

		++m_local_cache_misses;
		++m_local_cache_misses_by_line[y];
		++recomputedLines;
		if (firstMissLine < 0) firstMissLine = y;
		lastMissLine = y;

		if (shift_start_array_dirty)
		{
			shift_start_array_dirty = false;

			ResetSpriteShiftStartArray();
		}

		const int rastinsncnt = (int)rline.instructions.size();
		const SRasterInstruction *__restrict rastinsns = rastinsncnt ? &rline.instructions[0] : nullptr;

		restart_line=false;
		ip=0;
		cycle=0;
		const ScreenCycle* lineCycles = lineSchedule.timing->cycles.data();
		const bool antic4Timing = pic->graphics_mode == GraphicsMode::Antic4;
		next_instr_offset = rastinsncnt
			? RasterInstructionCompletionOffset(
				lineCycles, cycle, rastinsns[ip], antic4Timing)
			: 1000;

		// on new line clear sprite shifts and wait to be taken from mem_regs
		memset(m_sprite_shift_regs,0,sizeof(m_sprite_shift_regs));

		const int picture_row_index = m_width * y;

		distance_accum_t total_line_error = 0;

		sprites_row_memory_t& spriterow = m_sprites_memory[y];

		for (x = -SpriteScreenColorCycleStart(
				m_active_raster_picture ? m_active_raster_picture->graphics_mode
					: GraphicsMode::AnticE);
			x < static_cast<int>(m_width) + 16; ++x)
		{
			// check position of sprites
			const int sprite_check_x = x + SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);

			const unsigned char sprite_start_mask = m_sprite_shift_start_array[sprite_check_x];

			if (sprite_start_mask)
			{
				//if (sprite_start_mask & 1) m_sprite_shift_regs[0] = m_mem_regs[E_HPOSP0];
				//if (sprite_start_mask & 2) m_sprite_shift_regs[1] = m_mem_regs[E_HPOSP1];
				//if (sprite_start_mask & 4) m_sprite_shift_regs[2] = m_mem_regs[E_HPOSP2];
				//if (sprite_start_mask & 8) m_sprite_shift_regs[3] = m_mem_regs[E_HPOSP3];
				if (sprite_start_mask & 1) StartSpriteShift(E_HPOSP0);
				if (sprite_start_mask & 2) StartSpriteShift(E_HPOSP1);
				if (sprite_start_mask & 4) StartSpriteShift(E_HPOSP2);
				if (sprite_start_mask & 8) StartSpriteShift(E_HPOSP3);
			}

			while(next_instr_offset<x && ip<rastinsncnt) // execute instructions
			{
				// check position of sprites

				instr = &rastinsns[ip++];

				//if (cycle<4) // in the previous line
				//	ExecuteInstruction(*instr,x+200);
				//else
				//	ExecuteInstruction(*instr,x);
				const unsigned hpos_index =
					static_cast<unsigned>(instr->loose.target - E_HPOSP0);
				if (hpos_index < 4)
				{
					const int new_x = StoredRegisterValue(
						*instr, m_reg_a, m_reg_x, m_reg_y);
					const int old_x = m_mem_regs[instr->loose.target];
					const int visible_left = SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE) - sprite_size;
					const int visible_right = SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE) + m_width - 1;
					if (new_x >= 0 && old_x != new_x
						&& new_x >= visible_left && new_x <= visible_right)
					{
						assert(pmg_hpos_event_count < k_max_hpos_events);
						PmgHposEvent& event = pmg_hpos_events[pmg_hpos_event_count++];
						event.sprite = static_cast<unsigned char>(hpos_index);
						event.old_x = old_x;
						event.new_x = new_x;
						event.check_x = sprite_check_x;
					}
				}

				ExecuteInstruction(*instr, sprite_check_x, spriterow, total_line_error);

				cycle+=GetInstructionCycles(*instr);
				next_instr_offset = ip < rastinsncnt
					? RasterInstructionCompletionOffset(
						lineCycles, cycle, rastinsns[ip], antic4Timing)
					: 1000;
			}

			if ((unsigned)x < (unsigned)m_width)		// x>=0 && x<m_width
			{
				PmgPixelSnapshot& snapshot = pmg_snapshots[x];
				memcpy(snapshot.color_regs, m_mem_regs, sizeof snapshot.color_regs);
				memcpy(snapshot.shift_regs, m_sprite_shift_regs, sizeof snapshot.shift_regs);
				memcpy(snapshot.shift_emitted, m_sprite_shift_emitted, sizeof snapshot.shift_emitted);

				// put pixel closest to one of the current color registers
				distance_t closest_dist;
				unsigned char outputColor = 0;
				e_target closest_register = FindClosestColorRegister(
					spriterow, picture_row_index + x, x, y, restart_line,
					closest_dist, outputColor);
				total_line_error += closest_dist;
				created_picture_row[x] = outputColor;
				created_picture_targets_row[x]=closest_register;
			}
		}

		if (restart_line)
		{
			++m_local_cache_pmg_restarts;

			// Pixel choices do not affect CPU/register evolution. Reuse the first
			// pass's visible-pixel states until PMG bits reach the same fixed point
			// as full line restarts, then retain the already-known outgoing state.
			register_state outgoing_state;
			CaptureRegisterState(outgoing_state);
			bool added_bits;
			do
			{
				added_bits = false;
				total_line_error = 0;
				for (x = 0; x < visible_width; ++x)
				{
					const PmgPixelSnapshot& snapshot = pmg_snapshots[x];
					memcpy(m_mem_regs, snapshot.color_regs, sizeof snapshot.color_regs);
					memcpy(m_sprite_shift_regs, snapshot.shift_regs, sizeof snapshot.shift_regs);
					memcpy(m_sprite_shift_emitted, snapshot.shift_emitted, sizeof snapshot.shift_emitted);

					bool added_bit = false;
					distance_t closest_dist;
					unsigned char outputColor = 0;
					const e_target closest_register = FindClosestColorRegister(
						spriterow, picture_row_index + x, x, y, added_bit,
						closest_dist, outputColor);
					added_bits = added_bits || added_bit;
					total_line_error += closest_dist;
					created_picture_row[x] = outputColor;
					created_picture_targets_row[x] = closest_register;
				}
				if (added_bits)
					++m_local_cache_pmg_restarts;
			} while (added_bits);

			for (int event_index = 0; event_index < pmg_hpos_event_count; ++event_index)
			{
				const PmgHposEvent& event = pmg_hpos_events[event_index];
				bool sprite_has_data = false;
				for (int bit = 7; bit >= 0; --bit)
				{
					if (spriterow[event.sprite][bit])
					{
						sprite_has_data = true;
						break;
					}
				}
				if (!sprite_has_data)
					continue;
				if (event.old_x - event.check_x <= 6 && event.old_x - event.check_x > 0)
					total_line_error += 100000;
				if (event.new_x - event.check_x <= 6 && event.new_x - event.check_x > 0)
					total_line_error += 100000;
			}
			ApplyRegisterState(outgoing_state);
			restart_line = false;
		}

		if (lineSchedule.chbase_transition)
		{
			// The fixed LDA #>charset_N / STA CHBASE suffix intentionally
			// clobbers A. The concrete generator uses ten 1 KiB sets at $8000.
			m_reg_a = static_cast<unsigned char>(0x80 + 4 * (y / 24 + 1));
		}

		if (!restart_line)
		{
			total_error += total_line_error;

			// add this to line cache
			bool allocatedBlock = false;
			line_cache_result& result_state = antic4Cache
				? m_line_caches[y].insert(
					antic4_lck, lck_hash, m_line_allocator, &allocatedBlock)
				: m_line_caches[y].insert(
					lck, lck_hash, m_line_allocator, &allocatedBlock);
			++m_local_cache_inserts;
			if (allocatedBlock)
				++m_local_cache_hash_blocks;
			UpdateLRU(y);
			result_state.line_error = total_line_error;
			CaptureRegisterState(result_state.new_state);
			result_state.color_row = (unsigned char *)m_line_allocator.allocate(
				m_width, linear_allocator::LINE_CACHE_COLOR_ROW);
			memcpy(result_state.color_row, created_picture_row, m_width);

			const size_t targetBytes = line_cache_result::packed_target_bytes(m_width);
			result_state.packed_target_row = (unsigned char *)m_line_allocator.allocate(
				targetBytes, linear_allocator::LINE_CACHE_TARGET_ROW);
			line_cache_result::pack_target_row(
				result_state.packed_target_row, created_picture_targets_row, m_width);

			memcpy(result_state.sprite_data, m_sprites_memory[y], sizeof result_state.sprite_data);

			results_array[y] = &result_state;
		}
	}

	RecordCacheEvaluation(recomputedLines, firstMissLine, lastMissLine);
	return total_error;
}

template<fn_rgb_distance& T_distance_function>
distance_accum_t Evaluator::CalculateLineDistance(const screen_line &r, const screen_line &l)
{
	const int width = r.size();
	distance_accum_t distance=0;

	for (int x=0;x<width;++x)
	{
		rgb in_pixel = r[x];
		rgb out_pixel = l[x];
		distance += T_distance_function(in_pixel,out_pixel);
	}
	return distance;
};

//inline void Evaluator::ExecuteInstruction(const SRasterInstruction &instr, int x)
inline void Evaluator::ExecuteInstruction(const SRasterInstruction &instr, int sprite_check_x, sprites_row_memory_t &spriterow, distance_accum_t &total_line_error)
{
	int reg_value=-1;
	switch(instr.loose.instruction)
	{
	case E_RASTER_LDA:
		m_reg_a=instr.loose.value;
		break;
	case E_RASTER_LDX:
		m_reg_x=instr.loose.value;
		break;
	case E_RASTER_LDY:
		m_reg_y=instr.loose.value;
		break;
	case E_RASTER_STA:
		reg_value=m_reg_a;
		break;
	case E_RASTER_STX:
		reg_value=m_reg_x;
		break;
	case E_RASTER_STY:
		reg_value=m_reg_y;
		break;
	}

	if (reg_value!=-1)
	{
		const unsigned hpos_index = (unsigned)(instr.loose.target - E_HPOSP0);
		if (hpos_index < 4) 
		{
			// Check for unemulated 5 to 6 colour clock latency issues on player hpos changes.
			// Unexpected horizontal lines appear in pictures otherwise when viewed on real
			// hardware and modern emulators.
			// This change strongly discourages the use of the solution for the line but does not
			// make the problem horizontal lines show on screen when RastaConverter is running.
			const int sprite_old_x = m_mem_regs[instr.loose.target];
			const int sprite_new_x = reg_value;
			const int sprites_visible_left = SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE) - sprite_size;
			const int sprites_visible_right = SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE) + m_width-1;
			if (sprite_old_x != sprite_new_x && sprite_new_x >= sprites_visible_left && sprite_new_x <= sprites_visible_right)
			{
				// check if anything to display
				int sprite_bits;
				const int sprite = hpos_index;
				for (sprite_bits = 7; sprite_bits >= 0; --sprite_bits)
				{
					if (spriterow[sprite][sprite_bits])
						break;
				}
				if (sprite_bits >= 0 && sprite_old_x - sprite_check_x <= 6 && sprite_old_x - sprite_check_x > 0)
					// too late to prevent display at old position
					total_line_error += 100000;
				if (sprite_bits >= 0 && sprite_new_x - sprite_check_x <= 6 && sprite_new_x - sprite_check_x > 0)
					// too late to change display to new position
					total_line_error += 100000;
			}

			m_sprite_shift_start_array[m_mem_regs[instr.loose.target]] &= ~(1 << hpos_index);
			m_mem_regs[instr.loose.target]=reg_value;
			m_sprite_shift_start_array[m_mem_regs[instr.loose.target]] |= (1 << hpos_index);
		} 
		else 
		{
			m_mem_regs[instr.loose.target]=reg_value;
		}
	}
}

void Evaluator::StartSpriteShift(int mem_reg)
{
	unsigned char sprite_self_overlap = m_mem_regs[mem_reg] - m_sprite_shift_regs[mem_reg-E_HPOSP0];
	if (sprite_self_overlap > 0 && sprite_self_overlap < sprite_size)
		// number of sprite bits shifted out from the old position
		m_sprite_shift_emitted[mem_reg-E_HPOSP0] = sprite_self_overlap;
	else
		// default is all sprite bits shifted out, no leftover
		m_sprite_shift_emitted[mem_reg-E_HPOSP0] = sprite_size;

	// new shift out starting now at this position
	m_sprite_shift_regs[mem_reg-E_HPOSP0] = m_mem_regs[mem_reg];
}

void Evaluator::MutateLine(raster_line& prog, raster_picture& pic)
{
    // Small batch normally; escalate only when stuck
    int mutation_count = std::min(3 + (int)(prog.instructions.size() / 5), 8);

    bool stuck = false;
    if (m_gstate) {
        unsigned long long thr = m_gstate->m_unstuck_after;
        if (thr > 0 && m_gstate->m_evaluations > m_gstate->m_last_best_evaluation) {
            unsigned long long plateau = m_gstate->m_evaluations - m_gstate->m_last_best_evaluation;
            stuck = (plateau >= thr);
        }
    }

    if (stuck) {
        // Escalate mutations when stuck for stronger exploration
        mutation_count += 5 + Random(10); // +[5..14]
    }

    for (int i = 0; i < mutation_count; ++i) {
        MutateOnce(prog, pic);
    }
	prog.rehash();
}

void Evaluator::MutateOnce(raster_line& prog, raster_picture& pic)
{
	int i1, i2, c, x;
	const RasterLineSchedule currentSchedule =
		GetRasterLineSchedule(pic.graphics_mode, m_currently_mutated_y, m_height);
	const int currentCycleLimit = currentSchedule.optimizer_cycle_limit;
	auto randomWritableTarget = [&]() -> e_target {
		if (pic.graphics_mode == GraphicsMode::Antic4)
		{
			// GTIA applies HPOSP writes five color clocks late and moving a
			// player behind the active beam also changes its internal
			// shifter/latch state. The evaluator does not model that full
			// mid-line state machine, so keep ANTIC 4 player positions fixed
			// for the frame instead of emitting preview-inaccurate streaks.
			const int colorTargetCount = E_COLPM3 + 1;
			if (Random(colorTargetCount + 1) == colorTargetCount)
				return E_COLOR3;
			return static_cast<e_target>(Random(colorTargetCount));
		}
		const int legacyCount = E_HPOSP3 + 1;
		return static_cast<e_target>(Random(legacyCount));
	};

	i1 = Random(prog.instructions.size());
	i2 = i1;
	if (prog.instructions.size() > 2) {
		do {
			i2 = Random(prog.instructions.size());
		} while (i1 == i2);
	}

	SRasterInstruction temp;

	// Use smart selection instead of random
	int mutation = SelectMutation();
	++m_selector_attempt_count[mutation];
	switch (mutation)
	{
	case E_MUTATION_COPY_LINE_TO_NEXT_ONE:
		++m_mutation_attempt_count[E_MUTATION_COPY_LINE_TO_NEXT_ONE];
		if (m_currently_mutated_y < (int)m_height - 1)
		{
			int next_y = m_currently_mutated_y + 1;
			raster_line& next_line = pic.raster_lines[next_y];
			if (next_line.cycles > currentCycleLimit)
				break;
			prog = next_line;
			m_current_mutations[E_MUTATION_COPY_LINE_TO_NEXT_ONE]++;
			++m_mutation_applied_count[E_MUTATION_COPY_LINE_TO_NEXT_ONE];
			++m_selector_applied_count[mutation];
			break;
		}
	case E_MUTATION_PUSH_BACK_TO_PREV:
		++m_mutation_attempt_count[E_MUTATION_PUSH_BACK_TO_PREV];
		if (m_currently_mutated_y > 0)
		{
			int prev_y = m_currently_mutated_y - 1;
			raster_line& prev_line = pic.raster_lines[prev_y];
			c = GetInstructionCycles(prog.instructions[i1]);
			const int previousLimit = GetRasterLineSchedule(
				pic.graphics_mode, prev_y, m_height).optimizer_cycle_limit;
			if (prev_line.cycles + c <= previousLimit)
			{
				// add it to prev line but do not remove it from the current
				prev_line.cycles += c;
				prev_line.instructions.push_back(prog.instructions[i1]);
				prev_line.cache_key = NULL;
				m_current_mutations[E_MUTATION_PUSH_BACK_TO_PREV]++;
				++m_mutation_applied_count[E_MUTATION_PUSH_BACK_TO_PREV];
				++m_selector_applied_count[mutation];
				break;
			}
		}
	case E_MUTATION_SWAP_LINE_WITH_PREV_ONE:
		++m_mutation_attempt_count[E_MUTATION_SWAP_LINE_WITH_PREV_ONE];
		if (m_currently_mutated_y > 0)
		{
			int prev_y = m_currently_mutated_y - 1;
			raster_line& prev_line = pic.raster_lines[prev_y];
			const int previousLimit = GetRasterLineSchedule(
				pic.graphics_mode, prev_y, m_height).optimizer_cycle_limit;
			if (prog.cycles > previousLimit || prev_line.cycles > currentCycleLimit)
				break;
			prog.swap(prev_line);
			m_current_mutations[E_MUTATION_SWAP_LINE_WITH_PREV_ONE]++;
			++m_mutation_applied_count[E_MUTATION_SWAP_LINE_WITH_PREV_ONE];
			++m_selector_applied_count[mutation];
			break;
		}
	case E_MUTATION_ADD_INSTRUCTION:
		++m_mutation_attempt_count[E_MUTATION_ADD_INSTRUCTION];
		if (prog.cycles + 2 <= currentCycleLimit)
		{
			if (prog.cycles + 4 <= currentCycleLimit && Random(2)) // 4 cycles instructions
			{
				temp.loose.instruction = (e_raster_instruction)(E_RASTER_STA + Random(3));
				temp.loose.value = (Random(128) * 2);
				temp.loose.target = randomWritableTarget();

				// More efficient insert - add at end then swap to position
				prog.instructions.push_back(temp);
				for (int i = prog.instructions.size() - 1; i > i1; --i) {
					std::swap(prog.instructions[i], prog.instructions[i - 1]);
				}

				prog.cache_key = NULL;
				prog.cycles += 4;
			}
			else
			{
				temp.loose.instruction = (e_raster_instruction)(E_RASTER_LDA + Random(4));
				if (Random(2))
					temp.loose.value = (Random(128) * 2);
				else
				{
					const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
					temp.loose.value = possible_colors[Random(possible_colors.size())];
				}

				temp.loose.target = randomWritableTarget();
				c = Random(m_picture[m_currently_mutated_y].size());
				temp.loose.value = FindAtariColorIndex(m_picture[m_currently_mutated_y][c]) * 2;

				// More efficient insert
				prog.instructions.push_back(temp);
				for (int i = prog.instructions.size() - 1; i > i1; --i) {
					std::swap(prog.instructions[i], prog.instructions[i - 1]);
				}

				prog.cache_key = NULL;
				prog.cycles += 2;
			}
			m_current_mutations[E_MUTATION_ADD_INSTRUCTION]++;
			++m_mutation_applied_count[E_MUTATION_ADD_INSTRUCTION];
			++m_selector_applied_count[mutation];
			break;
		}
	case E_MUTATION_REMOVE_INSTRUCTION:
		++m_mutation_attempt_count[E_MUTATION_REMOVE_INSTRUCTION];
		if (prog.cycles > 4)
		{
			c = GetInstructionCycles(prog.instructions[i1]);
			if (prog.cycles - c > 0)
			{
				prog.cycles -= c;

				// Preserve order: erase at position i1
				prog.instructions.erase(prog.instructions.begin() + i1);

				prog.cache_key = NULL;
				assert(prog.cycles > 0);
				m_current_mutations[E_MUTATION_REMOVE_INSTRUCTION]++;
				++m_mutation_applied_count[E_MUTATION_REMOVE_INSTRUCTION];
				++m_selector_applied_count[mutation];
				break;
			}
		}
	case E_MUTATION_SWAP_INSTRUCTION:
		++m_mutation_attempt_count[E_MUTATION_SWAP_INSTRUCTION];
		if (prog.instructions.size() > 2)
		{
			// Legacy arbitrary swap
			temp = prog.instructions[i1];
			prog.instructions[i1] = prog.instructions[i2];
			prog.instructions[i2] = temp;
			prog.cache_key = NULL;
			m_current_mutations[E_MUTATION_SWAP_INSTRUCTION]++;
			++m_mutation_applied_count[E_MUTATION_SWAP_INSTRUCTION];
			++m_selector_applied_count[mutation];
			break;
		}
	case E_MUTATION_CHANGE_TARGET:
		++m_mutation_attempt_count[E_MUTATION_CHANGE_TARGET];
		prog.instructions[i1].loose.target = randomWritableTarget();
		prog.cache_key = NULL;
		m_current_mutations[E_MUTATION_CHANGE_TARGET]++;
		++m_mutation_applied_count[E_MUTATION_CHANGE_TARGET];
		++m_selector_applied_count[mutation];
		break;
	case E_MUTATION_CHANGE_VALUE:
		++m_mutation_attempt_count[E_MUTATION_CHANGE_VALUE];
		if (Random(10) == 0)
		{
			if (Random(2))
				prog.instructions[i1].loose.value = (Random(128) * 2);
			else
			{
				const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
				prog.instructions[i1].loose.value = possible_colors[Random(possible_colors.size())];
			}
		}
		else
		{
			c = 1;
			if (Random(2))
				c *= -1;
			if (Random(2))
				c *= 16;
			prog.instructions[i1].loose.value += c;
		}
		prog.cache_key = NULL;
		m_current_mutations[E_MUTATION_CHANGE_VALUE]++;
		++m_mutation_applied_count[E_MUTATION_CHANGE_VALUE];
		++m_selector_applied_count[mutation];
		break;
	case E_MUTATION_CHANGE_VALUE_TO_COLOR:
		++m_mutation_attempt_count[E_MUTATION_CHANGE_VALUE_TO_COLOR];
		if ((prog.instructions[i1].loose.target >= E_HPOSP0 && prog.instructions[i1].loose.target <= E_HPOSP3))
		{
			x = m_mem_regs[prog.instructions[i1].loose.target] - SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);
			x += Random(sprite_size);
		}
		else
		{
			c = 0;
			// find color in the next raster column
			for (x = 0; x < i1 - 1; ++x)
			{
				if (prog.instructions[x].loose.instruction <= E_RASTER_NOP)
					c += 2;
				else
					c += 4; // cycles
			}
			while (Random(5) == 0)
				++c;

			if (c >= currentCycleLimit)
				c = currentCycleLimit - 1;
			x = currentSchedule.timing->cycles[c].offset;
			x += Random(currentSchedule.timing->cycles[c].length);
		}
		if (x < 0 || x >= (int)m_width)
			x = Random(m_width);
		i2 = m_currently_mutated_y;
		// check color in next lines
		while (Random(5) == 0 && i2 + 1 < (int)m_height)
			++i2;
		prog.instructions[i1].loose.value = FindAtariColorIndex(m_picture[i2][x]) * 2;
		prog.cache_key = NULL;
		m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
		++m_mutation_applied_count[E_MUTATION_CHANGE_VALUE_TO_COLOR];
		++m_selector_applied_count[mutation];
		break;
	case E_MUTATION_COMPLEMENT_VALUE_DUAL:
		{
			++m_mutation_attempt_count[E_MUTATION_COMPLEMENT_VALUE_DUAL];
			// Dual-aware: choose value that complements other frame to match target at (x,y)
			// Fallback to CHANGE_VALUE_TO_COLOR if dual context not available
			if (!(m_dual_pairYsum || m_dual_pairYsum8) || m_dual_mutation_other_rows == nullptr) {
				// degrade gracefully
				int saved_mut = E_MUTATION_CHANGE_VALUE_TO_COLOR;
				mutation = saved_mut;
				// re-run this case body by jumping label-style is messy; just perform inline equivalent
				int xx;
				if ((prog.instructions[i1].loose.target >= E_HPOSP0 && prog.instructions[i1].loose.target <= E_HPOSP3))
				{
					xx = m_mem_regs[prog.instructions[i1].loose.target] - SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);
					xx += Random(sprite_size);
				}
				else
				{
					c = 0;
					for (x = 0; x < i1 - 1; ++x)
					{
						if (prog.instructions[x].loose.instruction <= E_RASTER_NOP)
							c += 2;
						else
							c += 4;
					}
					while (Random(5) == 0) ++c;
					if (c >= currentCycleLimit) c = currentCycleLimit - 1;
					xx = currentSchedule.timing->cycles[c].offset;
					xx += Random(currentSchedule.timing->cycles[c].length);
				}
				if (xx < 0 || xx >= (int)m_width) xx = Random(m_width);
				int yy = m_currently_mutated_y; while (Random(5) == 0 && yy + 1 < (int)m_height) ++yy;
				prog.instructions[i1].loose.value = FindAtariColorIndex(m_picture[yy][xx]) * 2;
				prog.cache_key = NULL;
				m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
				++m_mutation_attempt_count[E_MUTATION_CHANGE_VALUE_TO_COLOR];
				++m_mutation_applied_count[E_MUTATION_CHANGE_VALUE_TO_COLOR];
				++m_selector_applied_count[mutation];
				break;
			}

			// Compute approximate (x,y) like CHANGE_VALUE_TO_COLOR
			if ((prog.instructions[i1].loose.target >= E_HPOSP0 && prog.instructions[i1].loose.target <= E_HPOSP3))
			{
				x = m_mem_regs[prog.instructions[i1].loose.target] - SpriteScreenColorCycleStart(m_active_raster_picture ? m_active_raster_picture->graphics_mode : GraphicsMode::AnticE);
				x += Random(sprite_size);
			}
			else
			{
				c = 0;
				for (int k = 0; k < i1 - 1; ++k)
				{
					if (prog.instructions[k].loose.instruction <= E_RASTER_NOP) c += 2; else c += 4;
				}
				while (Random(5) == 0) ++c;
				if (c >= currentCycleLimit) c = currentCycleLimit - 1;
				x = currentSchedule.timing->cycles[c].offset;
				x += Random(currentSchedule.timing->cycles[c].length);
			}
			if (x < 0 || x >= (int)m_width) x = Random(m_width);
			int ypix = m_currently_mutated_y; while (Random(5) == 0 && ypix + 1 < (int)m_height) ++ypix;

			// Get other-frame index at (x,ypix)
			unsigned char idxOther = 0;
			{
				const std::vector<const unsigned char*>& rows = *m_dual_mutation_other_rows;
				if ((int)rows.size() > ypix && rows[ypix]) idxOther = rows[ypix][x];
			}

			// Compute best self index scanning 0..127 using dual LUTs
			unsigned bestIdx = 0; unsigned bestScore = 0xFFFFFFFFu; double bestScoreF = 1e300;
			const unsigned pix = (unsigned)(m_width * ypix + x);
			if (m_dual_pairYsum8 && m_dual_pairUsum8 && m_dual_pairVsum8 && m_dual_targetY8 && m_dual_targetU8 && m_dual_targetV8)
			{
				unsigned char Ty = m_dual_targetY8[pix];
				unsigned char Tu = m_dual_targetU8[pix];
				unsigned char Tv = m_dual_targetV8[pix];
				for (unsigned s = 0; s < 128u; ++s)
				{
					unsigned pair = (s << 7) | (unsigned)idxOther;
					unsigned char Yab = m_dual_pairYsum8[pair];
					unsigned char Uab = m_dual_pairUsum8[pair];
					unsigned char Vab = m_dual_pairVsum8[pair];
					unsigned dY = (unsigned)((Yab > Ty) ? (Yab - Ty) : (Ty - Yab));
					unsigned dU = (unsigned)((Uab > Tu) ? (Uab - Tu) : (Tu - Uab));
					unsigned dV = (unsigned)((Vab > Tv) ? (Vab - Tv) : (Tv - Vab));
					unsigned sum = (unsigned)m_sq_lut[dY] + (unsigned)m_sq_lut[dU] + (unsigned)m_sq_lut[dV];
					if (m_dual_pairYdiff8 && m_dual_pairUdiff8 && m_dual_pairVdiff8)
					{
						unsigned dYt = m_dual_pairYdiff8[pair];
						unsigned dUt = m_dual_pairUdiff8[pair];
						unsigned dVt = m_dual_pairVdiff8[pair];
						sum += (unsigned)((double)m_dual_lambda_luma * (double)m_sq_lut[dYt]
							+ (double)m_dual_lambda_chroma * ((double)m_sq_lut[dUt] + (double)m_sq_lut[dVt]));
					}
					if (sum < bestScore) { bestScore = sum; bestIdx = s; }
				}
			}
			else
			{
				float Ty = m_dual_targetY[pix];
				float Tu = m_dual_targetU[pix];
				float Tv = m_dual_targetV[pix];
				for (unsigned s = 0; s < 128u; ++s)
				{
					unsigned pair = (s << 7) | (unsigned)idxOther;
					float Yab = m_dual_pairYsum[pair];
					float Uab = m_dual_pairUsum[pair];
					float Vab = m_dual_pairVsum[pair];
					float dy = Yab - Ty, du = Uab - Tu, dv = Vab - Tv;
					double dist = (double)(dy*dy + du*du + dv*dv);
					if (m_dual_pairYdiff && m_dual_pairUdiff && m_dual_pairVdiff)
					{
						float dYt = m_dual_pairYdiff[pair];
						float dUt = m_dual_pairUdiff[pair];
						float dVt = m_dual_pairVdiff[pair];
						dist += (double)m_dual_lambda_luma * (double)(dYt*dYt)
							+ (double)m_dual_lambda_chroma * (double)(dUt*dUt + dVt*dVt);
					}
					if (dist < bestScoreF) { bestScoreF = dist; bestIdx = s; }
				}
			}

			prog.instructions[i1].loose.value = (unsigned char)(bestIdx * 2);
			prog.cache_key = NULL;
			m_current_mutations[E_MUTATION_COMPLEMENT_VALUE_DUAL]++;
			++m_mutation_applied_count[E_MUTATION_COMPLEMENT_VALUE_DUAL];
			++m_selector_applied_count[mutation];
			break;
		}
	case E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE:
		break;
	}
}


void Evaluator::BeginMutationTransaction(
	raster_picture& pic, RasterMutationTransaction& transaction)
{
	transaction.Begin(pic, m_allocator_epoch);
}

void Evaluator::RestoreMutationTransaction(RasterMutationTransaction& transaction)
{
	transaction.Restore(m_allocator_epoch);
}

void Evaluator::MutateRasterProgram(raster_picture* pic, RasterMutationTransaction* transaction)
{
	memset(m_current_mutations, 0, sizeof m_current_mutations);

	// Determine if we are stuck based on /unstuck_after
	bool stuck = false;
	if (m_gstate) {
		unsigned long long thr = m_gstate->m_unstuck_after;
		if (thr > 0 && m_gstate->m_evaluations > m_gstate->m_last_best_evaluation) {
			stuck = (m_gstate->m_evaluations - m_gstate->m_last_best_evaluation) >= thr;
		}
	}

	// Calculate this thread's assigned region
	int thread_count = m_gstate->m_thread_count;
	int lines_per_thread = m_height / thread_count;
	int region_start = m_thread_id * lines_per_thread;
	int region_end = (m_thread_id == thread_count - 1) ?
		m_height : region_start + lines_per_thread;
	if (m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY) {
		// Legacy mutation strategy: simple line decrement
		--m_currently_mutated_y;
		if (m_currently_mutated_y < 0)
			m_currently_mutated_y = pic->raster_lines.size() - 1;
		if (m_currently_mutated_y >= (int)pic->raster_lines.size())
			m_currently_mutated_y = 0;
	} else if (!m_allocation_line_weights.empty()) {
		++m_primary_mutation_count;
		if (m_primary_mutation_count % m_allocation_global_period == 0)
			m_currently_mutated_y = Random(static_cast<int>(pic->raster_lines.size()));
		else
			m_currently_mutated_y = SelectAllocatedLine(region_start, region_end);
	} else {
		// Prefer mutating lines in this thread's region (80% of the time)
		if (Random(100) < 80 && region_end > region_start) {
			m_currently_mutated_y = region_start + Random(region_end - region_start);
		}
		// Otherwise, allow some exploration outside the region (20% of time)
		else if (m_currently_mutated_y >= (int)pic->raster_lines.size()) {
			m_currently_mutated_y = 0;
		}
		else if (m_currently_mutated_y < 0) {
			m_currently_mutated_y = pic->raster_lines.size() - 1;
		}
	}

	// Keep an ANTIC 4 attribute proposal independent from raster and initial
	// register mutations. This makes its acceptance and eight-line cache cost
	// measurable, and tests exactly one coupled 4x8 cell at a time.
	if (pic->graphics_mode == GraphicsMode::Antic4 && Random(12) == 0)
	{
		if (transaction)
			transaction->SaveMemory();
		const int characterRow =
			Random(static_cast<int>(pic->antic4_attributes.size()));
		const int column = Random(antic4_visible_characters);
		pic->set_antic4_attribute(characterRow, column,
			!pic->antic4_attribute(characterRow, column));
		m_current_mutations[E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE]++;
		++m_mutation_attempt_count[E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE];
		++m_mutation_applied_count[E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE];
		++m_selector_attempt_count[E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE];
		++m_selector_applied_count[E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE];
		return;
	}

    // Batch memory register mutations: fixed probability 1/10 always
    int mem_prob = 10;
    if (Random(mem_prob) == 0) // mutate random init mem reg
	{
		if (transaction)
			transaction->SaveMemory();
		int c = 1;
		if (Random(2))
			c *= -1;
		if (Random(2))
			c *= 16;

		int targ;
		do {
			const int legacyCount = E_HPOSP3 + 1;
			targ = pic->graphics_mode == GraphicsMode::Antic4
				&& Random(legacyCount + 1) == legacyCount
				? E_COLOR3 : Random(legacyCount);
		} while (targ == E_COLBAK);

		pic->mem_regs_init[targ] += c;
	}
	raster_line& current_line = pic->raster_lines[m_currently_mutated_y];
	if (transaction)
		transaction->SaveMutationNeighborhood(m_currently_mutated_y);
	MutateLine(current_line, *pic);

    // Longer multi-line chains when stuck
    int chain_prob = stuck ? 5 : 20;
    int steps = stuck ? 30 : 10;
	if (Random(chain_prob) == 0)
	{
		for (int t = 0; t < steps; ++t)
		{
			if (m_gstate->m_optimizer == EvalGlobalState::OPT_LEGACY) {
				// Legacy chain mutation: simple decrement or random
				if (Random(2) && m_currently_mutated_y > 0)
					--m_currently_mutated_y;
				else
					m_currently_mutated_y = Random(pic->raster_lines.size());
			} else {
				// When jumping, prefer to stay within this thread's region
				if (Random(2) && m_currently_mutated_y > region_start)
					--m_currently_mutated_y;
				else if (m_currently_mutated_y < region_end - 1)
					++m_currently_mutated_y;
				else {
					// Fall back to anywhere in the thread's region
					m_currently_mutated_y = region_start + Random(region_end - region_start);
				}
			}

			raster_line& current_line = pic->raster_lines[m_currently_mutated_y];
			if (transaction)
				transaction->SaveMutationNeighborhood(m_currently_mutated_y);
			MutateLine(current_line, *pic);
		}
	}

	// recache any lines that have changed
	for (int y = 0; y < (int)m_height; ++y) {
		raster_line& rline = pic->raster_lines[y];
		if (rline.cache_key == NULL)
			rline.recache_insns(m_insn_seq_cache, m_insn_allocator);
	}
	assert(ValidateRasterPicture(*pic) == E_RASTER_VALID);
}

int Evaluator::SelectAllocatedLine(int first, int last)
{
	first = std::max(0, first);
	last = std::min(static_cast<int>(m_allocation_line_weights.size()), last);
	if (last <= first) return Random(static_cast<int>(m_height));
	double total = 0.0;
	for (int y = first; y < last; ++y)
		total += std::max(0.0, m_allocation_line_weights[y]);
	if (!(total > 0.0)) return first + Random(last - first);
	const double target = total * Random(1000000) / 1000000.0;
	double cumulative = 0.0;
	for (int y = first; y < last; ++y)
	{
		cumulative += std::max(0.0, m_allocation_line_weights[y]);
		if (target < cumulative) return y;
	}
	return last - 1;
}

void Evaluator::CaptureRegisterState(register_state& rs) const
{
	rs.reg_a = m_reg_a;
	rs.reg_x = m_reg_x;
	rs.reg_y = m_reg_y;

	memcpy(rs.mem_regs, m_mem_regs, sizeof rs.mem_regs);
}

void Evaluator::ApplyRegisterState(const register_state& rs)
{
	m_reg_a = rs.reg_a;
	m_reg_x = rs.reg_x;
	m_reg_y = rs.reg_y;
	memcpy(m_mem_regs, rs.mem_regs, sizeof rs.mem_regs);
}

void Evaluator::StoreLineRegs()
{
	CaptureRegisterState(m_old_reg_state);
}

void Evaluator::RestoreLineRegs()
{
	ApplyRegisterState(m_old_reg_state);
}

void Evaluator::ResetSpriteShiftStartArray()
{
	memset(m_sprite_shift_start_array, 0, sizeof m_sprite_shift_start_array);

	for(int i=0; i<4; ++i)
		m_sprite_shift_start_array[m_mem_regs[i+E_HPOSP0]] |= (1 << i);
}

int Evaluator::Random(int range)
{
	if (range <= 0)
		return 0;

	// XorShift algorithm - much faster than the current LFSR
	m_randseed ^= m_randseed << 13;
	m_randseed ^= m_randseed >> 17;
	m_randseed ^= m_randseed << 5;

	// Use rejection sampling for unbiased distribution
	uint32_t scaled = (uint32_t)(m_randseed & 0x7FFFFFFF) % range;
	return (int)scaled;
}
