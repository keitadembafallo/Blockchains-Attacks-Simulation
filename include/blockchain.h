#ifndef ATELIER2_BLOCKCHAIN_H
#define ATELIER2_BLOCKCHAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#define BC_MAX_NODES 16
#define BC_MAX_BLOCKS 128
#define BC_MAX_PENDING_TRANSACTIONS 64
#define BC_MAX_TRANSACTIONS_PER_BLOCK 8
#define BC_MAX_PENDING_RESOURCE_UPDATES 32
#define BC_MAX_RESOURCE_UPDATES_PER_BLOCK 4
#define BC_NAME_SIZE 32
#define BC_MAX_DIFFICULTY 6

typedef enum {
    CONSENSUS_POW,
    CONSENSUS_POS,
    CONSENSUS_GENESIS
} Consensus;

typedef struct {
    char name[BC_NAME_SIZE];
    uint64_t hash_power;
    uint64_t stake; /* Capital bloque, utilise comme poids PoS. */
    uint64_t initial_hash_power;
    uint64_t initial_stake;
    uint64_t initial_balance; /* Solde disponible au genesis. */
    uint64_t balance;         /* Solde disponible, seul montant transferable. */
    uint64_t rewards;
} Node;

typedef struct {
    char sender[BC_NAME_SIZE];
    char recipient[BC_NAME_SIZE];
    uint64_t amount;
} Transaction;

typedef struct {
    char node_name[BC_NAME_SIZE];
    uint64_t hash_power;
    uint64_t stake;
} ResourceUpdate;

typedef struct {
    uint32_t height;
    Consensus consensus;
    char previous_hash[SHA256_HEX_SIZE];
    char hash[SHA256_HEX_SIZE];
    char producer[BC_NAME_SIZE];
    uint64_t selection_ticket;
    uint64_t nonce;
    uint64_t reward;
    Transaction transactions[BC_MAX_TRANSACTIONS_PER_BLOCK];
    size_t transaction_count;
    ResourceUpdate resource_updates[BC_MAX_RESOURCE_UPDATES_PER_BLOCK];
    size_t resource_update_count;
} Block;

typedef struct Blockchain Blockchain;

/* Extension future : 51 %, selfish mining et defenses fourniront leur strategie. */
typedef size_t (*ProducerStrategy)(const Blockchain *chain, Consensus consensus, uint32_t height,
                                   const char previous_hash[SHA256_HEX_SIZE], uint64_t ticket);

typedef struct {
    size_t blocks_produced;
    size_t accepted_transactions;
    size_t rejected_transactions;
    size_t forks;
    double mining_seconds;
} RunMetrics;

typedef enum {
    BC_CANDIDATE_READY,
    BC_CANDIDATE_CHAIN_NOT_STARTED,
    BC_CANDIDATE_CHAIN_INVALID,
    BC_CANDIDATE_CAPACITY_REACHED,
    BC_CANDIDATE_EMPTY_SELECTION,
    BC_CANDIDATE_TOO_MANY_TRANSACTIONS,
    BC_CANDIDATE_INVALID_INDEX,
    BC_CANDIDATE_DUPLICATE_INDEX,
    BC_CANDIDATE_NO_PRODUCER,
    BC_CANDIDATE_TRANSACTION_INVALID,
    BC_CANDIDATE_BALANCE_OVERFLOW,
    BC_CANDIDATE_INVALID_RESOURCE_UPDATE,
    BC_CANDIDATE_DUPLICATE_RESOURCE_TARGET,
    BC_CANDIDATE_STAKE_INSUFFICIENT
} BlockCandidateStatus;

struct Blockchain {
    Consensus consensus;
    unsigned difficulty;
    uint64_t seed;
    uint64_t block_reward;
    Node nodes[BC_MAX_NODES];
    size_t node_count;
    Transaction pending[BC_MAX_PENDING_TRANSACTIONS];
    size_t pending_count;
    ResourceUpdate pending_resource_updates[BC_MAX_PENDING_RESOURCE_UPDATES];
    size_t pending_resource_update_count;
    Block blocks[BC_MAX_BLOCKS];
    size_t block_count;
    ProducerStrategy producer_strategy;
};

void bc_init(Blockchain *chain, Consensus consensus, unsigned difficulty,
             uint64_t seed, uint64_t block_reward);
bool bc_add_node(Blockchain *chain, const char *name, uint64_t hash_power,
                 uint64_t locked_stake, uint64_t initial_total_balance);
bool bc_update_node(Blockchain *chain, const char *name,
                    uint64_t hash_power, uint64_t locked_stake, uint64_t initial_total_balance);
bool bc_remove_node(Blockchain *chain, const char *name);
bool bc_create_genesis(Blockchain *chain);
bool bc_add_transaction(Blockchain *chain, const Transaction *transaction);
bool bc_remove_pending_transaction(Blockchain *chain, size_t transaction_index);
bool bc_add_resource_update(Blockchain *chain, const ResourceUpdate *update);
bool bc_remove_pending_resource_update(Blockchain *chain, size_t update_index);
BlockCandidateStatus bc_preview_selected_block(const Blockchain *chain,
                                                Consensus consensus,
                                                const size_t transaction_indices[],
                                                size_t transaction_count,
                                                const size_t resource_update_indices[],
                                                size_t resource_update_count,
                                                Block *preview);
BlockCandidateStatus bc_produce_selected_block(Blockchain *chain,
                                                Consensus consensus,
                                                const size_t transaction_indices[],
                                                size_t transaction_count,
                                                const size_t resource_update_indices[],
                                                size_t resource_update_count,
                                                RunMetrics *metrics);
BlockCandidateStatus bc_preview_selected_block_ticket(const Blockchain *chain, Consensus consensus,
                                                       const size_t transaction_indices[], size_t transaction_count,
                                                       const size_t resource_update_indices[], size_t resource_update_count,
                                                       uint64_t ticket, Block *preview);
BlockCandidateStatus bc_produce_selected_block_ticket(Blockchain *chain, Consensus consensus,
                                                       const size_t transaction_indices[], size_t transaction_count,
                                                       const size_t resource_update_indices[], size_t resource_update_count,
                                                       uint64_t ticket, RunMetrics *metrics);
BlockCandidateStatus bc_produce_empty_block_ticket(Blockchain *chain, Consensus consensus,
                                                    uint64_t ticket, RunMetrics *metrics);
size_t bc_select_producer_ticket(const Blockchain *chain, Consensus consensus, uint32_t height,
                                 const char previous_hash[SHA256_HEX_SIZE], uint64_t ticket);
bool bc_produce_next_block(Blockchain *chain, RunMetrics *metrics);
bool bc_run(Blockchain *chain, size_t blocks_to_produce, RunMetrics *metrics);
bool bc_validate(const Blockchain *chain);

size_t bc_honest_producer_strategy(const Blockchain *chain, Consensus consensus, uint32_t height,
                                   const char previous_hash[SHA256_HEX_SIZE], uint64_t ticket);
void bc_set_producer_strategy(Blockchain *chain, ProducerStrategy strategy);
const char *bc_consensus_name(Consensus consensus);
const char *bc_candidate_status_message(BlockCandidateStatus status);
const Node *bc_find_node(const Blockchain *chain, const char *name);
bool bc_node_total_balance(const Node *node, uint64_t *total);

#endif
