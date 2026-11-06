#include "blockchain.h"
#include "accounts_store.h"
#include "attack.h"
#include "session_store.h"
#include "mitigation.h"
#include "evaluation.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "ECHEC %s:%d : %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static Transaction transaction(const char *sender, const char *recipient, uint64_t amount) {
    Transaction value = {{0}, {0}, amount};
    snprintf(value.sender, sizeof(value.sender), "%s", sender);
    snprintf(value.recipient, sizeof(value.recipient), "%s", recipient);
    return value;
}

static ResourceUpdate resource_update(const char *node_name, uint64_t hash_power, uint64_t stake) {
    ResourceUpdate value = {{0}, hash_power, stake};
    snprintf(value.node_name, sizeof(value.node_name), "%s", node_name);
    return value;
}

static bool build_chain(Blockchain *chain, Consensus consensus) {
    Transaction first = transaction("Alice", "Bob", 15);
    Transaction second = transaction("Bob", "Charlie", 5);
    bc_init(chain, consensus, 1, 42, 10);
    return bc_add_node(chain, "Alice", 50, 50, 100) &&
           bc_add_node(chain, "Bob", 30, 30, 100) &&
           bc_add_node(chain, "Charlie", 20, 20, 100) &&
           bc_add_transaction(chain, &first) && bc_add_transaction(chain, &second) &&
           bc_run(chain, 3, NULL);
}

