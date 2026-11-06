#include "attack_internal.h"

#include <stdio.h>
#include <string.h>

bool attack_run_51(const Blockchain *base, const AttackScenario *scenario, AttackResult *result) {
    Blockchain public_branch, private_branch;
    Transaction payment = {{0}, {0}, 0};
    Transaction conflict = {{0}, {0}, 0};
    uint64_t random = scenario->seed ? scenario->seed : 1;
    uint64_t all, attackers;
    size_t i;

    if ((scenario->consensus != CONSENSUS_POW && scenario->consensus != CONSENSUS_POS) ||
        scenario->attacker_count == 0 || scenario->amount == 0) return false;
    if (!attack_initialize(base, result)) return false;
    all = attack_total_resource(&result->reference_chain, scenario->consensus, NULL, 0);
    attackers = attack_total_resource(&result->reference_chain, scenario->consensus,
                                      scenario->attackers, scenario->attacker_count);
    if (all == 0 || attackers * 2 <= all) {
        snprintf(result->message, sizeof(result->message), "Refus : la coalition ne depasse pas 50 %% de la ressource.");
        return false;
    }
    public_branch = result->reference_chain;
    private_branch = result->reference_chain;
    snprintf(payment.sender, sizeof(payment.sender), "%s", scenario->spender);
    snprintf(payment.recipient, sizeof(payment.recipient), "%s", scenario->merchant);
    payment.amount = scenario->amount;
    snprintf(conflict.sender, sizeof(conflict.sender), "%s", scenario->spender);
    snprintf(conflict.recipient, sizeof(conflict.recipient), "%s", scenario->private_recipient);
    conflict.amount = scenario->amount;
    if (!bc_add_transaction(&public_branch, &payment) || !bc_add_transaction(&private_branch, &conflict) ||
        !attack_mine_pending_first(&public_branch, scenario->consensus, attack_next_random(&random)) ||
        !attack_mine_pending_first(&private_branch, scenario->consensus, attack_next_random(&random))) {
        snprintf(result->message, sizeof(result->message), "Transactions de double depense invalides.");
        return false;
    }
    for (i = 0; i < scenario->confirmations; ++i) {
        if (!attack_mine_empty(&public_branch, scenario->consensus, attack_next_random(&random))) return false;
    }
    for (i = 0; i < scenario->rounds; ++i) {
        uint64_t ticket = attack_next_random(&random);
        size_t producer = bc_select_producer_ticket(&public_branch, scenario->consensus,
                                                     (uint32_t)public_branch.block_count,
                                                     public_branch.blocks[public_branch.block_count - 1].hash, ticket);
        if (producer < public_branch.node_count &&
            attack_named(scenario->attackers, scenario->attacker_count, public_branch.nodes[producer].name)) {
            if (!attack_mine_empty(&private_branch, scenario->consensus, ticket)) return false;
            result->private_blocks++;
            attack_trace(result, "Tour %zu : bloc prive retenu par %s.", i + 1, public_branch.nodes[producer].name);
        } else {
            if (!attack_mine_empty(&public_branch, scenario->consensus, ticket)) return false;
            result->public_blocks++;
            attack_trace(result, "Tour %zu : bloc honnete publie.", i + 1);
        }
        if (private_branch.block_count > public_branch.block_count) break;
    }
    result->reference_chain = public_branch;
    result->success = private_branch.block_count > public_branch.block_count;
    result->orphaned_blocks = result->success ? public_branch.block_count - 1 : 0;
    result->reorganization_depth = result->success ? public_branch.block_count - 1 : 0;
    result->merchant_loss = result->success ? scenario->amount : 0;
    result->attacked_chain = result->success ? private_branch : public_branch;
    result->attacker_rewards = attack_rewards_for(&result->attacked_chain, scenario->attackers,
                                                   scenario->attacker_count);
    snprintf(result->message, sizeof(result->message), result->success ?
             "51 %% reussi : la branche privee remplace le paiement marchand." :
             "51 %% non abouti avant la limite de tours.");
    return true;
}
