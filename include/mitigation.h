#ifndef ATELIER2_MITIGATION_H
#define ATELIER2_MITIGATION_H

#include "detection.h"

#define MITIGATION_MAX_INTERVENTIONS 8
#define MITIGATION_MIN_CONFIRMATIONS 6
#define MITIGATION_MAX_REORGANIZATION 1

typedef struct {
    bool enabled;
    bool protect_51;
    bool protect_selfish;
    bool protect_withholding;
    bool protect_sybil;
} MitigationConfig;

typedef struct {
    AttackType attack;
    char target[BC_NAME_SIZE];
    char action[96];
    char outcome[128];
    unsigned score;
    DetectionSeverity severity;
    size_t tick;
    bool applied;
} MitigationIntervention;

typedef struct {
    bool evaluated;
    bool applied;
    bool adoption_blocked;
    bool protected_valid;
    size_t required_confirmations;
    size_t allowed_sybil_neighbors;
    uint64_t prevented_attacker_reward;
    Blockchain protected_chain;
    MitigationIntervention interventions[MITIGATION_MAX_INTERVENTIONS];
    size_t intervention_count;
} MitigationResult;

void mitigation_config_default(MitigationConfig *config);
bool mitigation_apply(MetricsExperiment *experiment, const DetectionReport *report,
                      const MitigationConfig *config, MitigationResult *result);
void mitigation_print_report(const MitigationResult *result);
bool mitigation_export_csv(const MitigationResult *result, const MetricsExperiment *experiment);

#endif