static bool test_sha256(void) {
    char hash[SHA256_HEX_SIZE];
    sha256_hex((const uint8_t *)"abc", 3, hash);
    CHECK(strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    return true;
}

static bool test_honest_chains(void) {
    Blockchain pow_chain;
    Blockchain pos_chain;
    CHECK(build_chain(&pow_chain, CONSENSUS_POW));
    CHECK(build_chain(&pos_chain, CONSENSUS_POS));
    CHECK(pow_chain.block_count == 4 && pos_chain.block_count == 4);
    CHECK(bc_validate(&pow_chain));
    CHECK(bc_validate(&pos_chain));
    CHECK(pos_chain.blocks[1].nonce == 0);
    CHECK(bc_find_node(&pow_chain, "Bob") != NULL);
    return true;
}

static bool test_transaction_rejections(void) {
    Blockchain chain;
    Transaction valid = transaction("Alice", "Bob", 50);
    Transaction unknown = transaction("Mallory", "Bob", 1);
    Transaction zero = transaction("Alice", "Bob", 0);
    Transaction overspend = transaction("Alice", "Bob", 51);

    bc_init(&chain, CONSENSUS_POW, 0, 1, 1);
    CHECK(bc_add_node(&chain, "Alice", 1, 0, 50));
    CHECK(bc_add_node(&chain, "Bob", 1, 0, 0));
    CHECK(!bc_add_transaction(&chain, &unknown));
    CHECK(!bc_add_transaction(&chain, &zero));
    CHECK(!bc_add_transaction(&chain, &overspend));
    CHECK(bc_add_transaction(&chain, &valid));
    CHECK(!bc_add_transaction(&chain, &valid));
    return true;
}

static bool test_tamper_detection(void) {
    Blockchain chain;
    CHECK(build_chain(&chain, CONSENSUS_POW));
    chain.blocks[1].transactions[0].amount++;
    CHECK(!bc_validate(&chain));

    CHECK(build_chain(&chain, CONSENSUS_POW));
    chain.blocks[1].previous_hash[0] = 'f';
    CHECK(!bc_validate(&chain));

    CHECK(build_chain(&chain, CONSENSUS_POW));
    chain.blocks[1].nonce++;
    CHECK(!bc_validate(&chain));

    CHECK(build_chain(&chain, CONSENSUS_POS));
    chain.blocks[1].producer[0] = 'Z';
    CHECK(!bc_validate(&chain));
    return true;
}

static bool test_management_operations(void) {
    Blockchain chain;
    Transaction pending = transaction("Alice", "Bob", 10);

    bc_init(&chain, CONSENSUS_POW, 0, 1, 1);
    CHECK(bc_add_node(&chain, "Alice", 50, 50, 100));
    CHECK(bc_add_node(&chain, "Bob", 50, 50, 100));
    CHECK(bc_add_transaction(&chain, &pending));
    CHECK(!bc_update_node(&chain, "Alice", 60, 60, 120));
    CHECK(!bc_remove_node(&chain, "Alice"));
    CHECK(bc_remove_pending_transaction(&chain, 0));
    CHECK(bc_update_node(&chain, "Alice", 60, 60, 120));
    CHECK(bc_find_node(&chain, "Alice") != NULL);
    CHECK(bc_find_node(&chain, "Alicia") == NULL);
    CHECK(bc_remove_node(&chain, "Bob"));
    CHECK(bc_create_genesis(&chain));
    CHECK(!bc_update_node(&chain, "Alice", 50, 50, 100));
    CHECK(!bc_remove_node(&chain, "Alice"));
    return true;
}

static bool test_tamper_blocks_mining(void) {
    Blockchain chain;
    CHECK(build_chain(&chain, CONSENSUS_POW));
    snprintf(chain.blocks[1].transactions[0].recipient, BC_NAME_SIZE, "Mallory");
    CHECK(!bc_validate(&chain));
    CHECK(!bc_produce_next_block(&chain, NULL));
    return true;
}

static bool test_transaction_validation_progress(void) {
    Blockchain chain;
    RunMetrics metrics = {0};
    Transaction value = transaction("Alice", "Bob", 1);
    size_t i;

    bc_init(&chain, CONSENSUS_POW, 0, 1, 1);
    CHECK(bc_add_node(&chain, "Alice", 1, 1, 100));
    CHECK(bc_add_node(&chain, "Bob", 1, 1, 1));
    CHECK(bc_create_genesis(&chain));
    for (i = 0; i < 9; ++i) CHECK(bc_add_transaction(&chain, &value));
    CHECK(chain.pending_count == 9);
    CHECK(bc_produce_next_block(&chain, &metrics));
    CHECK(chain.blocks[1].transaction_count == BC_MAX_TRANSACTIONS_PER_BLOCK);
    CHECK(chain.pending_count == 1);
    CHECK(metrics.accepted_transactions == BC_MAX_TRANSACTIONS_PER_BLOCK);
    CHECK(bc_produce_next_block(&chain, &metrics));
    CHECK(chain.blocks[2].transaction_count == 1);
    CHECK(chain.pending_count == 0);
    CHECK(metrics.accepted_transactions == 9);
    CHECK(bc_validate(&chain));
    return true;
}

static bool test_manual_candidate_selection(void) {
    Blockchain chain;
    RunMetrics metrics = {0};
    Block preview;
    Transaction first = transaction("Alice", "Bob", 10);
    Transaction second = transaction("Bob", "Charlie", 5);
    Transaction third = transaction("Alice", "Charlie", 7);
    const size_t selected[] = {2, 0};

    bc_init(&chain, CONSENSUS_POW, 0, 7, 1);
    CHECK(bc_add_node(&chain, "Alice", 50, 50, 100));
    CHECK(bc_add_node(&chain, "Bob", 30, 30, 30));
    CHECK(bc_add_node(&chain, "Charlie", 20, 20, 20));
    CHECK(bc_create_genesis(&chain));
    CHECK(bc_add_transaction(&chain, &first));
    CHECK(bc_add_transaction(&chain, &second));
    CHECK(bc_add_transaction(&chain, &third));
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POW, selected, 2, NULL, 0, &preview) == BC_CANDIDATE_READY);
    CHECK(preview.transaction_count == 2);
    CHECK(strcmp(preview.transactions[0].recipient, "Charlie") == 0);
    CHECK(strcmp(preview.transactions[1].recipient, "Bob") == 0);
    CHECK(chain.pending_count == 3);
    CHECK(bc_produce_selected_block(&chain, CONSENSUS_POW, selected, 2, NULL, 0, &metrics) == BC_CANDIDATE_READY);
    CHECK(chain.blocks[1].transaction_count == 2);
    CHECK(chain.pending_count == 1);
    CHECK(strcmp(chain.pending[0].sender, "Bob") == 0);
    CHECK(metrics.accepted_transactions == 2);
    CHECK(bc_validate(&chain));
    return true;
}

