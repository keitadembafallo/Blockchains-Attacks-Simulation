#ifndef ATELIER2_EVALUATION_H
#define ATELIER2_EVALUATION_H

#include "mitigation.h"

#define EVALUATION_MAX_SEEDS 10
#define EVALUATION_MAX_VALUES 3
#define EVALUATION_MAX_RUNS 160
#define EVALUATION_PATH_SIZE 512

typedef struct {
    uint64_t seed_base;
    size_t seed_count;
    size_t node_counts[EVALUATION_MAX_VALUES], node_count_count;
    size_t delays[EVALUATION_MAX_VALUES], delay_count;
    bool attacks[4];
    bool quick_profile;
    const char *output_directory;
} EvaluationConfig;

typedef struct {
    uint64_t seed;
    size_t nodes, delay;
    AttackType attack;
    Consensus consensus;
    bool attack_success, attack_blocked, mitigation_applied;
    bool fixed_positive, adaptive_positive;
    size_t fixed_tick, adaptive_tick;
    double normal_loss, attack_loss, protected_loss;
    double residual_impact;
    uint64_t normal_work, attack_work, protected_work;
    double simulation_cpu, detection_cpu, mitigation_cpu;
    size_t interventions;
} EvaluationRun;

typedef struct {
    EvaluationConfig config;
    EvaluationRun runs[EVALUATION_MAX_RUNS];
    size_t run_count;
    char output_path[EVALUATION_PATH_SIZE];
} EvaluationReport;

void evaluation_config_default(EvaluationConfig *config);
void evaluation_config_quick(EvaluationConfig *config);
bool evaluation_run_campaign(const EvaluationConfig *config, EvaluationReport *report);
void evaluation_print_report(const EvaluationReport *report);

#endif
