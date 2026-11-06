#include "menu.h"

#include "accounts_store.h"
#include "attack.h"
#include "blockchain.h"
#include "session_store.h"
#include "metrics.h"
#include "detection.h"
#include "mitigation.h"
#include "evaluation.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool active;
    Consensus consensus;
    size_t transaction_indices[BC_MAX_TRANSACTIONS_PER_BLOCK];
    size_t transaction_count;
    size_t resource_update_indices[BC_MAX_RESOURCE_UPDATES_PER_BLOCK];
    size_t resource_update_count;
    BlockCandidateStatus status;
    Block preview;
} BlockDraft;

typedef struct {
    Blockchain chain;
    RunMetrics metrics;
    BlockDraft draft;
    char account_store_path[ACCOUNT_STORE_PATH_SIZE];
    char session_store_path[SESSION_STORE_PATH_SIZE];
    MitigationConfig mitigation;
} MenuState;

static int read_line(char *text, size_t capacity) {
    size_t length;
    int character;
    if (fgets(text, (int)capacity, stdin) == NULL) return 0;
    length = strcspn(text, "\r\n");
    if (text[length] == '\0' && length == capacity - 1) {
        while ((character = getchar()) != '\n' && character != EOF) { }
        return 0;
    }
    text[length] = '\0';
    return 1;
}

static int parse_u64(const char *text, uint64_t *result) {
    char *end;
    unsigned long long value;
    if (text[0] == '\0' || text[0] == '-') return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0') return 0;
    *result = (uint64_t)value;
    return 1;
}

static int prompt_u64(const char *message, uint64_t minimum, uint64_t maximum, uint64_t *result) {
    char line[80];
    uint64_t value;
    for (;;) {
        printf("%s", message);
        if (!read_line(line, sizeof(line))) return 0;
        if (parse_u64(line, &value) && value >= minimum && value <= maximum) {
            *result = value;
            return 1;
        }
        printf("Entree invalide. Valeur attendue : %" PRIu64 " a %" PRIu64 ".\n", minimum, maximum);
    }
}

static int prompt_text(const char *message, char *text, size_t capacity) {
    for (;;) {
        printf("%s", message);
        if (!read_line(text, capacity)) return 0;
        if (text[0] != '\0') return 1;
        puts("Le champ ne doit pas etre vide ou depasser la longueur autorisee.");
    }
}

static int prompt_yes_no(const char *message, int *answer) {
    char line[8];
    for (;;) {
        printf("%s", message);
        if (!read_line(line, sizeof(line))) return 0;
        if ((line[0] == 'o' || line[0] == 'O') && line[1] == '\0') { *answer = 1; return 1; }
        if ((line[0] == 'n' || line[0] == 'N') && line[1] == '\0') { *answer = 0; return 1; }
        puts("Repondez par o ou n.");
    }
}

static int has_consensus_resource(const Blockchain *chain, Consensus consensus) {
    size_t i;
    for (i = 0; i < chain->node_count; ++i) {
        if (consensus == CONSENSUS_POW && chain->nodes[i].hash_power > 0) return 1;
        if (consensus == CONSENSUS_POS && chain->nodes[i].stake > 0) return 1;
    }
    return 0;
}

static int load_persisted_accounts(MenuState *state, int announce) {
    AccountStoreReport report;
    AccountStoreStatus status;
    Blockchain loaded_chain;

    bc_init(&loaded_chain, CONSENSUS_POW, 3, 42, 10);
    status = account_store_load(&loaded_chain, state->account_store_path, &report);
    if (status == ACCOUNT_STORE_IO_ERROR) {
        puts("Erreur : impossible de lire le registre de comptes. Aucun changement n'a ete applique.");
        return 0;
    }
    state->chain = loaded_chain;
    memset(&state->metrics, 0, sizeof(state->metrics));
    memset(&state->draft, 0, sizeof(state->draft));
    mitigation_config_default(&state->mitigation);
    if (announce && status == ACCOUNT_STORE_NOT_FOUND) {
        puts("Aucun registre de comptes trouve : la liste commence vide.");
    } else if (announce) {
        printf("%zu compte(s) recharge(s) depuis le registre.", report.loaded);
        if (report.skipped > 0) printf(" %zu ligne(s) invalide(s) ignoree(s).", report.skipped);
        putchar('\n');
    }
    return 1;
}

static int load_persisted_session(MenuState *state, int announce) {
    SessionState snapshot;
    SessionStoreStatus status = session_store_load(&snapshot, state->session_store_path);
    if (status == SESSION_STORE_OK) {
        state->chain = snapshot.chain;
        state->metrics = snapshot.metrics;
        state->mitigation = snapshot.mitigation;
        memset(&state->draft, 0, sizeof(state->draft));
        state->draft.active = snapshot.draft.active;
        state->draft.consensus = snapshot.draft.consensus;
        state->draft.transaction_count = snapshot.draft.transaction_count;
        state->draft.resource_update_count = snapshot.draft.resource_update_count;
        memcpy(state->draft.transaction_indices, snapshot.draft.transaction_indices,
               sizeof(state->draft.transaction_indices));
        memcpy(state->draft.resource_update_indices, snapshot.draft.resource_update_indices,
               sizeof(state->draft.resource_update_indices));
        if (state->draft.active && state->chain.block_count > 0) {
            state->draft.status = bc_preview_selected_block(&state->chain, state->draft.consensus,
                state->draft.transaction_indices, state->draft.transaction_count,
                state->draft.resource_update_indices, state->draft.resource_update_count, &state->draft.preview);
        }
        if (announce) printf("Session blockchain restauree : %zu bloc(s).\n", state->chain.block_count);
        return 1;
    }
    if (status == SESSION_STORE_INVALID && announce) {
        puts("Avertissement : session blockchain invalide ignoree ; registre de comptes charge.");
    } else if (status == SESSION_STORE_IO_ERROR) {
        puts("Erreur : impossible de lire la session blockchain.");
        return 0;
    }
    return load_persisted_accounts(state, announce);
}

