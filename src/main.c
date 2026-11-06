#include "blockchain.h"
#include "attack.h"
#include "metrics.h"
#include "detection.h"
#include "mitigation.h"
#include "evaluation.h"
#include "analysis.h"
#include "menu.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Consensus consensus;
    size_t blocks;
    unsigned difficulty;
    uint64_t seed;
    uint64_t reward;
    const char *transactions[BC_MAX_PENDING_TRANSACTIONS];
    size_t transaction_count;
    bool custom_nodes;
    bool attack_mode;
    bool adopt_attacked_chain;
    bool export_metrics;
    const char *experiments_dir;
    AttackScenario attack;
    MitigationConfig mitigation;
    bool campaign;
    EvaluationConfig evaluation;
    const char *analysis_directory;
} CliOptions;

static void print_usage(const char *program) {
    printf("Usage : %s [options]\n\n", program);
    puts("Options :");
    puts("  --consensus pow|pos              Consensus (PoW par defaut)");
    puts("  --blocks N                       Nombre de blocs apres genesis (defaut : 5)");
    puts("  --difficulty N                   Difficulte PoW, de 0 a 6 (defaut : 3)");
    puts("  --seed N                         Graine de selection (defaut : 42)");
    puts("  --reward N                       Recompense par bloc (defaut : 10)");
    puts("  --node nom:hash_power:stake_bloque:solde_total  Ajoute un participant (repetable)");
    puts("  --tx emetteur:destinataire:montant Ajoute une transaction (repetable)");
    puts("  --attack 51|selfish|withholding|sybil  Execute un scenario d'attaque");
    puts("  --attacker nom, --merchant nom, --private-recipient nom, --spender nom");
    puts("  --amount N, --confirmations N, --rounds N, --gamma 0..100");
    puts("  --pool-member nom, --sybil-victim nom, --sybils N, --victim-degree N");
    puts("  --adopt-attacked-chain yes|no");
    puts("  --mitigation on|off, --mitigation-51 on|off");
    puts("  --mitigation-selfish on|off, --mitigation-withholding on|off, --mitigation-sybil on|off");
    puts("  --campaign [--campaign-seeds N] [--campaign-nodes 3,5,8]");
    puts("  --campaign-profile quick|complete");
    puts("  --campaign-delays 1,2,3 [--campaign-attacks 51,selfish,withholding,sybil]");
    puts("  --evaluation-dir chemin");
    puts("  --analyze chemin_campagne");
    puts("  --no-export-metrics, --experiments-dir chemin");
    puts("  --help                           Affiche cette aide");
    puts("\nSans --node, Alice, Bob et Charlie sont crees automatiquement.");
}

static bool parse_u64(const char *text, uint64_t *result) {
    char *end;
    unsigned long long value;
    if (text == NULL || *text == '\0' || *text == '-') return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0') return false;
    *result = (uint64_t)value;
    return true;
}

static bool parse_size(const char *text, size_t *result) {
    uint64_t value;
    if (!parse_u64(text, &value) || value > SIZE_MAX) return false;
    *result = (size_t)value;
    return true;
}

static bool parse_on_off(const char *text, bool *result) {
    if (strcmp(text, "on") == 0) { *result = true; return true; }
    if (strcmp(text, "off") == 0) { *result = false; return true; }
    return false;
}

static bool parse_size_list(const char *text, size_t values[], size_t *count) {
    char copy[96], *part, *next; size_t n = 0, value;
    if (strlen(text) >= sizeof(copy)) return false;
    snprintf(copy, sizeof(copy), "%s", text); part = copy;
    while (part != NULL) { next = strchr(part, ','); if (next) *next++ = '\0';
        if (n == EVALUATION_MAX_VALUES || !parse_size(part, &value) || value == 0) return false;
        values[n++] = value; part = next;
    }
    if (!n) return false;
    *count = n;
    return true;
}

static bool parse_attack_list(const char *text, bool attacks[4]) {
    char copy[128], *part, *next; AttackType type; size_t n = 0;
    if (strlen(text) >= sizeof(copy)) return false;
    memset(attacks, 0, 4 * sizeof(*attacks)); snprintf(copy, sizeof(copy), "%s", text); part = copy;
    while (part != NULL) { next = strchr(part, ','); if (next) *next++ = '\0';
        if (!attack_parse_type(part, &type)) return false;
        attacks[type] = true;
        n++;
        part = next;
    }
    return n > 0;
}

