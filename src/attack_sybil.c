#include "attack_internal.h"

#include <stdio.h>

bool attack_run_sybil(const Blockchain *base, const AttackScenario *scenario, AttackResult *result) {
    uint64_t random = scenario->seed ? scenario->seed : 1;
    Consensus consensus;
    size_t i;
    if (scenario->victim[0] == '\0' || bc_find_node(base, scenario->victim) == NULL ||
        scenario->victim_degree == 0 || scenario->sybil_count == 0 ||
        !attack_initialize(base, result)) return false;
    consensus = attack_total_resource(&result->reference_chain, CONSENSUS_POW, NULL, 0) > 0 ?
                CONSENSUS_POW : CONSENSUS_POS;
    if (attack_total_resource(&result->reference_chain, consensus, NULL, 0) == 0) return false;
    for (i = 0; i < scenario->rounds; ++i) {
        if (!attack_mine_empty(&result->reference_chain, consensus, attack_next_random(&random))) return false;
        attack_trace(result, "Tour %zu : annonce honnete retenue par les voisins Sybil.", i + 1);
    }
    result->sybil_neighbors = scenario->sybil_count < scenario->victim_degree ?
                              scenario->sybil_count : scenario->victim_degree;
    result->victim_reference_height = result->reference_chain.block_count - 1;
    result->victim_seen_height = result->victim_reference_height > 0 ?
                                 result->victim_reference_height - 1 : 0;
    result->success = result->sybil_neighbors == scenario->victim_degree &&
                      result->victim_seen_height < result->victim_reference_height;
    snprintf(result->message, sizeof(result->message), result->success ?
             "Sybil reussi : le pair victime est isole et voit un tip retarde." :
             "Sybil incomplet : les voisins Sybil ne saturent pas la victime.");
    return true;
}