static bool test_invalid_candidate_order(void) {
    Blockchain chain;
    Block preview;
    Transaction incoming = transaction("Alice", "Bob", 10);
    Transaction outgoing = transaction("Bob", "Charlie", 10);
    const size_t invalid_order[] = {1};
    const size_t valid_order[] = {0, 1};

    bc_init(&chain, CONSENSUS_POW, 0, 1, 1);
    CHECK(bc_add_node(&chain, "Alice", 1, 1, 100));
    CHECK(bc_add_node(&chain, "Bob", 1, 1, 1));
    CHECK(bc_add_node(&chain, "Charlie", 1, 1, 1));
    CHECK(bc_create_genesis(&chain));
    CHECK(bc_add_transaction(&chain, &incoming));
    CHECK(bc_add_transaction(&chain, &outgoing));
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POW, invalid_order, 1, NULL, 0, &preview) == BC_CANDIDATE_TRANSACTION_INVALID);
    CHECK(chain.block_count == 1 && chain.pending_count == 2);
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POW, valid_order, 2, NULL, 0, &preview) == BC_CANDIDATE_READY);
    CHECK(chain.pending_count == 2);
    return true;
}

static bool test_mixed_consensus_chain(void) {
    Blockchain chain;
    RunMetrics metrics = {0};
    Transaction first = transaction("Alice", "Bob", 10);
    Transaction second = transaction("Bob", "Alice", 5);
    const size_t selection[] = {0};

    bc_init(&chain, CONSENSUS_POW, 1, 42, 2);
    CHECK(bc_add_node(&chain, "Alice", 60, 70, 100));
    CHECK(bc_add_node(&chain, "Bob", 40, 30, 100));
    CHECK(bc_create_genesis(&chain));
    CHECK(chain.blocks[0].consensus == CONSENSUS_GENESIS);
    CHECK(bc_add_transaction(&chain, &first));
    CHECK(bc_produce_selected_block(&chain, CONSENSUS_POW, selection, 1, NULL, 0, &metrics) == BC_CANDIDATE_READY);
    CHECK(chain.blocks[1].consensus == CONSENSUS_POW);
    CHECK(chain.blocks[1].hash[0] == '0');
    CHECK(bc_add_transaction(&chain, &second));
    CHECK(bc_produce_selected_block(&chain, CONSENSUS_POS, selection, 1, NULL, 0, &metrics) == BC_CANDIDATE_READY);
    CHECK(chain.blocks[2].consensus == CONSENSUS_POS);
    CHECK(chain.blocks[2].nonce == 0);
    CHECK(bc_validate(&chain));
    chain.blocks[1].consensus = CONSENSUS_POS;
    CHECK(!bc_validate(&chain));
    return true;
}

static bool test_consensus_resource_requirement(void) {
    Blockchain chain;
    Block preview;
    Transaction payment = transaction("Alice", "Bob", 1);
    const size_t selection[] = {0};

    bc_init(&chain, CONSENSUS_POW, 0, 1, 1);
    CHECK(bc_add_node(&chain, "Alice", 0, 10, 11));
    CHECK(bc_add_node(&chain, "Bob", 0, 0, 0));
    CHECK(bc_create_genesis(&chain));
    CHECK(bc_add_transaction(&chain, &payment));
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POW, selection, 1, NULL, 0, &preview) == BC_CANDIDATE_NO_PRODUCER);
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POS, selection, 1, NULL, 0, &preview) == BC_CANDIDATE_READY);
    return true;
}