static bool parse_node(Blockchain *chain, const char *argument) {
    char copy[256];
    char *name, *hash_power, *stake, *balance, *extra;
    uint64_t parsed_hash_power, parsed_stake, parsed_balance;

    if (strlen(argument) >= sizeof(copy)) return false;
    snprintf(copy, sizeof(copy), "%s", argument);
    name = strtok(copy, ":");
    hash_power = strtok(NULL, ":");
    stake = strtok(NULL, ":");
    balance = strtok(NULL, ":");
    extra = strtok(NULL, ":");
    if (name == NULL || hash_power == NULL || stake == NULL || balance == NULL || extra != NULL ||
        !parse_u64(hash_power, &parsed_hash_power) || !parse_u64(stake, &parsed_stake) ||
        !parse_u64(balance, &parsed_balance)) return false;
    return bc_add_node(chain, name, parsed_hash_power, parsed_stake, parsed_balance);
}

static bool parse_transaction(const char *argument, Transaction *transaction) {
    char copy[256];
    char *sender, *recipient, *amount, *extra;

    if (strlen(argument) >= sizeof(copy)) return false;
    snprintf(copy, sizeof(copy), "%s", argument);
    sender = strtok(copy, ":");
    recipient = strtok(NULL, ":");
    amount = strtok(NULL, ":");
    extra = strtok(NULL, ":");
    if (sender == NULL || recipient == NULL || amount == NULL || extra != NULL ||
        strlen(sender) >= BC_NAME_SIZE || strlen(recipient) >= BC_NAME_SIZE ||
        !parse_u64(amount, &transaction->amount)) return false;
    snprintf(transaction->sender, sizeof(transaction->sender), "%s", sender);
    snprintf(transaction->recipient, sizeof(transaction->recipient), "%s", recipient);
    return true;
}

static bool add_default_nodes(Blockchain *chain) {
    return bc_add_node(chain, "Alice", 50, 50, 100) &&
           bc_add_node(chain, "Bob", 30, 30, 100) &&
           bc_add_node(chain, "Charlie", 20, 20, 100);
}

static bool has_consensus_resource(const Blockchain *chain) {
    size_t i;
    for (i = 0; i < chain->node_count; ++i) {
        if (chain->consensus == CONSENSUS_POW && chain->nodes[i].hash_power > 0) return true;
        if (chain->consensus == CONSENSUS_POS && chain->nodes[i].stake > 0) return true;
    }
    return false;
}

static void print_chain(const Blockchain *chain) {
    size_t i, j;
    puts("\n========== CHAINE PRODUITE ==========");
    for (i = 0; i < chain->block_count; ++i) {
        const Block *block = &chain->blocks[i];
        printf("\nBloc %" PRIu32 " | consensus : %s | producteur : %s",
               block->height, bc_consensus_name(block->consensus), block->producer);
        if (block->height == 0) puts(" (genesis)");
        else putchar('\n');
        printf("  previous_hash : %s\n", block->previous_hash);
        printf("  hash          : %s\n", block->hash);
        if (block->height != 0) printf("  ticket        : %" PRIu64 "\n", block->selection_ticket);
        if (block->consensus == CONSENSUS_POW && block->height != 0) {
            printf("  nonce         : %" PRIu64 "\n", block->nonce);
        }
        if (block->height != 0) printf("  recompense    : %" PRIu64 "\n", block->reward);
        printf("  Transactions validees dans ce bloc : %zu\n", block->transaction_count);
        for (j = 0; j < block->transaction_count; ++j) {
            const Transaction *transaction = &block->transactions[j];
            printf("    VALIDEE | %s -> %s : %" PRIu64 " unites\n",
                   transaction->sender, transaction->recipient, transaction->amount);
        }
        printf("  Mises a jour de ressources appliquees : %zu\n", block->resource_update_count);
        for (j = 0; j < block->resource_update_count; ++j) {
            const ResourceUpdate *update = &block->resource_updates[j];
            printf("    APPLIQUEE | %s : hash power -> %" PRIu64 ", stake bloque -> %" PRIu64 "\n",
                   update->node_name, update->hash_power, update->stake);
        }
    }
}

