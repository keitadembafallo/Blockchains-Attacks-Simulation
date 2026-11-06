#include "attack_internal.h"

#include <stdio.h>

bool attack_run_withholding(const Blockchain *base, const AttackScenario *scenario, AttackResult *result) {
    Blockchain honest, attacked;
    uint64_t random = scenario->seed ? scenario->seed : 1;
    uint64_t pool_power;
    size_t i;
    if (scenario->consensus != CONSENSUS_POW || scenario->pool_member_count < 2 ||
        scenario->attacker_count == 0 || !attack_initialize(base, result)) return false;
    honest = result->reference_chain;
    attacked = honest;
    pool_power = attack_total_resource(base, CONSENSUS_POW, scenario->pool_members, scenario->pool_member_count);
    if (pool_power == 0) {
        snprintf(result->message, sizeof(result->message), "Pool invalide : puissance de calcul totale nulle.");
        return false;
    }
    for (i = 0; i < scenario->attacker_count; ++i) {
        if (!attack_named(scenario->pool_members, scenario->pool_member_count, scenario->attackers[i])) {
            snprintf(result->message, sizeof(result->message), "Le mineur malveillant doit appartenir a la pool.");
            return false;
        }
    }
    for (i = 0; i < scenario->rounds; ++i) {
        uint64_t ticket = attack_next_random(&random);
        size_t producer = bc_select_producer_ticket(&honest, CONSENSUS_POW, (uint32_t)honest.block_count,
            honest.blocks[honest.block_count - 1].hash, ticket);
        if (!attack_mine_empty(&honest, CONSENSUS_POW, ticket)) return false;
        if (producer < honest.node_count && attack_named(scenario->attackers, scenario->attacker_count,
                                                         honest.nodes[producer].name)) {
            attack_trace(result, "Tour %zu : %s retient un bloc valide de la pool.", i + 1,
                         honest.nodes[producer].name);
            result->private_blocks++;
        } else {
            if (!attack_mine_empty(&attacked, CONSENSUS_POW, ticket)) return false;
            result->public_blocks++;
            attack_trace(result, "Tour %zu : bloc de pool honnete publie.", i + 1);
        }
    }
    result->reference_chain = honest;
    result->attacked_chain = attacked;
    result->pool_reference_rewards = scenario->rounds * honest.block_reward;
    result->pool_attacked_rewards = result->public_blocks * attacked.block_reward;
    result->success = result->pool_attacked_rewards < result->pool_reference_rewards;
    result->attacker_rewards = attack_total_resource(base, CONSENSUS_POW, scenario->attackers,
                                                      scenario->attacker_count) *
                               result->pool_attacked_rewards / pool_power;
    snprintf(result->message, sizeof(result->message), "Block withholding : production de la pool reduite.");
    return true;
}