static bool test_resource_updates(void) {
    Blockchain chain;
    RunMetrics metrics = {0};
    Block preview;
    ResourceUpdate alice_update = resource_update("Alice", 0, 90);
    ResourceUpdate bob_update = resource_update("Bob", 100, 10);
    ResourceUpdate unknown_update = resource_update("Unknown", 1, 1);
    const size_t update_selection[] = {0};
    const size_t duplicate_selection[] = {0, 1};

    bc_init(&chain, CONSENSUS_POW, 0, 19, 3);
    CHECK(bc_add_node(&chain, "Alice", 60, 40, 100));
    CHECK(bc_add_node(&chain, "Bob", 40, 60, 100));
    CHECK(bc_create_genesis(&chain));
    CHECK(!bc_add_resource_update(&chain, &unknown_update));
    CHECK(bc_add_resource_update(&chain, &alice_update));
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POW, NULL, 0, update_selection, 1,
                                    &preview) == BC_CANDIDATE_READY);
    CHECK(preview.producer[0] != '\0');
    CHECK(bc_produce_selected_block(&chain, CONSENSUS_POW, NULL, 0, update_selection, 1,
                                    &metrics) == BC_CANDIDATE_READY);
    CHECK(chain.blocks[1].transaction_count == 0 && chain.blocks[1].resource_update_count == 1);
    CHECK(chain.nodes[0].hash_power == 0 && chain.nodes[0].stake == 90);
    CHECK(chain.pending_resource_update_count == 0);
    CHECK(bc_add_resource_update(&chain, &bob_update));
    CHECK(bc_produce_selected_block(&chain, CONSENSUS_POW, NULL, 0, update_selection, 1,
                                    &metrics) == BC_CANDIDATE_READY);
    CHECK(strcmp(chain.blocks[2].producer, "Bob") == 0);
    CHECK(bc_validate(&chain));

    alice_update = resource_update("Alice", 25, 0);
    CHECK(bc_add_resource_update(&chain, &alice_update));
    CHECK(bc_produce_selected_block(&chain, CONSENSUS_POS, NULL, 0, update_selection, 1,
                                    &metrics) == BC_CANDIDATE_READY);
    CHECK(chain.blocks[3].consensus == CONSENSUS_POS);
    CHECK(chain.nodes[0].stake == 0);
    CHECK(bc_validate(&chain));

    CHECK(bc_add_resource_update(&chain, &alice_update));
    CHECK(bc_add_resource_update(&chain, &bob_update));
    CHECK(bc_preview_selected_block(&chain, CONSENSUS_POS, NULL, 0, duplicate_selection, 2,
                                    &preview) == BC_CANDIDATE_READY);
    CHECK(bc_remove_pending_resource_update(&chain, 0));
    CHECK(bc_add_resource_update(&chain, &alice_update));
    {
        const size_t same_target[] = {1, 2};
        CHECK(bc_add_resource_update(&chain, &alice_update));
        CHECK(bc_preview_selected_block(&chain, CONSENSUS_POS, NULL, 0, same_target, 2,
                                        &preview) == BC_CANDIDATE_DUPLICATE_RESOURCE_TARGET);
    }
    chain.blocks[1].resource_updates[0].stake++;
    CHECK(!bc_validate(&chain));
    return true;
}