static int save_persisted_accounts(const MenuState *state) {
    SessionState snapshot;
    if (!account_store_save(&state->chain, state->account_store_path)) {
        puts("ERREUR : le registre de comptes n'a pas pu etre sauvegarde.");
        return 0;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.chain = state->chain;
    snapshot.metrics = state->metrics;
    snapshot.mitigation = state->mitigation;
    snapshot.draft.active = state->draft.active;
    snapshot.draft.consensus = state->draft.consensus;
    snapshot.draft.transaction_count = state->draft.transaction_count;
    snapshot.draft.resource_update_count = state->draft.resource_update_count;
    memcpy(snapshot.draft.transaction_indices, state->draft.transaction_indices,
           sizeof(snapshot.draft.transaction_indices));
    memcpy(snapshot.draft.resource_update_indices, state->draft.resource_update_indices,
           sizeof(snapshot.draft.resource_update_indices));
    if (session_store_save(&snapshot, state->session_store_path)) return 1;
    puts("ERREUR : la session est en memoire mais n'a pas pu etre sauvegardee.");
    return 0;
}

static void print_nodes(const Blockchain *chain) {
    size_t i;
    puts("\n--- Participants ---");
    if (chain->node_count == 0) {
        puts("Aucun participant configure.");
        return;
    }
    puts("Nom                     Hash power  Stake bloque    Disponible        Total");
    puts("----------------------------------------------------------------------------");
    for (i = 0; i < chain->node_count; ++i) {
        const Node *node = &chain->nodes[i];
        uint64_t total = 0;
        (void)bc_node_total_balance(node, &total);
        printf("%-20s %10" PRIu64 " %13" PRIu64 " %13" PRIu64 " %12" PRIu64 "\n", node->name,
               node->hash_power, node->stake, node->balance, total);
    }
}

static void print_pending(const Blockchain *chain) {
    size_t i;
    puts("\n--- Transactions en attente ---");
    if (chain->pending_count == 0) {
        puts("Aucune transaction en attente.");
        return;
    }
    puts("Index  Statut       Emetteur                     Destinataire                 Unites");
    puts("--------------------------------------------------------------------------------------");
    for (i = 0; i < chain->pending_count; ++i) {
        const Transaction *transaction = &chain->pending[i];
        printf("%-6zu %-12s %-28s %-28s %" PRIu64 "\n", i, "EN_ATTENTE",
               transaction->sender, transaction->recipient, transaction->amount);
    }
}

static void print_pending_resource_updates(const Blockchain *chain) {
    size_t i;
    puts("\n--- Mises a jour de ressources en attente ---");
    if (chain->pending_resource_update_count == 0) {
        puts("Aucune mise a jour de ressources en attente.");
        return;
    }
    puts("Index  Statut       Compte                       Hash power        Stake bloque");
    puts("--------------------------------------------------------------------------------");
    for (i = 0; i < chain->pending_resource_update_count; ++i) {
        const ResourceUpdate *update = &chain->pending_resource_updates[i];
        const Node *node = bc_find_node(chain, update->node_name);
        printf("%-6zu %-12s %-28s %" PRIu64 " -> %" PRIu64 "    %" PRIu64 " -> %" PRIu64,
               i, "EN_ATTENTE", update->node_name,
               node == NULL ? 0 : node->hash_power, update->hash_power,
               node == NULL ? 0 : node->stake, update->stake);
        if (node != NULL && update->stake > node->stake) {
            printf(" | a bloquer : %" PRIu64 "\n", update->stake - node->stake);
        } else if (node != NULL && update->stake < node->stake) {
            printf(" | a debloquer : %" PRIu64 "\n", node->stake - update->stake);
        } else puts(" | solde disponible inchange");
    }
}

static void print_chain(const Blockchain *chain) {
    size_t i, j;
    puts("\n========== CHAINE ==========");
    if (chain->block_count == 0) {
        puts("Le bloc genesis n'a pas encore ete cree.");
        return;
    }
    for (i = 0; i < chain->block_count; ++i) {
        const Block *block = &chain->blocks[i];
        printf("\nBloc %" PRIu32 " | consensus : %s | producteur : %s",
               block->height, bc_consensus_name(block->consensus), block->producer);
        if (block->height == 0) puts(" (genesis)"); else putchar('\n');
        printf("  previous_hash : %s\n  hash          : %s\n", block->previous_hash, block->hash);
        if (block->height != 0) printf("  ticket        : %" PRIu64 "\n", block->selection_ticket);
        if (block->consensus == CONSENSUS_POW && block->height != 0) {
            printf("  nonce         : %" PRIu64 "\n", block->nonce);
        }
        if (block->height != 0) printf("  recompense    : %" PRIu64 "\n", block->reward);
        printf("  Transactions validees dans ce bloc : %zu\n", block->transaction_count);
        for (j = 0; j < block->transaction_count; ++j) {
            printf("    VALIDEE | %s -> %s : %" PRIu64 " unites\n",
                   block->transactions[j].sender, block->transactions[j].recipient,
                   block->transactions[j].amount);
        }
        printf("  Mises a jour de ressources appliquees : %zu\n", block->resource_update_count);
        for (j = 0; j < block->resource_update_count; ++j) {
            printf("    APPLIQUEE | %s : hash power -> %" PRIu64 ", stake bloque -> %" PRIu64 "\n",
                   block->resource_updates[j].node_name, block->resource_updates[j].hash_power,
                   block->resource_updates[j].stake);
        }
    }
}

static void print_summary(const MenuState *state) {
    const Blockchain *chain = &state->chain;
    size_t i;
    size_t pow_blocks = 0;
    size_t pos_blocks = 0;
    for (i = 1; i < chain->block_count; ++i) {
        if (chain->blocks[i].consensus == CONSENSUS_POW) pow_blocks++;
        if (chain->blocks[i].consensus == CONSENSUS_POS) pos_blocks++;
    }
    puts("\n========== ETAT ET METRIQUES ==========");
    printf("Blocs PoW             : %zu\n", pow_blocks);
    printf("Blocs PoS             : %zu\n", pos_blocks);
    printf("Difficulte PoW        : %u\n", chain->difficulty);
    printf("Seed                  : %" PRIu64 "\n", chain->seed);
    printf("Recompense par bloc   : %" PRIu64 "\n", chain->block_reward);
    printf("Blocs (avec genesis)  : %zu\n", chain->block_count);
    printf("Transactions validees / incluses dans des blocs : %zu\n", state->metrics.accepted_transactions);
    printf("Transactions toujours en attente              : %zu\n", chain->pending_count);
    printf("Mises a jour de ressources en attente         : %zu\n",
           chain->pending_resource_update_count);
    printf("Temps de production   : %.6f s\n", state->metrics.mining_seconds);
    printf("Forks                 : %zu\n", state->metrics.forks);
    printf("Integrite             : %s\n", chain->block_count == 0 ? "NON DEMARREE" :
           (bc_validate(chain) ? "VALIDE" : "INVALIDE"));
    puts("\nNoeud                     Disponible  Stake bloque        Total   Recompenses");
    puts("-------------------------------------------------------------------------------");
    for (i = 0; i < chain->node_count; ++i) {
        uint64_t total = 0;
        (void)bc_node_total_balance(&chain->nodes[i], &total);
        printf("%-20s %10" PRIu64 " %13" PRIu64 " %12" PRIu64 " %14" PRIu64 "\n", chain->nodes[i].name,
               chain->nodes[i].balance, chain->nodes[i].stake, total, chain->nodes[i].rewards);
    }
}

static void configure_parameters(MenuState *state) {
    Blockchain *chain = &state->chain;
    uint64_t choice, value;
    if (chain->block_count != 0) {
        puts("Configuration verrouillee apres le genesis. Reinitialisez la chaine pour la modifier.");
        return;
    }
    for (;;) {
        puts("\n--- Parametres ---");
        printf("1 - Difficulte PoW : %u\n", chain->difficulty);
        printf("2 - Seed : %" PRIu64 "\n", chain->seed);
        printf("3 - Recompense : %" PRIu64 "\n", chain->block_reward);
        puts("0 - Retour");
        if (!prompt_u64("Votre choix : ", 0, 3, &choice) || choice == 0) return;
        if (choice == 1) {
            if (!prompt_u64("Difficulte PoW (0 a 6) : ", 0, BC_MAX_DIFFICULTY, &value)) return;
            chain->difficulty = (unsigned)value;
        } else if (choice == 2) {
            if (!prompt_u64("Seed : ", 0, UINT64_MAX, &chain->seed)) return;
        } else {
            if (!prompt_u64("Recompense par bloc : ", 0, UINT64_MAX, &chain->block_reward)) return;
        }
        save_persisted_accounts(state);
    }
}

static void manage_nodes(MenuState *state) {
    Blockchain *chain = &state->chain;
    uint64_t choice, hash_power, stake, balance;
    char name[BC_NAME_SIZE];
    if (chain->block_count != 0) {
        print_nodes(chain);
        puts("Participants verrouilles apres le genesis. Utilisez les mises a jour de ressources pour changer hash power ou stake.");
        return;
    }
    for (;;) {
        print_nodes(chain);
        puts("1 - Ajouter  2 - Modifier  3 - Supprimer  0 - Retour");
        if (!prompt_u64("Votre choix : ", 0, 3, &choice) || choice == 0) return;
        if (choice == 1) {
            if (!prompt_text("Nom : ", name, sizeof(name)) ||
                !prompt_u64("Hash power : ", 0, UINT64_MAX, &hash_power) ||
                !prompt_u64("Stake a bloquer : ", 0, UINT64_MAX, &stake) ||
                !prompt_u64("Solde total initial : ", 0, UINT64_MAX, &balance)) return;
            if (bc_add_node(chain, name, hash_power, stake, balance)) {
                if (save_persisted_accounts(state)) puts("Participant ajoute et sauvegarde.");
            } else {
                puts("Ajout impossible : nom, capacite ou stake bloque superieur au solde total.");
            }
        } else if (choice == 2) {
            if (!prompt_text("Participant a modifier : ", name, sizeof(name)) ||
                !prompt_u64("Nouveau hash power : ", 0, UINT64_MAX, &hash_power) ||
                !prompt_u64("Nouveau stake a bloquer : ", 0, UINT64_MAX, &stake) ||
                !prompt_u64("Nouveau solde total initial : ", 0, UINT64_MAX, &balance)) return;
            if (bc_update_node(chain, name, hash_power, stake, balance)) {
                if (save_persisted_accounts(state)) puts("Participant modifie et sauvegarde.");
            } else {
                puts("Modification impossible : nom, stake bloque, blockchain ou transaction en attente invalide.");
            }
        } else {
            if (!prompt_text("Nom a supprimer : ", name, sizeof(name))) return;
            if (bc_remove_node(chain, name)) {
                if (save_persisted_accounts(state)) puts("Participant supprime du registre.");
            } else {
                puts("Suppression impossible : nom inconnu ou transaction en attente associee.");
            }
        }
    }
}

static void manage_transactions(MenuState *state) {
    Blockchain *chain = &state->chain;
    uint64_t choice, amount, index;
    Transaction transaction;
    if (state->draft.active) {
        puts("La file est verrouillee par un bloc candidat. Confirmez-le ou abandonnez-le d'abord.");
        return;
    }
    for (;;) {
        print_pending(chain);
        puts("1 - Ajouter  2 - Supprimer  0 - Retour");
        if (!prompt_u64("Votre choix : ", 0, 2, &choice) || choice == 0) return;
        if (choice == 1) {
            if (!prompt_text("Emetteur : ", transaction.sender, sizeof(transaction.sender)) ||
                !prompt_text("Destinataire : ", transaction.recipient, sizeof(transaction.recipient)) ||
                !prompt_u64("Montant : ", 1, UINT64_MAX, &amount)) return;
            transaction.amount = amount;
            if (bc_add_transaction(chain, &transaction)) {
                save_persisted_accounts(state);
                puts("Transaction ajoutee a la file.");
            } else puts("Transaction refusee : compte, montant ou solde invalide.");
        } else {
            if (chain->pending_count == 0) {
                puts("Aucune transaction a supprimer.");
            } else if (!prompt_u64("Indice de la transaction : ", 0, chain->pending_count - 1, &index)) {
                return;
            } else {
                bc_remove_pending_transaction(chain, (size_t)index);
                save_persisted_accounts(state);
                puts("Transaction retiree.");
            }
        }
    }
}

static void manage_resource_updates(MenuState *state) {
    Blockchain *chain = &state->chain;
    ResourceUpdate update;
    uint64_t choice;
    uint64_t index;

    if (state->draft.active) {
        puts("La file est verrouillee par un bloc candidat. Confirmez-le ou abandonnez-le d'abord.");
        return;
    }
    if (chain->block_count == 0) {
        puts("Les ressources se modifient directement dans les participants avant le genesis.");
        return;
    }
    for (;;) {
        print_pending_resource_updates(chain);
        puts("1 - Demander une mise a jour  2 - Supprimer  0 - Retour");
        if (!prompt_u64("Votre choix : ", 0, 2, &choice) || choice == 0) return;
        if (choice == 1) {
            if (!prompt_text("Compte cible : ", update.node_name, sizeof(update.node_name)) ||
                !prompt_u64("Nouveau hash power : ", 0, UINT64_MAX, &update.hash_power) ||
                !prompt_u64("Nouveau stake bloque : ", 0, UINT64_MAX, &update.stake)) return;
            if (bc_add_resource_update(chain, &update)) {
                save_persisted_accounts(state);
                puts("Mise a jour placee en attente : le stake sera bloque ou debloque apres son bloc.");
            } else {
                puts("Mise a jour refusee : compte, configuration, file ou solde disponible invalide.");
            }
        } else if (chain->pending_resource_update_count == 0) {
            puts("Aucune mise a jour a supprimer.");
        } else if (!prompt_u64("Indice de la mise a jour : ", 0,
                               chain->pending_resource_update_count - 1, &index)) {
            return;
        } else if (bc_remove_pending_resource_update(chain, (size_t)index)) {
            save_persisted_accounts(state);
            puts("Mise a jour retiree de la file.");
        }
    }
}

static void print_draft(const MenuState *state) {
    const BlockDraft *draft = &state->draft;
    const Blockchain *chain = &state->chain;
    size_t i;

    puts("\n========== BLOC CANDIDAT ==========");
    if (!draft->active) {
        puts("Aucun bloc candidat prepare.");
        return;
    }
    printf("Transactions selectionnees : %zu\n", draft->transaction_count);
    for (i = 0; i < draft->transaction_count; ++i) {
        size_t index = draft->transaction_indices[i];
        const Transaction *transaction = &chain->pending[index];
        printf("  #%zu (file %zu) | %s -> %s : %" PRIu64 " unites | EN_ATTENTE\n",
               i + 1, index, transaction->sender, transaction->recipient, transaction->amount);
    }
    printf("Mises a jour de ressources selectionnees : %zu\n", draft->resource_update_count);
    for (i = 0; i < draft->resource_update_count; ++i) {
        size_t index = draft->resource_update_indices[i];
        const ResourceUpdate *update = &chain->pending_resource_updates[index];
        const Node *node = bc_find_node(chain, update->node_name);
        printf("  #%zu (file %zu) | %s | hash power %" PRIu64 " -> %" PRIu64
               " | stake bloque %" PRIu64 " -> %" PRIu64,
               i + 1, index, update->node_name, node == NULL ? 0 : node->hash_power,
               update->hash_power, node == NULL ? 0 : node->stake, update->stake);
        if (node != NULL && update->stake > node->stake) {
            printf(" | a bloquer : %" PRIu64 " | EN_ATTENTE\n", update->stake - node->stake);
        } else if (node != NULL && update->stake < node->stake) {
            printf(" | a debloquer : %" PRIu64 " | EN_ATTENTE\n", node->stake - update->stake);
        } else puts(" | disponible inchange | EN_ATTENTE");
    }
    printf("Consensus choisi       : %s\n", bc_consensus_name(draft->consensus));
    printf("Prevalidation : %s\n", bc_candidate_status_message(draft->status));
    if (draft->status != BC_CANDIDATE_READY) return;
    printf("Hauteur du futur bloc : %" PRIu32 "\n", draft->preview.height);
    printf("Parent              : %s\n", draft->preview.previous_hash);
    printf("Producteur prevu    : %s\n", draft->preview.producer);
    printf("Recompense          : %" PRIu64 "\n", draft->preview.reward);
    if (draft->consensus == CONSENSUS_POW) {
        printf("Minage requis       : nonce avec %u zero(s) hexadecimal(aux)\n", chain->difficulty);
    } else {
        puts("Minage requis       : validation PoS sans recherche de nonce.");
    }
}

static void prepare_draft(MenuState *state) {
    Blockchain *chain = &state->chain;
    BlockDraft *draft = &state->draft;
    uint64_t count, index, consensus_choice;
    size_t i, j;

    if (chain->block_count != 0 && !bc_validate(chain)) {
        puts("La chaine est invalide. Le minage est bloque ; verifiez-la ou reinitialisez-la.");
        return;
    }
    if (draft->active) {
        puts("Un bloc candidat existe deja. Visualisez-le, confirmez-le ou abandonnez-le.");
        return;
    }
    if (chain->pending_count == 0 && chain->pending_resource_update_count == 0) {
        puts("Aucune transaction ni mise a jour de ressources en attente a regrouper.");
        return;
    }
    memset(draft, 0, sizeof(*draft));
    draft->active = true;
    if (chain->pending_count > 0) {
        print_pending(chain);
        if (!prompt_u64("Nombre de transactions a regrouper (0 a 8) : ", 0,
                        chain->pending_count < BC_MAX_TRANSACTIONS_PER_BLOCK ?
                        chain->pending_count : BC_MAX_TRANSACTIONS_PER_BLOCK, &count)) {
            memset(draft, 0, sizeof(*draft));
            return;
        }
        draft->transaction_count = (size_t)count;
    }
    for (i = 0; i < draft->transaction_count; ++i) {
        for (;;) {
            if (!prompt_u64("Indice de la transaction suivante : ", 0,
                            chain->pending_count - 1, &index)) {
                memset(draft, 0, sizeof(*draft));
                return;
            }
            for (j = 0; j < i; ++j) {
                if (draft->transaction_indices[j] == (size_t)index) break;
            }
            if (j == i) {
                draft->transaction_indices[i] = (size_t)index;
                break;
            }
            puts("Cette transaction est deja dans le bloc candidat. Choisissez un autre indice.");
        }
    }
    if (chain->pending_resource_update_count > 0) {
        print_pending_resource_updates(chain);
        if (!prompt_u64("Nombre de mises a jour a inclure (0 a 4) : ", 0,
                        chain->pending_resource_update_count < BC_MAX_RESOURCE_UPDATES_PER_BLOCK ?
                        chain->pending_resource_update_count : BC_MAX_RESOURCE_UPDATES_PER_BLOCK, &count)) {
            memset(draft, 0, sizeof(*draft));
            return;
        }
        draft->resource_update_count = (size_t)count;
    }
    for (i = 0; i < draft->resource_update_count; ++i) {
        for (;;) {
            if (!prompt_u64("Indice de la mise a jour suivante : ", 0,
                            chain->pending_resource_update_count - 1, &index)) {
                memset(draft, 0, sizeof(*draft));
                return;
            }
            for (j = 0; j < i; ++j) {
                if (draft->resource_update_indices[j] == (size_t)index) break;
            }
            if (j == i) {
                draft->resource_update_indices[i] = (size_t)index;
                break;
            }
            puts("Cette mise a jour est deja dans le bloc candidat. Choisissez un autre indice.");
        }
    }
    if (draft->transaction_count == 0 && draft->resource_update_count == 0) {
        puts("Le bloc candidat doit contenir une transaction ou une mise a jour de ressources.");
        memset(draft, 0, sizeof(*draft));
        return;
    }
    if (!prompt_u64("Consensus du bloc (1 - PoW, 2 - PoS) : ", 1, 2, &consensus_choice)) {
        memset(draft, 0, sizeof(*draft));
        return;
    }
    draft->consensus = consensus_choice == 1 ? CONSENSUS_POW : CONSENSUS_POS;
    if (chain->block_count == 0) {
        if (!has_consensus_resource(chain, draft->consensus)) {
            puts("Impossible : aucune ressource disponible pour le consensus choisi.");
            memset(draft, 0, sizeof(*draft));
            return;
        }
        if (!bc_create_genesis(chain)) {
            puts("Creation automatique du genesis impossible.");
            memset(draft, 0, sizeof(*draft));
            return;
        }
        puts("Bloc genesis cree automatiquement. La configuration et les participants sont verrouilles.");
    }
    draft->status = bc_preview_selected_block(chain, draft->consensus, draft->transaction_indices,
                                               draft->transaction_count, draft->resource_update_indices,
                                               draft->resource_update_count, &draft->preview);
    save_persisted_accounts(state);
    print_draft(state);
}

static void confirm_and_mine_draft(MenuState *state) {
    BlockDraft *draft = &state->draft;
    BlockCandidateStatus status;
    const Block *block;
    size_t i;
    int confirm;

    if (!draft->active) {
        puts("Preparez d'abord un bloc candidat.");
        return;
    }
    print_draft(state);
    if (draft->status != BC_CANDIDATE_READY) {
        puts("Ce bloc candidat ne peut pas etre ajoute. Abandonnez-le puis preparez une autre selection.");
        return;
    }
    if (!prompt_yes_no("Confirmer le minage et l'ajout de ce bloc ? (o/n) : ", &confirm) || !confirm) {
        puts("Bloc candidat conserve sans modification de la file.");
        return;
    }
    status = bc_produce_selected_block(&state->chain, draft->consensus, draft->transaction_indices,
                                       draft->transaction_count, draft->resource_update_indices,
                                       draft->resource_update_count, &state->metrics);
    if (status != BC_CANDIDATE_READY) {
        draft->status = status;
        puts("Ajout impossible : la prevalidation doit etre refaite.");
        print_draft(state);
        return;
    }
    block = &state->chain.blocks[state->chain.block_count - 1];
    printf("\nBLOC %" PRIu32 " AJOUTE APRES MINAGE\n", block->height);
    printf("Consensus  : %s\nProducteur : %s\nHash       : %s\n",
           bc_consensus_name(block->consensus), block->producer, block->hash);
    if (block->consensus == CONSENSUS_POW) printf("Nonce      : %" PRIu64 "\n", block->nonce);
    puts("Transactions maintenant VALIDEES :");
    for (i = 0; i < block->transaction_count; ++i) {
        printf("  VALIDEE | %s -> %s : %" PRIu64 " unites\n", block->transactions[i].sender,
               block->transactions[i].recipient, block->transactions[i].amount);
    }
    if (block->resource_update_count > 0) {
        puts("Mises a jour de ressources appliquees (actives au bloc suivant) :");
        for (i = 0; i < block->resource_update_count; ++i) {
            const ResourceUpdate *update = &block->resource_updates[i];
            const Node *node = bc_find_node(&state->chain, update->node_name);
            uint64_t total = 0;
            (void)bc_node_total_balance(node, &total);
            printf("  APPLIQUEE | %s : hash power -> %" PRIu64 ", stake bloque -> %" PRIu64
                   ", disponible -> %" PRIu64 ", total -> %" PRIu64 "\n",
                   update->node_name, node == NULL ? update->hash_power : node->hash_power,
                   node == NULL ? update->stake : node->stake,
                   node == NULL ? 0 : node->balance, total);
        }
    }
    printf("Transactions toujours en attente : %zu\n", state->chain.pending_count);
    memset(draft, 0, sizeof(*draft));
    if (save_persisted_accounts(state)) {
        puts("Soldes, recompenses et ressources sauvegardes dans le registre de comptes.");
    }
}

static void discard_draft(MenuState *state) {
    if (!state->draft.active) {
        puts("Aucun bloc candidat a abandonner.");
        return;
    }
    memset(&state->draft, 0, sizeof(state->draft));
    save_persisted_accounts(state);
    puts("Bloc candidat abandonne. Transactions et mises a jour restent EN_ATTENTE.");
}

static void verify_chain(const MenuState *state) {
    if (state->chain.block_count == 0) puts("Aucune chaine a verifier avant le genesis.");
    else printf("Resultat : BLOCKCHAIN %s\n", bc_validate(&state->chain) ? "VALIDE" : "INVALIDE");
}

static const char *protection_state(bool enabled) { return enabled ? "ACTIVE" : "DESACTIVE"; }

static void manage_mitigation(MenuState *state) {
    uint64_t choice;
    int value;
    for (;;) {
        puts("\n--- Protections et mitigation ---");
        printf("Protection globale             : %s\n", protection_state(state->mitigation.enabled));
        printf("1 - Protection 51 %%            : %s\n", protection_state(state->mitigation.protect_51));
        printf("2 - Protection selfish mining : %s\n", protection_state(state->mitigation.protect_selfish));
        printf("3 - Protection withholding    : %s\n", protection_state(state->mitigation.protect_withholding));
        printf("4 - Protection Sybil          : %s\n", protection_state(state->mitigation.protect_sybil));
        puts("5 - Activer/desactiver la protection globale");
        puts("Seuil : alertes ELEVE ou CRITIQUE | 6 confirmations | reorganisation maximale : 1 | voisins Sybil : 50 %");
        puts("0 - Retour");
        if (!prompt_u64("Votre choix : ", 0, 5, &choice) || choice == 0) return;
        if (choice == 5) {
            if (!prompt_yes_no("Activer la protection globale ? (o/n) : ", &value)) return;
            state->mitigation.enabled = value != 0;
        } else if (choice == 1) state->mitigation.protect_51 = !state->mitigation.protect_51;
        else if (choice == 2) state->mitigation.protect_selfish = !state->mitigation.protect_selfish;
        else if (choice == 3) state->mitigation.protect_withholding = !state->mitigation.protect_withholding;
        else if (choice == 4) state->mitigation.protect_sybil = !state->mitigation.protect_sybil;
        if (save_persisted_accounts(state)) puts("Configuration de protection sauvegardee.");
    }
}

static void run_evaluation_menu(const MenuState *state) {
    EvaluationConfig config;
    EvaluationReport report;
    uint64_t seeds;
    evaluation_config_default(&config);
    config.seed_base = state->chain.seed;
    puts("\n--- Campagne d'evaluation ---");
    puts("Grille : 3, 5, 8 noeuds ; delais 1, 2, 3 ticks ; quatre attaques.");
    if (!prompt_u64("Nombre de seeds (1 a 10, defaut recommande 3) : ", 1, EVALUATION_MAX_SEEDS, &seeds)) return;
    config.seed_count = (size_t)seeds;
    if (!evaluation_run_campaign(&config, &report)) {
        puts("Echec de la campagne d'evaluation.");
        return;
    }
    evaluation_print_report(&report);
}

static int prompt_attack_names(const Blockchain *chain, const char *label,
                               char names[][BC_NAME_SIZE], size_t *count, size_t minimum) {
    uint64_t requested;
    size_t i;
    if (!prompt_u64(label, minimum, BC_MAX_NODES, &requested)) return 0;
    *count = (size_t)requested;
    for (i = 0; i < *count; ++i) {
        if (!prompt_text("Nom du compte : ", names[i], BC_NAME_SIZE) ||
            bc_find_node(chain, names[i]) == NULL) {
            puts("Compte inconnu.");
            return 0;
        }
    }
    return 1;
}

static void run_attack_menu(MenuState *state) {
    AttackScenario scenario;
    MetricsExperiment experiment;
    AttackResult *result;
    MitigationResult mitigation;
    uint64_t choice, value;
    int adopt;

    memset(&scenario, 0, sizeof(scenario));
    puts("\n--- Simuler une attaque ---");
    puts("1 - 51 % / double spending   2 - Selfish mining");
    puts("3 - Block withholding        4 - Sybil P2P");
    if (!prompt_u64("Votre choix : ", 1, 4, &choice)) return;
    scenario.type = (AttackType)(choice - 1);
    scenario.seed = state->chain.seed;
    scenario.rounds = 20;
    scenario.gamma = 50;
    if (!prompt_u64("Nombre maximal de tours : ", 1, 100, &value)) return;
    scenario.rounds = (size_t)value;
    if (scenario.type == ATTACK_51) {
        if (!prompt_u64("Consensus (1 - PoW, 2 - PoS) : ", 1, 2, &value)) return;
        scenario.consensus = value == 1 ? CONSENSUS_POW : CONSENSUS_POS;
        if (!prompt_attack_names(&state->chain, "Nombre de comptes attaquants : ",
                                 scenario.attackers, &scenario.attacker_count, 1) ||
            !prompt_text("Compte depensant : ", scenario.spender, sizeof(scenario.spender)) ||
            !prompt_text("Marchand : ", scenario.merchant, sizeof(scenario.merchant)) ||
            !prompt_text("Destinataire prive : ", scenario.private_recipient, sizeof(scenario.private_recipient)) ||
            !prompt_u64("Montant : ", 1, UINT64_MAX, &scenario.amount) ||
            !prompt_u64("Confirmations marchand : ", 0, 20, &value)) return;
        scenario.confirmations = (size_t)value;
    } else if (scenario.type == ATTACK_SELFISH_MINING) {
        scenario.consensus = CONSENSUS_POW;
        if (!prompt_attack_names(&state->chain, "Nombre de comptes attaquants : ",
                                 scenario.attackers, &scenario.attacker_count, 1) ||
            !prompt_u64("Gamma (0 a 100) : ", 0, 100, &value)) return;
        scenario.gamma = (unsigned)value;
    } else if (scenario.type == ATTACK_BLOCK_WITHHOLDING) {
        scenario.consensus = CONSENSUS_POW;
        if (!prompt_attack_names(&state->chain, "Nombre de membres de la pool : ",
                                 scenario.pool_members, &scenario.pool_member_count, 2) ||
            !prompt_attack_names(&state->chain, "Nombre de membres malveillants : ",
                                 scenario.attackers, &scenario.attacker_count, 1)) return;
    } else {
        if (!prompt_text("Pair victime : ", scenario.victim, sizeof(scenario.victim)) ||
            !prompt_u64("Nombre de voisins de la victime : ", 1, BC_MAX_NODES, &value)) return;
        scenario.victim_degree = (size_t)value;
        if (!prompt_u64("Nombre d'identites Sybil : ", 1, 100, &value)) return;
        scenario.sybil_count = (size_t)value;
    }
    if (!metrics_run_experiment(&state->chain, &scenario, &experiment)) {
        printf("Scenario refuse ou invalide.\n");
        return;
    }
    result = &experiment.attack;
    attack_print_result(result);
    {
        char directory[METRICS_PATH_SIZE];
        DetectionReport report;
        detection_analyze(&experiment, &report);
        detection_print_report(&report);
        if (!mitigation_apply(&experiment, &report, &state->mitigation, &mitigation)) {
            puts("Avertissement : impossibilite de construire la simulation protegee.");
        }
        mitigation_print_report(&mitigation);
        metrics_print_experiment(&experiment);
        if (metrics_default_directory(directory) && metrics_export_csv(&experiment, directory)) {
            detection_export_csv(&report, &experiment);
            mitigation_export_csv(&mitigation, &experiment);
            printf("Metriques CSV exportees : %s\n", experiment.export_path);
        } else puts("Avertissement : export CSV des metriques impossible.");
    }
    if (scenario.type != ATTACK_SYBIL && !mitigation.adoption_blocked && result->success && result->attacked_valid &&
        result->attacked_chain.block_count > state->chain.block_count &&
        prompt_yes_no("Adopter la chaine attaquee comme chaine active ? (o/n) : ", &adopt) && adopt) {
        state->chain = result->attacked_chain;
        memset(&state->draft, 0, sizeof(state->draft));
        if (save_persisted_accounts(state)) puts("Chaine attaquee adoptee et comptes sauvegardes.");
    } else if (mitigation.adoption_blocked) puts("Adoption bloquee par la mitigation : la chaine active est conservee.");
}

static void print_menu(const MenuState *state) {
    const Blockchain *chain = &state->chain;
    puts("\n========================================");
    puts("   ATELIER 2 - PILOTAGE BLOCKCHAIN");
    puts("========================================");
    printf("Etat : %zu bloc(s), %zu transaction(s), %zu mise(s) a jour en attente, %s\n", chain->block_count,
           chain->pending_count, chain->pending_resource_update_count,
           chain->block_count == 0 ? "configuration ouverte" :
           (bc_validate(chain) ? "chaine valide" : "chaine invalide"));
    if (state->draft.active) {
        printf("Brouillon actif : %zu transaction(s), %zu mise(s) a jour, %s\n",
               state->draft.transaction_count, state->draft.resource_update_count,
               state->draft.status == BC_CANDIDATE_READY ? "PRET A MINER" : "A CORRIGER");
    }
    puts("1 - Configurer les parametres");
    puts("2 - Gerer les participants");
    puts("3 - Gerer les transactions en attente");
    puts("4 - Gerer les mises a jour de ressources");
    puts("5 - Preparer un bloc candidat (choix PoW ou PoS)");
    puts("6 - Visualiser le bloc candidat");
    puts("7 - Confirmer, miner et ajouter le bloc candidat");
    puts("8 - Abandonner le bloc candidat");
    puts("9 - Afficher la chaine");
    puts("10 - Afficher etat, metriques et soldes");
    puts("11 - Verifier l'integrite");
    puts("12 - Reinitialiser la blockchain");
    puts("13 - Simuler une attaque");
    puts("14 - Gerer les protections");
    puts("15 - Lancer une campagne d'evaluation");
    puts("0 - Quitter");
}

int run_interactive_menu(void) {
    MenuState state;
    uint64_t choice;
    int confirm;

    puts("Bienvenue dans le simulateur interactif de blockchain PoW/PoS.");
    memset(&state, 0, sizeof(state));
    if (!account_store_default_path(state.account_store_path)) {
        puts("Erreur : le chemin du registre de comptes est invalide.");
        return EXIT_FAILURE;
    }
    if (!session_store_default_path(state.account_store_path, state.session_store_path)) {
        puts("Erreur : le chemin de session blockchain est invalide.");
        return EXIT_FAILURE;
    }
    printf("Registre de comptes : %s\n", state.account_store_path);
    printf("Session blockchain : %s\n", state.session_store_path);
    if (!load_persisted_session(&state, 1)) return EXIT_FAILURE;
    for (;;) {
        print_menu(&state);
        if (!prompt_u64("Votre choix : ", 0, 15, &choice)) {
            puts("Execution interrompue.");
            return EXIT_FAILURE;
        }
        switch (choice) {
            case 0: puts("Au revoir."); return EXIT_SUCCESS;
            case 1: configure_parameters(&state); break;
            case 2: manage_nodes(&state); break;
            case 3: manage_transactions(&state); break;
            case 4: manage_resource_updates(&state); break;
            case 5: prepare_draft(&state); break;
            case 6: print_draft(&state); break;
            case 7: confirm_and_mine_draft(&state); break;
            case 8: discard_draft(&state); break;
            case 9: print_chain(&state.chain); break;
            case 10: print_summary(&state); break;
            case 11: verify_chain(&state); break;
            case 12:
                if (!prompt_yes_no("La chaine courante sera perdue. Reinitialiser ? (o/n) : ", &confirm)) return EXIT_FAILURE;
                if (confirm) {
                    if (!session_store_reset(state.session_store_path)) {
                        puts("ERREUR : la session sauvegardee n'a pas pu etre supprimee.");
                    } else if (load_persisted_accounts(&state, 1)) {
                        puts("Nouvelle blockchain vide creee avec les comptes sauvegardes.");
                    }
                }
                break;
            case 13: run_attack_menu(&state); break;
            case 14: manage_mitigation(&state); break;
            case 15: run_evaluation_menu(&state); break;
            default: break;
        }
    }
}
