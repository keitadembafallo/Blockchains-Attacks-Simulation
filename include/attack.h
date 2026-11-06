#ifndef ATELIER2_ATTACK_H
#define ATELIER2_ATTACK_H

#include "blockchain.h"

#define ATTACK_MAX_TRACE 160
#define ATTACK_TRACE_SIZE 160

typedef enum {
    ATTACK_51,
    ATTACK_SELFISH_MINING,
    ATTACK_BLOCK_WITHHOLDING,
    ATTACK_SYBIL
} AttackType;

typedef struct {
    AttackType type;
    Consensus consensus;
    char attackers[BC_MAX_NODES][BC_NAME_SIZE];
    size_t attacker_count;
    char merchant[BC_NAME_SIZE];
    char private_recipient[BC_NAME_SIZE];
    char spender[BC_NAME_SIZE];
    uint64_t amount;
    size_t confirmations;
    size_t rounds;
    unsigned gamma;
    char pool_members[BC_MAX_NODES][BC_NAME_SIZE];
    size_t pool_member_count;
    char victim[BC_NAME_SIZE];
    size_t sybil_count;
    size_t victim_degree;
    size_t propagation_delay;
    uint64_t seed;
} AttackScenario;

typedef struct {
    bool executed;
    bool success;
    bool reference_valid;
    bool attacked_valid;
    size_t rounds_completed;
    size_t public_blocks;
    size_t private_blocks;
    size_t orphaned_blocks;
    size_t reorganization_depth;
    uint64_t merchant_loss;
    uint64_t attacker_rewards;
    uint64_t pool_reference_rewards;
    uint64_t pool_attacked_rewards;
    size_t victim_reference_height;
    size_t victim_seen_height;
    size_t sybil_neighbors;
    char message[160];
    Blockchain reference_chain;
    Blockchain attacked_chain;
    char trace[ATTACK_MAX_TRACE][ATTACK_TRACE_SIZE];
    size_t trace_count;
} AttackResult;

const char *attack_type_name(AttackType type);
bool attack_parse_type(const char *text, AttackType *type);
bool attack_run(const Blockchain *base, const AttackScenario *scenario, AttackResult *result);
void attack_print_result(const AttackResult *result);

#endif