static bool test_staked_balances(void) {
    Blockchain chain;
    Blockchain insufficient;
    Block preview;
    ResourceUpdate increase = resource_update("Alice", 1, 50);
    ResourceUpdate decrease = resource_update("Alice", 1, 10);
    Transaction payment = transaction("Alice", "Bob", 40);
    const size_t update_selection[] = {0};
    const size_t transaction_selection[] = {0};
    uint64_t total = 0;

    bc_init(&chain, CONSENSUS_POS, 0, 3, 10);
    CHECK(!bc_add_node(&chain, "Invalide", 1, 101, 100));
    CHECK(bc_add_node(&chain, "Alice", 1, 20, 100));
    CHECK(bc_add_node(&chain, "Bob", 1, 80, 100));
    CHECK(chain.nodes[0].balance == 80 && chain.nodes[0].stake == 20);
    CHECK(bc_node_total_balance(&chain.nodes[0], &total) && total == 100);
    CHECK(!bc_add_transaction(&chain, &(Transaction){.sender = "Alice", .recipient = "Bob", .amount = 81}));
    CHECK(bc_create_genesis(&chain));
    CHECK(bc_add_resource_update(&chain, &increase));
    CHECK(bc_produce_selected_block_ticket(&chain, CONSENSUS_POS, NULL, 0,
                                           update_selection, 1, 0, NULL) == BC_CANDIDATE_READY);
    CHECK(strcmp(chain.blocks[1].producer, "Alice") == 0);
    CHECK(chain.nodes[0].stake == 50 && chain.nodes[0].balance == 60);
    CHECK(bc_produce_empty_block_ticket(&chain, CONSENSUS_POS, 30, NULL) == BC_CANDIDATE_READY);
    CHECK(strcmp(chain.blocks[2].producer, "Alice") == 0);
    CHECK(bc_add_resource_update(&chain, &decrease));
    CHECK(bc_produce_selected_block_ticket(&chain, CONSENSUS_POS, NULL, 0,
                                           update_selection, 1, 0, NULL) == BC_CANDIDATE_READY);
    CHECK(chain.nodes[0].stake == 10 && chain.nodes[0].balance == 120);
    CHECK(bc_validate(&chain));

    bc_init(&insufficient, CONSENSUS_POW, 0, 1, 0);
    CHECK(bc_add_node(&insufficient, "Alice", 1, 10, 50));
    CHECK(bc_add_node(&insufficient, "Bob", 1, 0, 0));
    CHECK(bc_create_genesis(&insufficient));
    CHECK(bc_add_transaction(&insufficient, &payment));
    increase = resource_update("Alice", 1, 20);
    CHECK(bc_add_resource_update(&insufficient, &increase));
    CHECK(bc_preview_selected_block(&insufficient, CONSENSUS_POW, transaction_selection, 1,
                                    update_selection, 1, &preview) == BC_CANDIDATE_STAKE_INSUFFICIENT);
    return true;
}

static void remove_store_files(const char *path) {
    char temporary[600];
    remove(path);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    remove(temporary);
}

