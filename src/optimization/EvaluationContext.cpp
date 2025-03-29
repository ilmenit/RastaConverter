#include "EvaluationContext.h"
#include <cfloat>

EvaluationContext::EvaluationContext()
    : m_best_result(DBL_MAX)
    , m_previous_results_index(0)
    , m_cost_max(DBL_MAX)
    , m_N(0)
    , m_current_cost(DBL_MAX)
    , m_time_start(time(NULL))
{
    memset(m_mutation_stats, 0, sizeof(m_mutation_stats));
    m_previous_save_time = std::chrono::steady_clock::now();
}

EvaluationContext::~EvaluationContext() = default;