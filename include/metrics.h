#ifndef ATELIER2_METRICS_H
#define ATELIER2_METRICS_H

#include "attack.h"

#define METRICS_MAX_EVENTS ATTACK_MAX_TRACE
#define METRICS_DETAIL_SIZE 160
#define METRICS_PATH_SIZE 512

typedef struct {
    size_t tick;
    char mode[10];
    char type[24];
    char node[BC_NAME_SIZE];
    char branch[16];
    size_t height;
    size_t delay_ticks;
    uint64_t amount;
    char detail[METRICS_DETAIL_SIZE];
} MetricEvent;

typedef struct {
    size_t blocks_produced, public_blocks, private_blocks, withheld_blocks, orphaned_blocks;
    size_t reorganization_depth, invalid_chains, double_spends, sybil_identities, sybil_neighbors;
    double orphan_rate, average_delay, maximum_delay;
    uint64_t total_rewards;
    uint64_t attacker_rewards;
    size_t event_count;
    MetricEvent events[METRICS_MAX_EVENTS];
} MetricSummary;

typedef struct {
    AttackScenario scenario;
    AttackResult attack;
    MetricSummary normal;
    MetricSummary malicious;
    MetricSummary protected;
    bool protected_available;
    bool mitigation_applied;
    bool adoption_blocked;
    char run_id[32];
    char export_path[METRICS_PATH_SIZE];
} MetricsExperiment;

bool metrics_run_experiment(const Blockchain *base, const AttackScenario *scenario, MetricsExperiment *experiment);
void metrics_summarize_chain(const Blockchain *chain, MetricSummary *summary);
void metrics_print_experiment(const MetricsExperiment *experiment);
bool metrics_default_directory(char output[METRICS_PATH_SIZE]);
bool metrics_export_csv(MetricsExperiment *experiment, const char *directory);

#endif