static bool test_account_store(void) {
    const char *path = "tests/accounts_store_test.csv";
    const char *invalid_path = "tests/accounts_store_invalid.csv";
    Blockchain chain;
    Blockchain restored;
    AccountStoreReport report;
    Transaction payment = transaction("Alice", "Bob", 10);
    const Node *alice;
    const Node *bob;
    FILE *file;
    char header[128];

    remove_store_files(path);
    remove_store_files(invalid_path);
    bc_init(&chain, CONSENSUS_POW, 0, 1, 2);
    CHECK(bc_add_node(&chain, "Alice", 60, 50, 100));
    CHECK(bc_add_node(&chain, "Bob", 40, 50, 75));
    CHECK(bc_create_genesis(&chain));
    CHECK(bc_add_transaction(&chain, &payment));
    CHECK(bc_produce_next_block(&chain, NULL));
    {
        ResourceUpdate update = resource_update("Alice", 75, 55);
        const size_t selection[] = {0};
        CHECK(bc_add_resource_update(&chain, &update));
        CHECK(bc_produce_selected_block(&chain, CONSENSUS_POS, NULL, 0, selection, 1,
                                        NULL) == BC_CANDIDATE_READY);
    }
    CHECK(account_store_save(&chain, path));
    file = fopen(path, "r");
    CHECK(file != NULL && fgets(header, sizeof(header), file) != NULL);
    CHECK(strcmp(header, "name;hash_power;locked_stake;available_balance\n") == 0);
    CHECK(fclose(file) == 0);

    bc_init(&restored, CONSENSUS_POW, 0, 1, 2);
    CHECK(account_store_load(&restored, path, &report) == ACCOUNT_STORE_OK);
    CHECK(report.loaded == 2 && report.skipped == 0);
    alice = bc_find_node(&restored, "Alice");
    bob = bc_find_node(&restored, "Bob");
    CHECK(alice != NULL && bob != NULL);
    CHECK(alice->balance == chain.nodes[0].balance);
    CHECK(alice->initial_balance == chain.nodes[0].balance);
    CHECK(alice->hash_power == 75 && alice->stake == 55);
    CHECK(alice->initial_hash_power == 75 && alice->initial_stake == 55);
    CHECK(bob->balance == chain.nodes[1].balance);
    CHECK(bc_update_node(&restored, "Alice", 70, 55, alice->balance + alice->stake));
    CHECK(bc_remove_node(&restored, "Bob"));
    CHECK(account_store_save(&restored, path));

    bc_init(&chain, CONSENSUS_POW, 0, 1, 2);
    CHECK(account_store_load(&chain, path, &report) == ACCOUNT_STORE_OK);
    CHECK(report.loaded == 1);
    CHECK(bc_find_node(&chain, "Alice") != NULL);
    CHECK(bc_find_node(&chain, "Bob") == NULL);

    file = fopen(invalid_path, "w");
    CHECK(file != NULL);
    fputs("name;hash_power;stake;balance\n", file);
    fputs("Valid;10;20;30\n", file);
    fputs("Invalide;pas_un_nombre;20;30\n", file);
    CHECK(fclose(file) == 0);
    bc_init(&chain, CONSENSUS_POW, 0, 1, 2);
    CHECK(account_store_load(&chain, invalid_path, &report) == ACCOUNT_STORE_OK);
    CHECK(report.loaded == 1 && report.skipped == 1);
    CHECK(bc_find_node(&chain, "Valid") != NULL);
    CHECK(bc_find_node(&chain, "Valid")->stake == 20 && bc_find_node(&chain, "Valid")->balance == 30);
    remove_store_files(path);
    remove_store_files(invalid_path);
    return true;
}

static bool test_attack_scenarios(void) {
    Blockchain chain;
    AttackScenario scenario;
    AttackResult result;

    bc_init(&chain, CONSENSUS_POW, 0, 7, 2);
    CHECK(bc_add_node(&chain, "Alice", 70, 70, 100));
    CHECK(bc_add_node(&chain, "Bob", 30, 30, 100));
    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_51;
    scenario.consensus = CONSENSUS_POW;
    scenario.seed = 7;
    scenario.rounds = 30;
    scenario.confirmations = 1;
    scenario.amount = 10;
    scenario.attacker_count = 1;
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Alice");
    snprintf(scenario.spender, BC_NAME_SIZE, "Alice");
    snprintf(scenario.merchant, BC_NAME_SIZE, "Bob");
    snprintf(scenario.private_recipient, BC_NAME_SIZE, "Alice");
    CHECK(attack_run(&chain, &scenario, &result));
    CHECK(result.success && result.reference_valid && result.attacked_valid);
    CHECK(result.merchant_loss == 10 && result.reorganization_depth > 0);

    scenario.consensus = CONSENSUS_POS;
    CHECK(attack_run(&chain, &scenario, &result));
    CHECK(result.success && result.reference_valid && result.attacked_valid);
    scenario.consensus = CONSENSUS_POW;

    scenario.attackers[0][0] = '\0';
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Bob");
    CHECK(!attack_run(&chain, &scenario, &result));

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_SELFISH_MINING;
    scenario.consensus = CONSENSUS_POW;
    scenario.seed = 9;
    scenario.rounds = 12;
    scenario.gamma = 100;
    scenario.attacker_count = 1;
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Alice");
    CHECK(attack_run(&chain, &scenario, &result));
    CHECK(result.reference_valid && result.attacked_valid);

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_BLOCK_WITHHOLDING;
    scenario.consensus = CONSENSUS_POW;
    scenario.seed = 4;
    scenario.rounds = 10;
    scenario.pool_member_count = 2;
    scenario.attacker_count = 1;
    snprintf(scenario.pool_members[0], BC_NAME_SIZE, "Alice");
    snprintf(scenario.pool_members[1], BC_NAME_SIZE, "Bob");
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Alice");
    CHECK(attack_run(&chain, &scenario, &result));
    CHECK(result.pool_attacked_rewards < result.pool_reference_rewards);
    CHECK(result.reference_valid && result.attacked_valid);

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_SYBIL;
    scenario.seed = 2;
    scenario.rounds = 3;
    scenario.victim_degree = 3;
    scenario.sybil_count = 3;
    snprintf(scenario.victim, BC_NAME_SIZE, "Bob");
    CHECK(attack_run(&chain, &scenario, &result));
    CHECK(result.success && result.victim_seen_height < result.victim_reference_height);
    CHECK(chain.block_count == 0);
    return true;
}

