#include "mitigation.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool enabled_for(const MitigationConfig *config, AttackType type) {
    if (!config->enabled) return false;
    if (type == ATTACK_51) return config->protect_51;
    if (type == ATTACK_SELFISH_MINING) return config->protect_selfish;
    if (type == ATTACK_BLOCK_WITHHOLDING) return config->protect_withholding;
    return config->protect_sybil;
}

static const DetectionAlert *matching_alert(const DetectionReport *report, AttackType type) {
    size_t i;
    const char *name = type == ATTACK_51 ? "51% / DOUBLE_DEPENSE" :
                       type == ATTACK_SELFISH_MINING ? "SELFISH_MINING" :
                       type == ATTACK_BLOCK_WITHHOLDING ? "BLOCK_WITHHOLDING" : "SYBIL";
    for (i = 0; i < report->alert_count; ++i) {
        if (strcmp(report->alerts[i].attack, name) == 0 &&
            report->alerts[i].severity >= SEVERITY_HIGH) return &report->alerts[i];
    }
    return NULL;
}

static void add_intervention(MitigationResult *result, AttackType attack,
                             const DetectionAlert *alert, const char *target,
                             const char *action, const char *outcome, size_t tick) {
    MitigationIntervention *entry;
    if (result->intervention_count == MITIGATION_MAX_INTERVENTIONS) return;
    entry = &result->interventions[result->intervention_count++];
    memset(entry, 0, sizeof(*entry));
    entry->attack = attack;
    entry->score = alert->score;
    entry->severity = alert->severity;
    entry->tick = tick;
    entry->applied = true;
    snprintf(entry->target, sizeof(entry->target), "%s", target);
    snprintf(entry->action, sizeof(entry->action), "%s", action);
    snprintf(entry->outcome, sizeof(entry->outcome), "%s", outcome);
}

void mitigation_config_default(MitigationConfig *config) {
    if (config == NULL) return;
    config->enabled = true;
    config->protect_51 = true;
    config->protect_selfish = true;
    config->protect_withholding = true;
    config->protect_sybil = true;
}

bool mitigation_apply(MetricsExperiment *experiment, const DetectionReport *report,
                      const MitigationConfig *config, MitigationResult *result) {
    const DetectionAlert *alert;
    const AttackScenario *scenario;
    const AttackResult *attack;
    size_t tick;
    if (experiment == NULL || report == NULL || config == NULL || result == NULL) return false;
    memset(result, 0, sizeof(*result));
    result->evaluated = true;
    experiment->mitigation_applied = false;
    experiment->adoption_blocked = false;
    scenario = &experiment->scenario;
    attack = &experiment->attack;
    result->protected_chain = attack->attacked_chain;
    result->required_confirmations = scenario->confirmations;
    result->allowed_sybil_neighbors = scenario->victim_degree;
    experiment->protected = experiment->malicious;
    experiment->protected_available = true;
    if (!enabled_for(config, scenario->type) ||
        (alert = matching_alert(report, scenario->type)) == NULL) {
        result->protected_valid = bc_validate(&result->protected_chain);
        return true;
    }
    tick = attack->rounds_completed;
    result->applied = true;
    experiment->mitigation_applied = true;
    if (scenario->type == ATTACK_51) {
        result->required_confirmations = scenario->confirmations < MITIGATION_MIN_CONFIRMATIONS ?
                                         MITIGATION_MIN_CONFIRMATIONS : scenario->confirmations;
        result->protected_chain = attack->reference_chain;
        result->adoption_blocked = attack->reorganization_depth > MITIGATION_MAX_REORGANIZATION;
        add_intervention(result, scenario->type, alert, alert->suspect,
                         "Confirmations renforcees et reorganisation rejetee",
                         "La branche publique est conservee ; paiement marchand protege.", tick);
    } else if (scenario->type == ATTACK_SELFISH_MINING) {
        result->protected_chain = attack->reference_chain;
        result->adoption_blocked = true;
        add_intervention(result, scenario->type, alert, alert->suspect,
                         "Publication tardive mise en quarantaine",
                         "La branche publique connue est privilegiee.", tick);
    } else if (scenario->type == ATTACK_BLOCK_WITHHOLDING) {
        result->prevented_attacker_reward = attack->attacker_rewards;
        experiment->protected.attacker_rewards = 0;
        add_intervention(result, scenario->type, alert, alert->suspect,
                         "Mineur limite dans la repartition de pool",
                         "Recompense du mineur retenant un bloc annulee.", tick);
    } else {
        result->protected_chain = attack->reference_chain;
        result->allowed_sybil_neighbors = scenario->victim_degree / 2;
        result->adoption_blocked = true;
        add_intervention(result, scenario->type, alert, scenario->victim,
                         "Connexions Sybil excedentaires refusees",
                         "Des voisins honnetes restaurent la diffusion vers la victime.", tick);
    }
    result->protected_valid = bc_validate(&result->protected_chain);
    if (scenario->type != ATTACK_BLOCK_WITHHOLDING) experiment->protected = experiment->normal;
    experiment->protected_available = true;
    experiment->adoption_blocked = result->adoption_blocked;
    return result->protected_valid;
}

void mitigation_print_report(const MitigationResult *result) {
    size_t i;
    puts("\n========== MITIGATION / CHAINE PROTEGEE ==========");
    if (!result->applied) {
        puts("Aucune contre-mesure appliquee : protection desactivee ou alerte insuffisante.");
        return;
    }
    for (i = 0; i < result->intervention_count; ++i) {
        const MitigationIntervention *entry = &result->interventions[i];
        printf("[%s | %u] %s | cible: %s\n  Action : %s\n  Resultat : %s\n",
               detection_severity_name(entry->severity), entry->score,
               attack_type_name(entry->attack), entry->target, entry->action, entry->outcome);
    }
    if (result->required_confirmations) printf("Confirmations requises : %zu\n", result->required_confirmations);
    if (result->allowed_sybil_neighbors) printf("Voisins Sybil autorises : %zu\n", result->allowed_sybil_neighbors);
    if (result->prevented_attacker_reward) printf("Recompense malveillante evitee : %" PRIu64 "\n", result->prevented_attacker_reward);
    printf("Adoption de branche attaquee : %s\n", result->adoption_blocked ? "BLOQUEE" : "AUTORISEE");
    printf("Chaine protegee : %s\n", result->protected_valid ? "VALIDE" : "INVALIDE");
}

bool mitigation_export_csv(const MitigationResult *result, const MetricsExperiment *experiment) {
    char path[METRICS_PATH_SIZE + 32];
    FILE *file;
    size_t i;
    if (result == NULL || experiment == NULL || !experiment->export_path[0] ||
        snprintf(path, sizeof(path), "%s\\mitigation.csv", experiment->export_path) >= (int)sizeof(path)) return false;
    file = fopen(path, "w");
    if (file == NULL) return false;
    fprintf(file, "attack,target,score,severity,tick,action,outcome,applied,adoption_blocked\n");
    for (i = 0; i < result->intervention_count; ++i) {
        const MitigationIntervention *entry = &result->interventions[i];
        fprintf(file, "%s,%s,%u,%s,%zu,%s,%s,%d,%d\n", attack_type_name(entry->attack),
                entry->target, entry->score, detection_severity_name(entry->severity), entry->tick,
                entry->action, entry->outcome, entry->applied ? 1 : 0,
                result->adoption_blocked ? 1 : 0);
    }
    return fclose(file) == 0;
}