static void print_summary(const Blockchain *chain, const RunMetrics *metrics) {
    size_t i;
    puts("\n========== SYNTHESE ==========");
    printf("Consensus             : %s\n", bc_consensus_name(chain->consensus));
    printf("Blocs (avec genesis)  : %zu\n", chain->block_count);
    printf("Transactions validees / incluses dans des blocs : %zu\n", metrics->accepted_transactions);
    printf("Transactions toujours en attente              : %zu\n", chain->pending_count);
    printf("Temps de production   : %.6f s\n", metrics->mining_seconds);
    printf("Forks                 : %zu (reseau honnete synchrone)\n", metrics->forks);
    printf("Validation finale     : %s\n", bc_validate(chain) ? "VALIDE" : "INVALIDE");
    puts("\nNoeud                     Disponible  Stake bloque        Total   Recompenses");
    puts("-------------------------------------------------------------------------------");
    for (i = 0; i < chain->node_count; ++i) {
        uint64_t total = 0;
        (void)bc_node_total_balance(&chain->nodes[i], &total);
        printf("%-20s %10" PRIu64 " %13" PRIu64 " %12" PRIu64 " %14" PRIu64 "\n", chain->nodes[i].name,
               chain->nodes[i].balance, chain->nodes[i].stake, total, chain->nodes[i].rewards);
    }
}