static bool test_mitigation(void) {
    Blockchain chain;
    AttackScenario scenario;
    MetricsExperiment experiment;
    DetectionReport report;
    MitigationConfig config;
    MitigationResult result;

    bc_init(&chain, CONSENSUS_POW, 0, 7, 10);
    CHECK(bc_add_node(&chain, "Alice", 70, 70, 100));
    CHECK(bc_add_node(&chain, "Bob", 30, 30, 100));
    mitigation_config_default(&config);

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_51; scenario.consensus = CONSENSUS_POW; scenario.seed = 7;
    scenario.rounds = 30; scenario.confirmations = 1; scenario.amount = 10; scenario.attacker_count = 1;
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Alice");
    snprintf(scenario.spender, BC_NAME_SIZE, "Alice");
    snprintf(scenario.merchant, BC_NAME_SIZE, "Bob");
    snprintf(scenario.private_recipient, BC_NAME_SIZE, "Alice");
    CHECK(metrics_run_experiment(&chain, &scenario, &experiment));
    detection_analyze(&experiment, &report);
    CHECK(mitigation_apply(&experiment, &report, &config, &result));
    CHECK(result.applied && result.adoption_blocked && result.required_confirmations == 6);
    CHECK(experiment.protected_available && result.protected_valid);
    CHECK(result.protected_chain.block_count == experiment.attack.reference_chain.block_count);
    config.protect_51 = false;
    CHECK(mitigation_apply(&experiment, &report, &config, &result));
    CHECK(!result.applied && experiment.protected_available && !result.adoption_blocked);
    mitigation_config_default(&config);

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_SELFISH_MINING; scenario.consensus = CONSENSUS_POW; scenario.seed = 9;
    scenario.rounds = 12; scenario.gamma = 100; scenario.attacker_count = 1;
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Alice");
    CHECK(metrics_run_experiment(&chain, &scenario, &experiment));
    detection_analyze(&experiment, &report);
    CHECK(mitigation_apply(&experiment, &report, &config, &result));
    CHECK(result.applied && result.adoption_blocked && result.protected_valid);

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_BLOCK_WITHHOLDING; scenario.consensus = CONSENSUS_POW; scenario.seed = 4;
    scenario.rounds = 10; scenario.attacker_count = 1; scenario.pool_member_count = 2;
    snprintf(scenario.attackers[0], BC_NAME_SIZE, "Alice");
    snprintf(scenario.pool_members[0], BC_NAME_SIZE, "Alice");
    snprintf(scenario.pool_members[1], BC_NAME_SIZE, "Bob");
    CHECK(metrics_run_experiment(&chain, &scenario, &experiment));
    detection_analyze(&experiment, &report);
    CHECK(mitigation_apply(&experiment, &report, &config, &result));
    CHECK(result.applied && result.prevented_attacker_reward > 0 && result.protected_valid);

    memset(&scenario, 0, sizeof(scenario));
    scenario.type = ATTACK_SYBIL; scenario.seed = 2; scenario.rounds = 3;
    scenario.victim_degree = 3; scenario.sybil_count = 3;
    snprintf(scenario.victim, BC_NAME_SIZE, "Bob");
    CHECK(metrics_run_experiment(&chain, &scenario, &experiment));
    detection_analyze(&experiment, &report);
    CHECK(mitigation_apply(&experiment, &report, &config, &result));
    CHECK(result.applied && result.adoption_blocked && result.allowed_sybil_neighbors == 1);
    CHECK(result.protected_chain.block_count == experiment.attack.reference_chain.block_count);
    return true;
}

