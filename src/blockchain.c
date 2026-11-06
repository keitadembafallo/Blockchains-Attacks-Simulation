#include "blockchain.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static bool append_format(char *buffer, size_t capacity, size_t *used, const char *format, ...) {
    int written;
    va_list arguments;

    va_start(arguments, format);
    written = vsnprintf(buffer + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *used) return false;
    *used += (size_t)written;
    return true;
}

static bool hash_block(const Block *block, char output[SHA256_HEX_SIZE]) {
    char serialized[4096];
    size_t used = 0;
    size_t i;

    if (!append_format(serialized, sizeof(serialized), &used, "%" PRIu32 "|%u|%s|%s|%" PRIu64 "|%" PRIu64 "|%" PRIu64 "|%zu|%zu|",
                       block->height, (unsigned)block->consensus, block->previous_hash, block->producer, block->nonce,
                       block->selection_ticket, block->reward, block->transaction_count,
                       block->resource_update_count)) return false;
    for (i = 0; i < block->transaction_count; ++i) {
        const Transaction *transaction = &block->transactions[i];
        if (!append_format(serialized, sizeof(serialized), &used, "%s>%s:%" PRIu64 "|",
                           transaction->sender, transaction->recipient, transaction->amount)) return false;
    }
    for (i = 0; i < block->resource_update_count; ++i) {
        const ResourceUpdate *update = &block->resource_updates[i];
        if (!append_format(serialized, sizeof(serialized), &used, "%s=%" PRIu64 ":%" PRIu64 "|",
                           update->node_name, update->hash_power, update->stake)) return false;
    }
    sha256_hex((const uint8_t *)serialized, used, output);
    return true;
}

static bool meets_difficulty(const char hash[SHA256_HEX_SIZE], unsigned difficulty) {
    unsigned i;
    for (i = 0; i < difficulty; ++i) {
        if (hash[i] != '0') return false;
    }
    return true;
}

static double now_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static size_t node_index(const Blockchain *chain, const char *name) {
    size_t i;
    for (i = 0; i < chain->node_count; ++i) {
        if (strcmp(chain->nodes[i].name, name) == 0) return i;
    }
    return SIZE_MAX;
}

static bool apply_resource_update_to_nodes(Node nodes[], size_t node_count,
                                           const ResourceUpdate *update) {
    size_t i;
    if (update == NULL || update->node_name[0] == '\0') return false;
    for (i = 0; i < node_count; ++i) {
        if (strcmp(nodes[i].name, update->node_name) == 0) {
            if (update->stake > nodes[i].stake) {
                uint64_t lock_amount = update->stake - nodes[i].stake;
                if (nodes[i].balance < lock_amount) return false;
                nodes[i].balance -= lock_amount;
            } else if (update->stake < nodes[i].stake) {
                uint64_t unlock_amount = nodes[i].stake - update->stake;
                if (UINT64_MAX - nodes[i].balance < unlock_amount) return false;
                nodes[i].balance += unlock_amount;
            }
            nodes[i].hash_power = update->hash_power;
            nodes[i].stake = update->stake;
            return true;
        }
    }
    return false;
}

static bool apply_transaction(uint64_t balances[BC_MAX_NODES], const Blockchain *chain,
                              const Transaction *transaction) {
    size_t sender = node_index(chain, transaction->sender);
    size_t recipient = node_index(chain, transaction->recipient);

    if (sender == SIZE_MAX || recipient == SIZE_MAX || transaction->amount == 0 ||
        balances[sender] < transaction->amount) return false;
    balances[sender] -= transaction->amount;
    if (UINT64_MAX - balances[recipient] < transaction->amount) return false;
    balances[recipient] += transaction->amount;
    return true;
}

static void current_balances(const Blockchain *chain, uint64_t balances[BC_MAX_NODES]) {
    size_t i;
    for (i = 0; i < chain->node_count; ++i) balances[i] = chain->nodes[i].balance;
}

