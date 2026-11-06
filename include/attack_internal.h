#ifndef ATELIER2_ATTACK_INTERNAL_H
#define ATELIER2_ATTACK_INTERNAL_H

#include "attack.h"

uint64_t attack_next_random(uint64_t *state);
void attack_trace(AttackResult *result, const char *format, ...);
bool attack_named(const char names[][BC_NAME_SIZE], size_t count, const char *name);
uint64_t attack_total_resource(const Blockchain *chain, Consensus consensus,
                               const char names[][BC_NAME_SIZE], size_t count);
bool attack_initialize(const Blockchain *base, AttackResult *result);
bool attack_mine_empty(Blockchain *chain, Consensus consensus, uint64_t ticket);
bool attack_mine_pending_first(Blockchain *chain, Consensus consensus, uint64_t ticket);
uint64_t attack_rewards_for(const Blockchain *chain, const char names[][BC_NAME_SIZE], size_t count);

bool attack_run_51(const Blockchain *base, const AttackScenario *scenario, AttackResult *result);
bool attack_run_selfish(const Blockchain *base, const AttackScenario *scenario, AttackResult *result);
bool attack_run_withholding(const Blockchain *base, const AttackScenario *scenario, AttackResult *result);
bool attack_run_sybil(const Blockchain *base, const AttackScenario *scenario, AttackResult *result);

#endif
