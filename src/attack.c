#include "attack_internal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

uint64_t attack_next_random(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

void attack_trace(AttackResult *result, const char *format, ...) {
    va_list arguments;
    if (result->trace_count == ATTACK_MAX_TRACE) return;
    va_start(arguments, format);
    vsnprintf(result->trace[result->trace_count], ATTACK_TRACE_SIZE, format, arguments);
    va_end(arguments);
    result->trace_count++;
}

bool attack_named(const char names[][BC_NAME_SIZE], size_t count, const char *name) {
    size_t i;
    for (i = 0; i < count; ++i) if (strcmp(names[i], name) == 0) return true;
    return false;
}

uint64_t attack_total_resource(const Blockchain *chain, Consensus consensus,
                               const char names[][BC_NAME_SIZE], size_t count) {
    uint64_t total = 0;
    size_t i;
    for (i = 0; i < chain->node_count; ++i) {
        if (count == 0 || attack_named(names, count, chain->nodes[i].name)) {
            total += consensus == CONSENSUS_POW ? chain->nodes[i].hash_power : chain->nodes[i].stake;
        }
    }
    return total;
}

bool attack_initialize(const Blockchain *base, AttackResult *result) {
    result->reference_chain = *base;
    if (result->reference_chain.block_count == 0 &&
        !bc_create_genesis(&result->reference_chain)) return false;
    if (!bc_validate(&result->reference_chain)) return false;
    result->attacked_chain = result->reference_chain;
    return true;
}

bool attack_mine_empty(Blockchain *chain, Consensus consensus, uint64_t ticket) {
    return bc_produce_empty_block_ticket(chain, consensus, ticket, NULL) == BC_CANDIDATE_READY;
}

bool attack_mine_pending_first(Blockchain *chain, Consensus consensus, uint64_t ticket) {
    const size_t index[] = {0};
    return bc_produce_selected_block_ticket(chain, consensus, index, 1, NULL, 0, ticket, NULL) ==
           BC_CANDIDATE_READY;
}

uint64_t attack_rewards_for(const Blockchain *chain, const char names[][BC_NAME_SIZE], size_t count) {
    uint64_t total = 0;
    size_t i;
    for (i = 0; i < chain->node_count; ++i) {
        if (attack_named(names, count, chain->nodes[i].name)) total += chain->nodes[i].rewards;
    }
    return total;
}

const char *attack_type_name(AttackType type) {
    switch (type) {
        case ATTACK_51: return "51% / double spending";
        case ATTACK_SELFISH_MINING: return "Selfish mining";
        case ATTACK_BLOCK_WITHHOLDING: return "Block withholding";
        default: return "Sybil P2P";
    }
}

bool attack_parse_type(const char *text, AttackType *type) {
    if (strcmp(text, "51") == 0 || strcmp(text, "51pct") == 0) *type = ATTACK_51;
    else if (strcmp(text, "selfish") == 0) *type = ATTACK_SELFISH_MINING;
    else if (strcmp(text, "withholding") == 0) *type = ATTACK_BLOCK_WITHHOLDING;
    else if (strcmp(text, "sybil") == 0) *type = ATTACK_SYBIL;
    else return false;
    return true;
}

bool attack_run(const Blockchain *base, const AttackScenario *scenario, AttackResult *result) {
    if (base == NULL || scenario == NULL || result == NULL) return false;
    memset(result, 0, sizeof(*result));
    if (scenario->type == ATTACK_51) result->executed = attack_run_51(base, scenario, result);
    else if (scenario->type == ATTACK_SELFISH_MINING) result->executed = attack_run_selfish(base, scenario, result);
    else if (scenario->type == ATTACK_BLOCK_WITHHOLDING) result->executed = attack_run_withholding(base, scenario, result);
    else result->executed = attack_run_sybil(base, scenario, result);
    result->rounds_completed = scenario->rounds;
    result->reference_valid = result->executed && bc_validate(&result->reference_chain);
    result->attacked_valid = result->executed && bc_validate(&result->attacked_chain);
    return result->executed;
}

void attack_print_result(const AttackResult *result) {
    size_t i;
    puts("\n========== RESULTAT D'ATTAQUE ==========");
    printf("%s\n", result->message);
    printf("Succes : %s | reference : %s | attaque : %s\n", result->success ? "OUI" : "NON",
           result->reference_valid ? "VALIDE" : "INVALIDE", result->attacked_valid ? "VALIDE" : "INVALIDE");
    printf("Blocs publics : %zu | prives/retenus : %zu | orphelins : %zu\n",
           result->public_blocks, result->private_blocks, result->orphaned_blocks);
    printf("Reorganisation : %zu | perte marchand : %" PRIu64 "\n",
           result->reorganization_depth, result->merchant_loss);
    if (result->pool_reference_rewards > 0) {
        printf("Pool reference : %" PRIu64 " | pool attaquee : %" PRIu64 "\n",
               result->pool_reference_rewards, result->pool_attacked_rewards);
    }
    if (result->victim_reference_height > 0 || result->sybil_neighbors > 0) {
        printf("Victime : hauteur globale %zu, vue %zu, voisins Sybil %zu\n",
               result->victim_reference_height, result->victim_seen_height, result->sybil_neighbors);
    }
    for (i = 0; i < result->trace_count; ++i) printf("  - %s\n", result->trace[i]);
}