int main(int argc, char *argv[]) {
    CliOptions options = {0};
    Blockchain chain;
    RunMetrics metrics;
    int i;
    const char *argument = NULL;

    options.consensus = CONSENSUS_POW;
    options.blocks = 5;
    options.difficulty = 3;
    options.seed = 42;
    options.reward = 10;
    options.attack.gamma = 50;
    options.attack.rounds = 20;
    options.export_metrics = true;
    mitigation_config_default(&options.mitigation);
    evaluation_config_default(&options.evaluation);
    if (argc == 1) return run_interactive_menu();

    bc_init(&chain, options.consensus, options.difficulty, options.seed, options.reward);
    for (i = 1; i < argc; ++i) {
        argument = argv[i];
        if (strcmp(argument, "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argument, "--no-export-metrics") == 0) {
            options.export_metrics = false;
            continue;
        }
        if (strcmp(argument, "--campaign") == 0) {
            options.campaign = true;
            continue;
        }
        if (++i >= argc) {
            fprintf(stderr, "Option sans valeur : %s\n", argument);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (strcmp(argument, "--consensus") == 0) {
            if (strcmp(argv[i], "pow") == 0) options.consensus = CONSENSUS_POW;
            else if (strcmp(argv[i], "pos") == 0) options.consensus = CONSENSUS_POS;
            else goto invalid_argument;
        } else if (strcmp(argument, "--blocks") == 0) {
            if (!parse_size(argv[i], &options.blocks) || options.blocks >= BC_MAX_BLOCKS) goto invalid_argument;
        } else if (strcmp(argument, "--difficulty") == 0) {
            uint64_t difficulty;
            if (!parse_u64(argv[i], &difficulty) || difficulty > BC_MAX_DIFFICULTY) goto invalid_argument;
            options.difficulty = (unsigned)difficulty;
        } else if (strcmp(argument, "--seed") == 0) {
            if (!parse_u64(argv[i], &options.seed)) goto invalid_argument;
        } else if (strcmp(argument, "--reward") == 0) {
            if (!parse_u64(argv[i], &options.reward)) goto invalid_argument;
        } else if (strcmp(argument, "--node") == 0) {
            if (!options.custom_nodes) {
                chain.node_count = 0;
                options.custom_nodes = true;
            }
            if (!parse_node(&chain, argv[i])) goto invalid_argument;
        } else if (strcmp(argument, "--tx") == 0) {
            if (options.transaction_count == BC_MAX_PENDING_TRANSACTIONS) goto invalid_argument;
            options.transactions[options.transaction_count++] = argv[i];
        } else if (strcmp(argument, "--attack") == 0) {
            if (!attack_parse_type(argv[i], &options.attack.type)) goto invalid_argument;
            options.attack_mode = true;
        } else if (strcmp(argument, "--attacker") == 0) {
            if (options.attack.attacker_count == BC_MAX_NODES || strlen(argv[i]) >= BC_NAME_SIZE) goto invalid_argument;
            snprintf(options.attack.attackers[options.attack.attacker_count++], BC_NAME_SIZE, "%s", argv[i]);
        } else if (strcmp(argument, "--pool-member") == 0) {
            if (options.attack.pool_member_count == BC_MAX_NODES || strlen(argv[i]) >= BC_NAME_SIZE) goto invalid_argument;
            snprintf(options.attack.pool_members[options.attack.pool_member_count++], BC_NAME_SIZE, "%s", argv[i]);
        } else if (strcmp(argument, "--merchant") == 0) {
            if (strlen(argv[i]) >= BC_NAME_SIZE) goto invalid_argument;
            snprintf(options.attack.merchant, BC_NAME_SIZE, "%s", argv[i]);
        } else if (strcmp(argument, "--private-recipient") == 0) {
            if (strlen(argv[i]) >= BC_NAME_SIZE) goto invalid_argument;
            snprintf(options.attack.private_recipient, BC_NAME_SIZE, "%s", argv[i]);
        } else if (strcmp(argument, "--spender") == 0) {
            if (strlen(argv[i]) >= BC_NAME_SIZE) goto invalid_argument;
            snprintf(options.attack.spender, BC_NAME_SIZE, "%s", argv[i]);
        } else if (strcmp(argument, "--amount") == 0) {
            if (!parse_u64(argv[i], &options.attack.amount) || options.attack.amount == 0) goto invalid_argument;
        } else if (strcmp(argument, "--confirmations") == 0) {
            if (!parse_size(argv[i], &options.attack.confirmations)) goto invalid_argument;
        } else if (strcmp(argument, "--rounds") == 0) {
            if (!parse_size(argv[i], &options.attack.rounds) || options.attack.rounds == 0) goto invalid_argument;
        } else if (strcmp(argument, "--gamma") == 0) {
            uint64_t gamma;
            if (!parse_u64(argv[i], &gamma) || gamma > 100) goto invalid_argument;
            options.attack.gamma = (unsigned)gamma;
        } else if (strcmp(argument, "--sybil-victim") == 0) {
            if (strlen(argv[i]) >= BC_NAME_SIZE) goto invalid_argument;
            snprintf(options.attack.victim, BC_NAME_SIZE, "%s", argv[i]);
        } else if (strcmp(argument, "--sybils") == 0) {
            if (!parse_size(argv[i], &options.attack.sybil_count) || options.attack.sybil_count == 0) goto invalid_argument;
        } else if (strcmp(argument, "--victim-degree") == 0) {
            if (!parse_size(argv[i], &options.attack.victim_degree) || options.attack.victim_degree == 0) goto invalid_argument;
        } else if (strcmp(argument, "--adopt-attacked-chain") == 0) {
            if (strcmp(argv[i], "yes") == 0) options.adopt_attacked_chain = true;
            else if (strcmp(argv[i], "no") == 0) options.adopt_attacked_chain = false;
            else goto invalid_argument;
        } else if (strcmp(argument, "--mitigation") == 0) {
            if (!parse_on_off(argv[i], &options.mitigation.enabled)) goto invalid_argument;
        } else if (strcmp(argument, "--mitigation-51") == 0) {
            if (!parse_on_off(argv[i], &options.mitigation.protect_51)) goto invalid_argument;
        } else if (strcmp(argument, "--mitigation-selfish") == 0) {
            if (!parse_on_off(argv[i], &options.mitigation.protect_selfish)) goto invalid_argument;
        } else if (strcmp(argument, "--mitigation-withholding") == 0) {
            if (!parse_on_off(argv[i], &options.mitigation.protect_withholding)) goto invalid_argument;
        } else if (strcmp(argument, "--mitigation-sybil") == 0) {
            if (!parse_on_off(argv[i], &options.mitigation.protect_sybil)) goto invalid_argument;
        } else if (strcmp(argument, "--campaign-seeds") == 0) {
            if (!parse_size(argv[i], &options.evaluation.seed_count) || options.evaluation.seed_count == 0 || options.evaluation.seed_count > EVALUATION_MAX_SEEDS) goto invalid_argument;
        } else if (strcmp(argument, "--campaign-profile") == 0) {
            if (strcmp(argv[i], "quick") == 0) evaluation_config_quick(&options.evaluation);
            else if (strcmp(argv[i], "complete") == 0) evaluation_config_default(&options.evaluation);
            else goto invalid_argument;
        } else if (strcmp(argument, "--campaign-nodes") == 0) {
            if (!parse_size_list(argv[i], options.evaluation.node_counts, &options.evaluation.node_count_count)) goto invalid_argument;
        } else if (strcmp(argument, "--campaign-delays") == 0) {
            if (!parse_size_list(argv[i], options.evaluation.delays, &options.evaluation.delay_count)) goto invalid_argument;
        } else if (strcmp(argument, "--campaign-attacks") == 0) {
            if (!parse_attack_list(argv[i], options.evaluation.attacks)) goto invalid_argument;
        } else if (strcmp(argument, "--evaluation-dir") == 0) {
            options.evaluation.output_directory = argv[i];
        } else if (strcmp(argument, "--analyze") == 0) {
            options.analysis_directory = argv[i];
        } else if (strcmp(argument, "--experiments-dir") == 0) {
            options.experiments_dir = argv[i];
        } else {
            goto invalid_argument;
        }
    }
    chain.consensus = options.consensus;
    chain.difficulty = options.difficulty;
    chain.seed = options.seed;
    chain.block_reward = options.reward;
    options.attack.consensus = options.consensus;
    options.attack.seed = options.seed;
    if (options.analysis_directory != NULL) {
        AnalysisReport report;
        if (!analysis_run(options.analysis_directory, &report)) { fputs("Analyse CSV impossible.\n", stderr); return EXIT_FAILURE; }
        analysis_print_report(&report);
        return EXIT_SUCCESS;
    }
    if (options.campaign) {
        EvaluationReport report;
        options.evaluation.seed_base = options.seed;
        if (!evaluation_run_campaign(&options.evaluation, &report)) {
            fputs("Echec de la campagne d'evaluation.\n", stderr);
            return EXIT_FAILURE;
        }
        evaluation_print_report(&report);
        return EXIT_SUCCESS;
    }
    if (!options.custom_nodes && !add_default_nodes(&chain)) {
        fputs("Impossible de creer les participants par defaut.\n", stderr);
        return EXIT_FAILURE;
    }
    if (!has_consensus_resource(&chain) &&
        !(options.attack_mode && options.attack.type == ATTACK_SYBIL)) {
        fprintf(stderr, "Ressource %s totale nulle.\n", options.consensus == CONSENSUS_POW ? "PoW" : "PoS");
        return EXIT_FAILURE;
    }
    if (options.transaction_count == 0 && !options.custom_nodes && !options.attack_mode) {
        options.transactions[options.transaction_count++] = "Alice:Bob:15";
        options.transactions[options.transaction_count++] = "Bob:Charlie:5";
    }
    for (i = 0; (size_t)i < options.transaction_count; ++i) {
        Transaction transaction;
        if (!parse_transaction(options.transactions[i], &transaction) || !bc_add_transaction(&chain, &transaction)) {
            fprintf(stderr, "Transaction invalide : %s\n", options.transactions[i]);
            return EXIT_FAILURE;
        }
    }
    if (options.attack_mode) {
        MetricsExperiment experiment;
        AttackResult *result;
        DetectionReport report;
        MitigationResult mitigation;
        if ((options.attack.type == ATTACK_SELFISH_MINING ||
             options.attack.type == ATTACK_BLOCK_WITHHOLDING) && options.consensus != CONSENSUS_POW) {
            fputs("Selfish mining et block withholding exigent PoW.\n", stderr);
            return EXIT_FAILURE;
        }
        if (!metrics_run_experiment(&chain, &options.attack, &experiment)) {
            fputs("Scenario d'attaque invalide.\n", stderr);
            return EXIT_FAILURE;
        }
        result = &experiment.attack;
        attack_print_result(result);
        detection_analyze(&experiment, &report);
        detection_print_report(&report);
        if (!mitigation_apply(&experiment, &report, &options.mitigation, &mitigation)) {
            fputs("Avertissement : impossibilite de construire la simulation protegee.\n", stderr);
        }
        mitigation_print_report(&mitigation);
        metrics_print_experiment(&experiment);
        if (options.export_metrics) {
            char directory[METRICS_PATH_SIZE];
            if ((options.experiments_dir != NULL && strlen(options.experiments_dir) < sizeof(directory) &&
                 (snprintf(directory, sizeof(directory), "%s", options.experiments_dir), 1)) ||
                metrics_default_directory(directory)) {
                if (metrics_export_csv(&experiment, directory)) {
                    detection_export_csv(&report, &experiment);
                    mitigation_export_csv(&mitigation, &experiment);
                    printf("Metriques CSV exportees : %s\n", experiment.export_path);
                } else fputs("Avertissement : export CSV impossible.\n", stderr);
            } else fputs("Avertissement : repertoire CSV invalide.\n", stderr);
        }
        if (options.adopt_attacked_chain && !mitigation.adoption_blocked && options.attack.type != ATTACK_SYBIL &&
            result->success && result->attacked_valid) chain = result->attacked_chain;
        else if (options.adopt_attacked_chain && mitigation.adoption_blocked)
            fputs("Adoption bloquee par la mitigation.\n", stderr);
        return result->attacked_valid ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    printf("Simulation blockchain honnete | consensus %s | seed %" PRIu64 "\n",
           bc_consensus_name(chain.consensus), chain.seed);
    if (!bc_run(&chain, options.blocks, &metrics)) {
        fputs("Echec pendant la production de la blockchain.\n", stderr);
        return EXIT_FAILURE;
    }
    print_chain(&chain);
    print_summary(&chain, &metrics);
    return bc_validate(&chain) ? EXIT_SUCCESS : EXIT_FAILURE;

invalid_argument:
    fprintf(stderr, "Argument invalide pour %s : %s\n", argument, argv[i]);
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
