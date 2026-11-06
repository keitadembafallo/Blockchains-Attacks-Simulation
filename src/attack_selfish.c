#include "attack_internal.h"

#include <stdio.h>

bool attack_run_selfish(const Blockchain *base, const AttackScenario *scenario, AttackResult *result) {
    Blockchain public_branch, private_branch;
    uint64_t random = scenario->seed ? scenario->seed : 1;
    size_t i;
    if (scenario->consensus != CONSENSUS_POW || scenario->attacker_count == 0 ||
        !attack_initialize(base, result)) return false;
    public_branch = result->reference_chain;
    private_branch = public_branch;
    for (i = 0; i < scenario->rounds; ++i) {
        uint64_t ticket = attack_next_random(&random);
        size_t producer = bc_select_producer_ticket(&public_branch, CONSENSUS_POW,
            (uint32_t)public_branch.block_count, public_branch.blocks[public_branch.block_count - 1].hash, ticket);
        if (producer < public_branch.node_count &&
            attack_named(scenario->attackers, scenario->attacker_count, public_branch.nodes[producer].name)) {
            if (!attack_mine_empty(&private_branch, CONSENSUS_POW, ticket)) return false;
            result->private_blocks++;
            attack_trace(result, "Tour %zu : avance privee %zu.", i + 1,
                         private_branch.block_count - public_branch.block_count);
        } else {
            if (!attack_mine_empty(&public_branch, CONSENSUS_POW, ticket)) return false;
            result->public_blocks++;
            if (private_branch.block_count == public_branch.block_count &&
                attack_next_random(&random) % 100 < scenario->gamma) {
                public_branch = private_branch;
                attack_trace(result, "Tour %zu : course gagnee par la branche privee (gamma).", i + 1);
            } else {
                attack_trace(result, "Tour %zu : bloc honnete publie.", i + 1);
            }
        }
    }
    result->reference_chain = public_branch;
    result->success = private_branch.block_count > public_branch.block_count;
    result->attacked_chain = result->success ? private_branch : public_branch;
    result->orphaned_blocks = result->success ? public_branch.block_count - 1 : 0;
    result->attacker_rewards = attack_rewards_for(&result->attacked_chain, scenario->attackers,
                                                   scenario->attacker_count);
    snprintf(result->message, sizeof(result->message), result->success ?
             "Selfish mining : branche privee plus longue." : "Selfish mining : aucun avantage final.");
    return true;
}