static bool test_evaluation_campaign(void) {
    EvaluationConfig config;
    EvaluationReport report;
    FILE *file;
    evaluation_config_default(&config);
    config.seed_base = 7;
    config.seed_count = 1;
    config.node_count_count = 1;
    config.delay_count = 1;
    config.node_counts[0] = 3;
    config.delays[0] = 1;
    config.output_directory = "tests\\evaluation_test";
    CHECK(evaluation_run_campaign(&config, &report));
    CHECK(report.run_count == 4);
    CHECK(report.output_path[0] != '\0');
    {
        char path[EVALUATION_PATH_SIZE + 32];
        snprintf(path, sizeof(path), "%s\\runs.csv", report.output_path);
        file = fopen(path, "r");
        CHECK(file != NULL); CHECK(fclose(file) == 0);
        snprintf(path, sizeof(path), "%s\\charts\\economic_loss.svg", report.output_path);
        file = fopen(path, "r");
        CHECK(file != NULL); CHECK(fclose(file) == 0);
    }
    return true;
}

static bool test_session_store(void) {
    const char *active = "tests/session_test.active";
    SessionState saved;
    SessionState restored;
    Transaction payment = transaction("Alice", "Bob", 5);
    remove(active);
    remove("tests/session_77_2.csv");
    memset(&saved, 0, sizeof(saved));
    bc_init(&saved.chain, CONSENSUS_POW, 0, 77, 2);
    CHECK(bc_add_node(&saved.chain, "Alice", 60, 60, 100));
    CHECK(bc_add_node(&saved.chain, "Bob", 40, 40, 40));
    CHECK(bc_create_genesis(&saved.chain));
    CHECK(bc_add_transaction(&saved.chain, &payment));
    CHECK(bc_produce_next_block(&saved.chain, &saved.metrics));
    CHECK(bc_add_transaction(&saved.chain, &payment));
    saved.draft.active = true;
    saved.draft.consensus = CONSENSUS_POS;
    saved.draft.transaction_count = 1;
    saved.draft.transaction_indices[0] = 0;
    mitigation_config_default(&saved.mitigation);
    saved.mitigation.protect_sybil = false;
    CHECK(session_store_save(&saved, active));
    CHECK(session_store_load(&restored, active) == SESSION_STORE_OK);
    CHECK(restored.chain.block_count == saved.chain.block_count);
    CHECK(restored.chain.pending_count == 1 && restored.draft.active);
    CHECK(restored.draft.transaction_indices[0] == 0);
    CHECK(!restored.mitigation.protect_sybil && restored.mitigation.protect_51);
    CHECK(bc_validate(&restored.chain));
    CHECK(session_store_reset(active));
    CHECK(session_store_load(&restored, active) == SESSION_STORE_NOT_FOUND);
    return true;
}

int main(void) {
    CHECK(test_sha256());
    CHECK(test_honest_chains());
    CHECK(test_transaction_rejections());
    CHECK(test_tamper_detection());
    CHECK(test_management_operations());
    CHECK(test_tamper_blocks_mining());
    CHECK(test_transaction_validation_progress());
    CHECK(test_manual_candidate_selection());
    CHECK(test_invalid_candidate_order());
    CHECK(test_mixed_consensus_chain());
    CHECK(test_consensus_resource_requirement());
    CHECK(test_resource_updates());
    CHECK(test_staked_balances());
    CHECK(test_account_store());
    CHECK(test_attack_scenarios());
    CHECK(test_mitigation());
    CHECK(test_evaluation_campaign());
    CHECK(test_session_store());
    puts("Tous les tests du moteur ont reussi.");
    return 0;
}
