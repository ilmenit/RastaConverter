#include "AcceptancePolicy.h"
#include "EvaluationContext.h"
#include <algorithm>
#include <cfloat>

// --- DLASPolicy implementation ---

void DLASPolicy::init(EvaluationContext& ctx) {
    // Mirror EvaluationContext initialization
    m_history.clear();
    // Use context's configured history length if present, else fallback to thread count
    int solutions_size = (ctx.m_history_length_config > 0) ? ctx.m_history_length_config : (ctx.m_thread_count > 1 ? ctx.m_thread_count : 1);
    if (solutions_size <= 0) solutions_size = 1;
    m_history.assign((size_t)solutions_size, DBL_MAX);
    m_historyIndex = 0;
    m_costMax = DBL_MAX;
    m_currentCost = DBL_MAX;
    m_N = (int)m_history.size();
    // Mirror context-visible legacy fields for save/state compatibility
    ctx.m_previous_results.assign(m_history.begin(), m_history.end());
    ctx.m_previous_results_index = 0;
    ctx.m_cost_max = DBL_MAX;
    ctx.m_current_cost = DBL_MAX;
    ctx.m_N = (int)ctx.m_previous_results.size();
}

void DLASPolicy::onInitialScore(double score, EvaluationContext& ctx) {
    const double init_margin = score * 0.1; // must match EvaluationContext::ReportInitialScore
    m_costMax = score + init_margin;
    m_currentCost = score;
    std::fill(m_history.begin(), m_history.end(), m_costMax);
    m_N = (int)m_history.size();
    m_historyIndex = 0;
    // Mirror to context for UI/save visibility
    ctx.m_current_cost = m_currentCost;
    ctx.m_cost_max = m_costMax;
    ctx.m_previous_results.assign(m_history.begin(), m_history.end());
    ctx.m_previous_results_index = 0;
    ctx.m_N = (int)m_history.size();
}

bool DLASPolicy::accept(double /*previousCost*/, double candidateCost, EvaluationContext& ctx) {
    // Accept if equal to current or below costMax (consistent with EvaluationContext acceptance)
    if (candidateCost == m_currentCost || candidateCost < m_costMax) {
        m_currentCost = candidateCost;
        // Keep context visible fields roughly in sync for UI/state files
        ctx.m_current_cost = m_currentCost;
        return true;
    }
    return false;
}

void DLASPolicy::onAccepted(double /*candidateCost*/, EvaluationContext& /*ctx*/) {
    // No-op; we update history every iteration in postIterationUpdate
}

void DLASPolicy::postIterationUpdate(EvaluationContext& ctx) {
    if (m_history.empty()) return;
    size_t l = (m_historyIndex % m_history.size());
    if (m_currentCost > m_history[l]) {
        m_history[l] = m_currentCost;
    } else if (m_currentCost < m_history[l]) {
        if (m_history[l] == m_costMax) --m_N;
        m_history[l] = m_currentCost;
        if (m_N <= 0) {
            m_costMax = *std::max_element(m_history.begin(), m_history.end());
            m_N = (int)std::count(m_history.begin(), m_history.end(), m_costMax);
        }
    }
    ++m_historyIndex;
    // Mirror into context ring for save/state
    if (!ctx.m_previous_results.empty()) {
        size_t li = (ctx.m_previous_results_index % ctx.m_previous_results.size());
        ctx.m_previous_results[li] = m_currentCost;
        ++ctx.m_previous_results_index;
    }
    ctx.m_current_cost = m_currentCost;
    ctx.m_cost_max = m_costMax;
    ctx.m_N = m_N;
}

void DLASPolicy::onStageSwitch(double currentCost, EvaluationContext& ctx, bool /*focusB*/) {
    // Full reseed: evaluate gives currentCost; raise costMax slightly and set entire history to costMax
    const double margin = std::max(5.0, currentCost * 0.02); // 2% or 5 absolute units
    m_currentCost = currentCost;
    m_costMax = currentCost + margin;
    if (!m_history.empty()) {
        std::fill(m_history.begin(), m_history.end(), m_costMax);
        m_N = (int)m_history.size();
    } else {
        m_N = 0;
    }
    // Mirror to context for UI/state
    ctx.m_current_cost = m_currentCost;
    ctx.m_cost_max = m_costMax;
    ctx.m_previous_results.assign(m_history.begin(), m_history.end());
    ctx.m_previous_results_index = 0;
    ctx.m_N = (int)m_history.size();
}

// --- LAHCPolicy implementation ---

void LAHCPolicy::init(EvaluationContext& /*ctx*/) {
    m_history.assign((size_t)m_historyLength, DBL_MAX);
    m_index = 0;
    m_currentCost = DBL_MAX;
}

void LAHCPolicy::onInitialScore(double score, EvaluationContext& ctx) {
    m_currentCost = score;
    std::fill(m_history.begin(), m_history.end(), score);
    m_index = 0;
    // Mirror into context for save/state compatibility
    ctx.m_previous_results.assign(m_history.begin(), m_history.end());
    ctx.m_previous_results_index = 0;
    ctx.m_current_cost = score;
    ctx.m_cost_max = score;
    ctx.m_N = (int)m_history.size();
}

bool LAHCPolicy::accept(double /*previousCost*/, double candidateCost, EvaluationContext& ctx) {
    // LAHC acceptance: cand <= history[l] OR cand < current_cost
    const double history_cost = m_history.empty() ? DBL_MAX : m_history[m_index % m_history.size()];
    const bool accept = (candidateCost <= history_cost) || (candidateCost < m_currentCost);
    if (accept) m_currentCost = candidateCost;
    ctx.m_current_cost = m_currentCost;
    return accept;
}

void LAHCPolicy::onAccepted(double /*candidateCost*/, EvaluationContext& /*ctx*/) {}

void LAHCPolicy::postIterationUpdate(EvaluationContext& ctx) {
    if (m_history.empty()) return;
    size_t li = (m_index % m_history.size());
    m_history[li] = m_currentCost;
    if (!ctx.m_previous_results.empty()) {
        size_t cli = (ctx.m_previous_results_index % ctx.m_previous_results.size());
        ctx.m_previous_results[cli] = m_currentCost;
        ++ctx.m_previous_results_index;
    }
    ctx.m_current_cost = m_currentCost;
    // For legacy fields, approximate cost_max as max of history (cheap enough for small L)
    if (!m_history.empty()) {
        ctx.m_cost_max = *std::max_element(m_history.begin(), m_history.end());
        ctx.m_N = (int)std::count(m_history.begin(), m_history.end(), ctx.m_cost_max);
    }
    ++m_index;
}

void LAHCPolicy::onStageSwitch(double currentCost, EvaluationContext& ctx, bool /*focusB*/) {
    // Full reseed: set entire history to current evaluated cost
    m_currentCost = currentCost;
    if (!m_history.empty()) {
        std::fill(m_history.begin(), m_history.end(), m_currentCost);
        m_index = 0;
    }
    // Mirror into context
    ctx.m_current_cost = m_currentCost;
    ctx.m_previous_results.assign(m_history.begin(), m_history.end());
    ctx.m_previous_results_index = 0;
    // For legacy fields, recompute cost_max and N
    if (!m_history.empty()) {
        ctx.m_cost_max = *std::max_element(m_history.begin(), m_history.end());
        ctx.m_N = (int)std::count(m_history.begin(), m_history.end(), ctx.m_cost_max);
    } else {
        ctx.m_cost_max = m_currentCost;
        ctx.m_N = 0;
    }
}