static uint64_t fnv1a_mix(uint64_t value, const unsigned char *data, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i) {
        value ^= data[i];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

void bc_init(Blockchain *chain, Consensus consensus, unsigned difficulty,
             uint64_t seed, uint64_t block_reward) {
    memset(chain, 0, sizeof(*chain));
    chain->consensus = consensus;
    chain->difficulty = difficulty;
    chain->seed = seed;
    chain->block_reward = block_reward;
    chain->producer_strategy = bc_honest_producer_strategy;
}

bool bc_add_node(Blockchain *chain, const char *name, uint64_t hash_power,
                 uint64_t locked_stake, uint64_t initial_total_balance) {
    Node *node;
    if (chain->node_count == BC_MAX_NODES || name == NULL || name[0] == '\0' ||
        strlen(name) >= BC_NAME_SIZE || strchr(name, ';') != NULL ||
        strcmp(name, "GENESIS") == 0 || locked_stake > initial_total_balance ||
        node_index(chain, name) != SIZE_MAX) return false;
    node = &chain->nodes[chain->node_count++];
    memset(node, 0, sizeof(*node));
    snprintf(node->name, sizeof(node->name), "%s", name);
    node->hash_power = hash_power;
    node->stake = locked_stake;
    node->initial_hash_power = hash_power;
    node->initial_stake = locked_stake;
    node->initial_balance = initial_total_balance - locked_stake;
    node->balance = initial_total_balance - locked_stake;
    return true;
}

bool bc_update_node(Blockchain *chain, const char *name,
                    uint64_t hash_power, uint64_t locked_stake, uint64_t initial_total_balance) {
    size_t index, i;
    if (chain->block_count != 0 || name == NULL || locked_stake > initial_total_balance) return false;
    index = node_index(chain, name);
    if (index == SIZE_MAX) return false;
    for (i = 0; i < chain->pending_count; ++i) {
        if (strcmp(chain->pending[i].sender, name) == 0 ||
            strcmp(chain->pending[i].recipient, name) == 0) return false;
    }
    chain->nodes[index].hash_power = hash_power;
    chain->nodes[index].stake = locked_stake;
    chain->nodes[index].initial_hash_power = hash_power;
    chain->nodes[index].initial_stake = locked_stake;
    chain->nodes[index].initial_balance = initial_total_balance - locked_stake;
    chain->nodes[index].balance = initial_total_balance - locked_stake;
    chain->nodes[index].rewards = 0;
    return true;
}

bool bc_remove_node(Blockchain *chain, const char *name) {
    size_t index, i;
    if (chain->block_count != 0 || name == NULL) return false;
    index = node_index(chain, name);
    if (index == SIZE_MAX) return false;
    for (i = 0; i < chain->pending_count; ++i) {
        if (strcmp(chain->pending[i].sender, name) == 0 ||
            strcmp(chain->pending[i].recipient, name) == 0) return false;
    }
    if (index + 1 < chain->node_count) {
        memmove(&chain->nodes[index], &chain->nodes[index + 1],
                (chain->node_count - index - 1) * sizeof(*chain->nodes));
    }
    chain->node_count--;
    return true;
}

bool bc_create_genesis(Blockchain *chain) {
    Block *genesis;
    if (chain->block_count != 0 || chain->node_count == 0) return false;
    genesis = &chain->blocks[0];
    memset(genesis, 0, sizeof(*genesis));
    genesis->height = 0;
    genesis->consensus = CONSENSUS_GENESIS;
    memset(genesis->previous_hash, '0', SHA256_HEX_SIZE - 1);
    genesis->previous_hash[SHA256_HEX_SIZE - 1] = '\0';
    snprintf(genesis->producer, sizeof(genesis->producer), "GENESIS");
    if (!hash_block(genesis, genesis->hash)) return false;
    chain->block_count = 1;
    return true;
}

bool bc_add_transaction(Blockchain *chain, const Transaction *transaction) {
    uint64_t balances[BC_MAX_NODES];
    size_t i;
    if (transaction == NULL || chain->pending_count == BC_MAX_PENDING_TRANSACTIONS ||
        strlen(transaction->sender) >= BC_NAME_SIZE || strlen(transaction->recipient) >= BC_NAME_SIZE) return false;
    current_balances(chain, balances);
    for (i = 0; i < chain->pending_count; ++i) {
        if (!apply_transaction(balances, chain, &chain->pending[i])) return false;
    }
    if (!apply_transaction(balances, chain, transaction)) return false;
    chain->pending[chain->pending_count++] = *transaction;
    return true;
}

bool bc_remove_pending_transaction(Blockchain *chain, size_t transaction_index) {
    if (transaction_index >= chain->pending_count) return false;
    if (transaction_index + 1 < chain->pending_count) {
        memmove(&chain->pending[transaction_index], &chain->pending[transaction_index + 1],
                (chain->pending_count - transaction_index - 1) * sizeof(*chain->pending));
    }
    chain->pending_count--;
    return true;
}

bool bc_add_resource_update(Blockchain *chain, const ResourceUpdate *update) {
    size_t index;
    const Node *node;
    if (chain == NULL || update == NULL || chain->block_count == 0 ||
        chain->pending_resource_update_count == BC_MAX_PENDING_RESOURCE_UPDATES ||
        update->node_name[0] == '\0' || strlen(update->node_name) >= BC_NAME_SIZE) return false;
    index = node_index(chain, update->node_name);
    if (index == SIZE_MAX) return false;
    node = &chain->nodes[index];
    if (update->stake > node->stake && node->balance < update->stake - node->stake) return false;
    chain->pending_resource_updates[chain->pending_resource_update_count++] = *update;
    return true;
}

bool bc_remove_pending_resource_update(Blockchain *chain, size_t update_index) {
    if (chain == NULL || update_index >= chain->pending_resource_update_count) return false;
    if (update_index + 1 < chain->pending_resource_update_count) {
        memmove(&chain->pending_resource_updates[update_index],
                &chain->pending_resource_updates[update_index + 1],
                (chain->pending_resource_update_count - update_index - 1) *
                sizeof(*chain->pending_resource_updates));
    }
    chain->pending_resource_update_count--;
    return true;
}

size_t bc_honest_producer_strategy(const Blockchain *chain, Consensus consensus, uint32_t height,
                                   const char previous_hash[SHA256_HEX_SIZE], uint64_t ticket) {
    uint64_t total = 0;
    size_t i;
    (void)height;
    (void)previous_hash;
    for (i = 0; i < chain->node_count; ++i) {
        uint64_t resource = consensus == CONSENSUS_POW ? chain->nodes[i].hash_power : chain->nodes[i].stake;
        if (UINT64_MAX - total < resource) return SIZE_MAX;
        total += resource;
    }
    if (total == 0) return SIZE_MAX;
    ticket %= total;
    for (i = 0; i < chain->node_count; ++i) {
        uint64_t resource = consensus == CONSENSUS_POW ? chain->nodes[i].hash_power : chain->nodes[i].stake;
        if (ticket < resource) return i;
        ticket -= resource;
    }
    return SIZE_MAX;
}

static uint64_t default_ticket(const Blockchain *chain, uint32_t height,
                               const char previous_hash[SHA256_HEX_SIZE]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = fnv1a_mix(hash, (const unsigned char *)&chain->seed, sizeof(chain->seed));
    hash = fnv1a_mix(hash, (const unsigned char *)&height, sizeof(height));
    return fnv1a_mix(hash, (const unsigned char *)previous_hash, strlen(previous_hash));
}

size_t bc_select_producer_ticket(const Blockchain *chain, Consensus consensus, uint32_t height,
                                 const char previous_hash[SHA256_HEX_SIZE], uint64_t ticket) {
    if (chain == NULL || chain->producer_strategy == NULL) return SIZE_MAX;
    return chain->producer_strategy(chain, consensus, height, previous_hash, ticket);
}

void bc_set_producer_strategy(Blockchain *chain, ProducerStrategy strategy) {
    chain->producer_strategy = strategy == NULL ? bc_honest_producer_strategy : strategy;
}

static BlockCandidateStatus prepare_selected_block(const Blockchain *chain,
                                                   Consensus consensus,
                                                   const size_t transaction_indices[],
                                                   size_t transaction_count,
                                                   const size_t resource_update_indices[],
                                                   size_t resource_update_count,
                                                   uint64_t ticket,
                                                   bool allow_empty,
                                                   Block *preview,
                                                   uint64_t balances[BC_MAX_NODES],
                                                   size_t *producer_out) {
    bool selected[BC_MAX_PENDING_TRANSACTIONS] = {false};
    bool selected_updates[BC_MAX_PENDING_RESOURCE_UPDATES] = {false};
    Node updated_nodes[BC_MAX_NODES];
    size_t i;
    size_t j;
    size_t producer;

    if (chain == NULL || preview == NULL || balances == NULL || producer_out == NULL) {
        return BC_CANDIDATE_CHAIN_INVALID;
    }
    if (chain->block_count == 0) return BC_CANDIDATE_CHAIN_NOT_STARTED;
    if (!bc_validate(chain)) return BC_CANDIDATE_CHAIN_INVALID;
    if (consensus != CONSENSUS_POW && consensus != CONSENSUS_POS) return BC_CANDIDATE_NO_PRODUCER;
    if (chain->block_count == BC_MAX_BLOCKS) return BC_CANDIDATE_CAPACITY_REACHED;
    if (transaction_count == 0 && resource_update_count == 0 && !allow_empty) {
        return BC_CANDIDATE_EMPTY_SELECTION;
    }
    if (transaction_count > BC_MAX_TRANSACTIONS_PER_BLOCK) return BC_CANDIDATE_TOO_MANY_TRANSACTIONS;
    if (resource_update_count > BC_MAX_RESOURCE_UPDATES_PER_BLOCK) {
        return BC_CANDIDATE_INVALID_RESOURCE_UPDATE;
    }
    if (transaction_count > 0 && transaction_indices == NULL) return BC_CANDIDATE_INVALID_INDEX;
    if (resource_update_count > 0 && resource_update_indices == NULL) {
        return BC_CANDIDATE_INVALID_RESOURCE_UPDATE;
    }

    memset(preview, 0, sizeof(*preview));
    preview->height = (uint32_t)chain->block_count;
    preview->consensus = consensus;
    memcpy(preview->previous_hash, chain->blocks[chain->block_count - 1].hash, SHA256_HEX_SIZE);
    preview->selection_ticket = ticket;
    producer = bc_select_producer_ticket(chain, consensus, preview->height, preview->previous_hash, ticket);
    if (producer >= chain->node_count) return BC_CANDIDATE_NO_PRODUCER;
    memcpy(preview->producer, chain->nodes[producer].name, BC_NAME_SIZE);
    preview->reward = chain->block_reward;

    for (i = 0; i < transaction_count; ++i) {
        size_t index = transaction_indices[i];
        if (index >= chain->pending_count) return BC_CANDIDATE_INVALID_INDEX;
        if (selected[index]) return BC_CANDIDATE_DUPLICATE_INDEX;
        selected[index] = true;
        preview->transactions[i] = chain->pending[index];
    }
    preview->transaction_count = transaction_count;
    for (i = 0; i < resource_update_count; ++i) {
        size_t index = resource_update_indices[i];
        const ResourceUpdate *update;
        if (index >= chain->pending_resource_update_count) return BC_CANDIDATE_INVALID_RESOURCE_UPDATE;
        if (selected_updates[index]) return BC_CANDIDATE_INVALID_RESOURCE_UPDATE;
        selected_updates[index] = true;
        update = &chain->pending_resource_updates[index];
        if (node_index(chain, update->node_name) == SIZE_MAX) return BC_CANDIDATE_INVALID_RESOURCE_UPDATE;
        for (j = 0; j < i; ++j) {
            if (strcmp(preview->resource_updates[j].node_name, update->node_name) == 0) {
                return BC_CANDIDATE_DUPLICATE_RESOURCE_TARGET;
            }
        }
        preview->resource_updates[i] = *update;
    }
    preview->resource_update_count = resource_update_count;
    current_balances(chain, balances);
    for (i = 0; i < transaction_count; ++i) {
        if (!apply_transaction(balances, chain, &preview->transactions[i])) {
            return BC_CANDIDATE_TRANSACTION_INVALID;
        }
    }
    if (UINT64_MAX - balances[producer] < preview->reward) return BC_CANDIDATE_BALANCE_OVERFLOW;
    balances[producer] += preview->reward;
    memcpy(updated_nodes, chain->nodes, chain->node_count * sizeof(*updated_nodes));
    for (i = 0; i < chain->node_count; ++i) updated_nodes[i].balance = balances[i];
    for (i = 0; i < resource_update_count; ++i) {
        if (!apply_resource_update_to_nodes(updated_nodes, chain->node_count,
                                            &preview->resource_updates[i])) {
            return BC_CANDIDATE_STAKE_INSUFFICIENT;
        }
    }
    *producer_out = producer;
    return BC_CANDIDATE_READY;
}

static void remove_selected_transactions(Blockchain *chain,
                                         const size_t transaction_indices[],
                                         size_t transaction_count) {
    bool selected[BC_MAX_PENDING_TRANSACTIONS] = {false};
    size_t i;
    size_t remaining = 0;

    for (i = 0; i < transaction_count; ++i) selected[transaction_indices[i]] = true;
    for (i = 0; i < chain->pending_count; ++i) {
        if (!selected[i]) chain->pending[remaining++] = chain->pending[i];
    }
    chain->pending_count = remaining;
}

static void remove_selected_resource_updates(Blockchain *chain,
                                             const size_t update_indices[],
                                             size_t update_count) {
    bool selected[BC_MAX_PENDING_RESOURCE_UPDATES] = {false};
    size_t i;
    size_t remaining = 0;

    for (i = 0; i < update_count; ++i) selected[update_indices[i]] = true;
    for (i = 0; i < chain->pending_resource_update_count; ++i) {
        if (!selected[i]) {
            chain->pending_resource_updates[remaining++] = chain->pending_resource_updates[i];
        }
    }
    chain->pending_resource_update_count = remaining;
}

static BlockCandidateStatus mine_selected_block(Blockchain *chain,
                                                Consensus consensus,
                                                const size_t transaction_indices[],
                                                size_t transaction_count,
                                                const size_t resource_update_indices[],
                                                size_t resource_update_count,
                                                uint64_t ticket,
                                                bool allow_empty,
                                                RunMetrics *metrics) {
    Block block;
    uint64_t balances[BC_MAX_NODES];
    size_t producer, i;
    BlockCandidateStatus status;
    double started;

    status = prepare_selected_block(chain, consensus, transaction_indices, transaction_count,
                                    resource_update_indices, resource_update_count, ticket, allow_empty,
                                    &block, balances, &producer);
    if (status != BC_CANDIDATE_READY) return status;
    started = now_seconds();
    if (block.consensus == CONSENSUS_POW) {
        for (block.nonce = 0;; ++block.nonce) {
            if (!hash_block(&block, block.hash)) return BC_CANDIDATE_CHAIN_INVALID;
            if (meets_difficulty(block.hash, chain->difficulty)) break;
            if (block.nonce == UINT64_MAX) return BC_CANDIDATE_CHAIN_INVALID;
        }
    } else if (!hash_block(&block, block.hash)) {
        return BC_CANDIDATE_CHAIN_INVALID;
    }
    if (metrics != NULL) metrics->mining_seconds += now_seconds() - started;
    chain->blocks[chain->block_count] = block;
    for (i = 0; i < chain->node_count; ++i) chain->nodes[i].balance = balances[i];
    chain->nodes[producer].rewards += block.reward;
    remove_selected_transactions(chain, transaction_indices, transaction_count);
    remove_selected_resource_updates(chain, resource_update_indices, resource_update_count);
    for (i = 0; i < block.resource_update_count; ++i) {
        if (!apply_resource_update_to_nodes(chain->nodes, chain->node_count,
                                            &block.resource_updates[i])) return BC_CANDIDATE_CHAIN_INVALID;
    }
    chain->block_count++;
    if (metrics != NULL) {
        metrics->blocks_produced++;
        metrics->accepted_transactions += transaction_count;
    }
    return BC_CANDIDATE_READY;
}

BlockCandidateStatus bc_preview_selected_block(const Blockchain *chain,
                                                Consensus consensus,
                                                const size_t transaction_indices[],
                                                size_t transaction_count,
                                                const size_t resource_update_indices[],
                                                size_t resource_update_count,
                                                Block *preview) {
    return bc_preview_selected_block_ticket(chain, consensus, transaction_indices, transaction_count,
                                            resource_update_indices, resource_update_count,
                                            default_ticket(chain, (uint32_t)chain->block_count,
                                                           chain->blocks[chain->block_count - 1].hash),
                                            preview);
}

BlockCandidateStatus bc_produce_selected_block(Blockchain *chain,
                                                Consensus consensus,
                                                const size_t transaction_indices[],
                                                size_t transaction_count,
                                                const size_t resource_update_indices[],
                                                size_t resource_update_count,
                                                RunMetrics *metrics) {
    return bc_produce_selected_block_ticket(chain, consensus, transaction_indices, transaction_count,
                                            resource_update_indices, resource_update_count,
                                            default_ticket(chain, (uint32_t)chain->block_count,
                                                           chain->blocks[chain->block_count - 1].hash),
                                            metrics);
}

BlockCandidateStatus bc_preview_selected_block_ticket(const Blockchain *chain, Consensus consensus,
                                                       const size_t transaction_indices[], size_t transaction_count,
                                                       const size_t resource_update_indices[], size_t resource_update_count,
                                                       uint64_t ticket, Block *preview) {
    uint64_t balances[BC_MAX_NODES];
    size_t producer;
    return prepare_selected_block(chain, consensus, transaction_indices, transaction_count,
                                  resource_update_indices, resource_update_count, ticket, false,
                                  preview, balances, &producer);
}

BlockCandidateStatus bc_produce_selected_block_ticket(Blockchain *chain, Consensus consensus,
                                                       const size_t transaction_indices[], size_t transaction_count,
                                                       const size_t resource_update_indices[], size_t resource_update_count,
                                                       uint64_t ticket, RunMetrics *metrics) {
    return mine_selected_block(chain, consensus, transaction_indices, transaction_count,
                               resource_update_indices, resource_update_count, ticket, false, metrics);
}

BlockCandidateStatus bc_produce_empty_block_ticket(Blockchain *chain, Consensus consensus,
                                                    uint64_t ticket, RunMetrics *metrics) {
    return mine_selected_block(chain, consensus, NULL, 0, NULL, 0, ticket, true, metrics);
}

bool bc_produce_next_block(Blockchain *chain, RunMetrics *metrics) {
    size_t indices[BC_MAX_TRANSACTIONS_PER_BLOCK];
    size_t count;
    size_t i;

    if (chain == NULL) return false;
    count = chain->pending_count < BC_MAX_TRANSACTIONS_PER_BLOCK ?
        chain->pending_count : BC_MAX_TRANSACTIONS_PER_BLOCK;
    for (i = 0; i < count; ++i) indices[i] = i;
    return mine_selected_block(chain, chain->consensus, indices, count, NULL, 0,
                               default_ticket(chain, (uint32_t)chain->block_count,
                                              chain->blocks[chain->block_count - 1].hash),
                               true, metrics) ==
           BC_CANDIDATE_READY;
}

bool bc_run(Blockchain *chain, size_t blocks_to_produce, RunMetrics *metrics) {
    size_t i;
    if (metrics != NULL) memset(metrics, 0, sizeof(*metrics));
    if (chain->block_count == 0 && !bc_create_genesis(chain)) return false;
    for (i = 0; i < blocks_to_produce; ++i) {
        if (!bc_produce_next_block(chain, metrics)) return false;
    }
    return true;
}

bool bc_validate(const Blockchain *chain) {
    uint64_t balances[BC_MAX_NODES] = {0};
    uint64_t rewards[BC_MAX_NODES] = {0};
    char expected_hash[SHA256_HEX_SIZE];
    Blockchain replay;
    size_t i, j;

    if (chain->block_count == 0 || chain->node_count == 0 || chain->difficulty > BC_MAX_DIFFICULTY) return false;
    replay = *chain;
    for (i = 0; i < chain->node_count; ++i) {
        uint64_t total_balance;
        if (!bc_node_total_balance(&chain->nodes[i], &total_balance) ||
            UINT64_MAX - chain->nodes[i].initial_balance < chain->nodes[i].initial_stake) return false;
        balances[i] = chain->nodes[i].initial_balance;
        replay.nodes[i].balance = chain->nodes[i].initial_balance;
        replay.nodes[i].rewards = 0;
        replay.nodes[i].hash_power = chain->nodes[i].initial_hash_power;
        replay.nodes[i].stake = chain->nodes[i].initial_stake;
    }
    for (i = 0; i < chain->block_count; ++i) {
        const Block *block = &chain->blocks[i];
        if (block->height != i || !hash_block(block, expected_hash) || strcmp(block->hash, expected_hash) != 0) return false;
        if (i == 0) {
            if (strcmp(block->producer, "GENESIS") != 0 || strspn(block->previous_hash, "0") != 64 ||
                block->transaction_count != 0 || block->reward != 0 ||
                block->resource_update_count != 0 || block->consensus != CONSENSUS_GENESIS) return false;
            continue;
        }
        if (strcmp(block->previous_hash, chain->blocks[i - 1].hash) != 0 ||
            block->transaction_count > BC_MAX_TRANSACTIONS_PER_BLOCK || block->reward != chain->block_reward ||
            block->resource_update_count > BC_MAX_RESOURCE_UPDATES_PER_BLOCK ||
            (block->consensus != CONSENSUS_POW && block->consensus != CONSENSUS_POS)) return false;
        if (block->consensus == CONSENSUS_POW && !meets_difficulty(block->hash, chain->difficulty)) return false;
        if (block->consensus == CONSENSUS_POS && block->nonce != 0) return false;
        if (chain->producer_strategy == NULL ||
            replay.producer_strategy(&replay, block->consensus, block->height, block->previous_hash,
                                     block->selection_ticket) >=
            chain->node_count) return false;
        j = replay.producer_strategy(&replay, block->consensus, block->height, block->previous_hash,
                                     block->selection_ticket);
        if (strcmp(block->producer, replay.nodes[j].name) != 0) return false;
        for (j = 0; j < block->transaction_count; ++j) {
            if (!apply_transaction(balances, chain, &block->transactions[j])) return false;
        }
        j = node_index(chain, block->producer);
        if (j == SIZE_MAX || UINT64_MAX - balances[j] < block->reward ||
            UINT64_MAX - rewards[j] < block->reward) return false;
        balances[j] += block->reward;
        rewards[j] += block->reward;
        for (j = 0; j < chain->node_count; ++j) replay.nodes[j].balance = balances[j];
        for (j = 0; j < block->resource_update_count; ++j) {
            size_t k;
            if (block->resource_updates[j].node_name[0] == '\0' ||
                !apply_resource_update_to_nodes(replay.nodes, replay.node_count,
                                                &block->resource_updates[j])) return false;
            for (k = 0; k < j; ++k) {
                if (strcmp(block->resource_updates[k].node_name,
                           block->resource_updates[j].node_name) == 0) return false;
            }
        }
        for (j = 0; j < chain->node_count; ++j) balances[j] = replay.nodes[j].balance;
    }
    for (i = 0; i < chain->node_count; ++i) {
        if (balances[i] != chain->nodes[i].balance || rewards[i] != chain->nodes[i].rewards ||
            replay.nodes[i].hash_power != chain->nodes[i].hash_power ||
            replay.nodes[i].stake != chain->nodes[i].stake) return false;
    }
    return true;
}

const char *bc_consensus_name(Consensus consensus) {
    if (consensus == CONSENSUS_POW) return "PoW";
    if (consensus == CONSENSUS_POS) return "PoS";
    return "GENESIS";
}

const char *bc_candidate_status_message(BlockCandidateStatus status) {
    switch (status) {
        case BC_CANDIDATE_READY:
            return "PRET A MINER : la selection et les soldes sont valides.";
        case BC_CANDIDATE_CHAIN_NOT_STARTED:
            return "Creez d'abord le bloc genesis.";
        case BC_CANDIDATE_CHAIN_INVALID:
            return "La chaine existante est invalide.";
        case BC_CANDIDATE_CAPACITY_REACHED:
            return "La capacite maximale de blocs est atteinte.";
        case BC_CANDIDATE_EMPTY_SELECTION:
            return "Le bloc candidat doit contenir une transaction ou une mise a jour de ressources.";
        case BC_CANDIDATE_TOO_MANY_TRANSACTIONS:
            return "Un bloc candidat ne peut contenir que 8 transactions.";
        case BC_CANDIDATE_INVALID_INDEX:
            return "Un indice ne correspond pas a une transaction en attente.";
        case BC_CANDIDATE_DUPLICATE_INDEX:
            return "Une meme transaction ne peut pas etre selectionnee deux fois.";
        case BC_CANDIDATE_NO_PRODUCER:
            return "Aucun producteur ne peut etre selectionne.";
        case BC_CANDIDATE_TRANSACTION_INVALID:
            return "Une transaction est invalide dans cet ordre (compte, montant ou solde).";
        case BC_CANDIDATE_BALANCE_OVERFLOW:
            return "La recompense provoquerait un depassement de solde.";
        case BC_CANDIDATE_INVALID_RESOURCE_UPDATE:
            return "Une mise a jour de ressources est invalide ou son indice est incorrect.";
        case BC_CANDIDATE_DUPLICATE_RESOURCE_TARGET:
            return "Un compte ne peut recevoir qu'une mise a jour par bloc.";
        case BC_CANDIDATE_STAKE_INSUFFICIENT:
            return "Le solde disponible ne couvre pas le stake a bloquer apres les transactions.";
        default:
            return "Statut de bloc candidat inconnu.";
    }
}

const Node *bc_find_node(const Blockchain *chain, const char *name) {
    size_t index = node_index(chain, name);
    return index == SIZE_MAX ? NULL : &chain->nodes[index];
}

bool bc_node_total_balance(const Node *node, uint64_t *total) {
    if (node == NULL || total == NULL || UINT64_MAX - node->balance < node->stake) return false;
    *total = node->balance + node->stake;
    return true;
}
