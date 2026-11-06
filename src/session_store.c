#include "session_store.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static int parse_u64(const char *text, uint64_t *value) {
    char *end;
    unsigned long long parsed;
    if (text == NULL || *text == '\0' || *text == '-') return 0;
    errno = 0; parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0') return 0;
    *value = (uint64_t)parsed; return 1;
}

static int split(char *line, char *fields[], size_t maximum) {
    size_t count = 0;
    char *cursor = line;
    while (count < maximum) {
        fields[count++] = cursor;
        cursor = strchr(cursor, ';');
        if (cursor == NULL) return (int)count;
        *cursor++ = '\0';
    }
    return -1;
}

static int replace_file(const char *temporary, const char *target) {
#ifdef _WIN32
    return MoveFileExA(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, target) == 0;
#endif
}

static int parent_path(const char *path, char output[SESSION_STORE_PATH_SIZE]) {
    char *slash;
    if (strlen(path) >= SESSION_STORE_PATH_SIZE) return 0;
    snprintf(output, SESSION_STORE_PATH_SIZE, "%s", path);
    slash = strrchr(output, '/');
#ifdef _WIN32
    { char *backslash = strrchr(output, '\\'); if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash; }
#endif
    if (slash == NULL) { output[0] = '\0'; return 1; }
    *slash = '\0'; return 1;
}

bool session_store_default_path(const char *accounts_path, char output[SESSION_STORE_PATH_SIZE]) {
    char parent[SESSION_STORE_PATH_SIZE];
    if (accounts_path == NULL || !parent_path(accounts_path, parent)) return false;
#ifdef _WIN32
    return snprintf(output, SESSION_STORE_PATH_SIZE, "%s\\session\\session.active", parent) < SESSION_STORE_PATH_SIZE;
#else
    return snprintf(output, SESSION_STORE_PATH_SIZE, "%s/session/session.active", parent) < SESSION_STORE_PATH_SIZE;
#endif
}

static int ensure_parent_directory(const char *path) {
    char parent[SESSION_STORE_PATH_SIZE];
    if (!parent_path(path, parent) || parent[0] == '\0') return 1;
#ifdef _WIN32
    return CreateDirectoryA(parent, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(parent, 0777) == 0 || errno == EEXIST;
#endif
}

static int write_state(const SessionState *state, const char *path) {
    char temporary[SESSION_STORE_PATH_SIZE + 8];
    FILE *file;
    size_t i, j;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return 0;
    file = fopen(temporary, "w"); if (file == NULL) return 0;
    fprintf(file, "META;1;%u;%u;%" PRIu64 ";%" PRIu64 ";%zu;%zu;%.17g;%zu;%zu;%zu;%d;%u;%zu;%zu\n",
            (unsigned)state->chain.consensus, state->chain.difficulty, state->chain.seed, state->chain.block_reward,
            state->chain.block_count, state->chain.node_count, state->metrics.mining_seconds,
            state->metrics.blocks_produced, state->metrics.accepted_transactions, state->metrics.forks,
            state->draft.active ? 1 : 0, (unsigned)state->draft.consensus,
            state->draft.transaction_count, state->draft.resource_update_count);
    fprintf(file, "MITIGATION;%d;%d;%d;%d;%d\n", state->mitigation.enabled ? 1 : 0,
            state->mitigation.protect_51 ? 1 : 0, state->mitigation.protect_selfish ? 1 : 0,
            state->mitigation.protect_withholding ? 1 : 0, state->mitigation.protect_sybil ? 1 : 0);
    for (i = 0; i < state->chain.node_count; ++i) {
        const Node *n = &state->chain.nodes[i];
        fprintf(file, "NODE;%s;%" PRIu64 ";%" PRIu64 ";%" PRIu64 ";%" PRIu64 ";%" PRIu64 ";%" PRIu64 ";%" PRIu64 "\n",
                n->name, n->hash_power, n->stake, n->initial_hash_power, n->initial_stake,
                n->initial_balance, n->balance, n->rewards);
    }
    for (i = 0; i < state->chain.block_count; ++i) {
        const Block *b = &state->chain.blocks[i];
        fprintf(file, "BLOCK;%zu;%" PRIu32 ";%u;%s;%s;%s;%" PRIu64 ";%" PRIu64 ";%" PRIu64 ";%" PRIu64 "\n",
                i, b->height, (unsigned)b->consensus, b->previous_hash, b->hash, b->producer,
                b->selection_ticket, b->nonce, b->reward, b->transaction_count);
        for (j = 0; j < b->transaction_count; ++j) fprintf(file, "BTX;%zu;%zu;%s;%s;%" PRIu64 "\n",
            i, j, b->transactions[j].sender, b->transactions[j].recipient, b->transactions[j].amount);
        for (j = 0; j < b->resource_update_count; ++j) fprintf(file, "BUP;%zu;%zu;%s;%" PRIu64 ";%" PRIu64 "\n",
            i, j, b->resource_updates[j].node_name, b->resource_updates[j].hash_power, b->resource_updates[j].stake);
    }
    for (i = 0; i < state->chain.pending_count; ++i) fprintf(file, "PTX;%s;%s;%" PRIu64 "\n",
        state->chain.pending[i].sender, state->chain.pending[i].recipient, state->chain.pending[i].amount);
    for (i = 0; i < state->chain.pending_resource_update_count; ++i) fprintf(file, "PUP;%s;%" PRIu64 ";%" PRIu64 "\n",
        state->chain.pending_resource_updates[i].node_name, state->chain.pending_resource_updates[i].hash_power,
        state->chain.pending_resource_updates[i].stake);
    for (i = 0; i < state->draft.transaction_count; ++i) fprintf(file, "DTX;%zu\n", state->draft.transaction_indices[i]);
    for (i = 0; i < state->draft.resource_update_count; ++i) fprintf(file, "DUP;%zu\n", state->draft.resource_update_indices[i]);
    if (ferror(file) || fflush(file) != 0 || fclose(file) != 0) { remove(temporary); return 0; }
    return replace_file(temporary, path);
}

bool session_store_save(const SessionState *state, const char *active_path) {
    char parent[SESSION_STORE_PATH_SIZE], state_path[SESSION_STORE_PATH_SIZE], temporary[SESSION_STORE_PATH_SIZE + 8];
    FILE *file;
    if (state == NULL || active_path == NULL || !parent_path(active_path, parent) ||
        !ensure_parent_directory(active_path)) return false;
    if (snprintf(state_path, sizeof(state_path), "%s\\session_%" PRIu64 "_%zu.csv", parent,
                 state->chain.seed, state->chain.block_count) >= (int)sizeof(state_path)) return false;
    if (!write_state(state, state_path) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", active_path) >= (int)sizeof(temporary)) return false;
    file = fopen(temporary, "w"); if (file == NULL) return false;
    if (fprintf(file, "%s\n", state_path) < 0 || fflush(file) != 0 || fclose(file) != 0) { remove(temporary); return false; }
    return replace_file(temporary, active_path);
}

static int load_state(SessionState *state, const char *path) {
    FILE *file = fopen(path, "r");
    char line[1024];
    int saw_meta = 0;
    size_t draft_tx_loaded = 0, draft_update_loaded = 0;
    if (file == NULL) return 0;
    memset(state, 0, sizeof(*state));
    mitigation_config_default(&state->mitigation);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *f[16]; int n; uint64_t values[8]; size_t i;
        line[strcspn(line, "\r\n")] = '\0'; n = split(line, f, 16); if (n < 1) goto invalid;
        if (strcmp(f[0], "META") == 0) {
            if (n != 16 || strcmp(f[1], "1") != 0) goto invalid;
            if (!parse_u64(f[2],&values[0]) || !parse_u64(f[3],&values[1]) || !parse_u64(f[4],&values[2]) ||
                !parse_u64(f[5],&values[3]) || !parse_u64(f[6],&values[4]) || !parse_u64(f[7],&values[5]) ||
                !parse_u64(f[9],&values[6]) || !parse_u64(f[10],&values[7]) || !parse_u64(f[11],&values[0]) ||
                !parse_u64(f[12],&values[1]) || !parse_u64(f[13],&values[2]) || !parse_u64(f[14],&values[3]) ||
                !parse_u64(f[15],&values[4])) goto invalid;
            bc_init(&state->chain, (Consensus)strtoul(f[2],NULL,10), (unsigned)strtoul(f[3],NULL,10),
                    strtoull(f[4],NULL,10), strtoull(f[5],NULL,10));
            state->chain.block_count = (size_t)strtoull(f[6],NULL,10);
            state->chain.node_count = (size_t)strtoull(f[7],NULL,10);
            state->metrics.mining_seconds = strtod(f[8], NULL);
            state->metrics.blocks_produced = (size_t)strtoull(f[9],NULL,10);
            state->metrics.accepted_transactions = (size_t)strtoull(f[10],NULL,10);
            state->metrics.forks = (size_t)strtoull(f[11],NULL,10);
            state->draft.active = strtoull(f[12],NULL,10) != 0;
            state->draft.consensus = (Consensus)strtoul(f[13],NULL,10);
            state->draft.transaction_count = (size_t)strtoull(f[14],NULL,10);
            state->draft.resource_update_count = (size_t)strtoull(f[15],NULL,10); saw_meta = 1;
            if (state->chain.node_count > BC_MAX_NODES || state->chain.block_count > BC_MAX_BLOCKS ||
                state->draft.transaction_count > BC_MAX_TRANSACTIONS_PER_BLOCK ||
                state->draft.resource_update_count > BC_MAX_RESOURCE_UPDATES_PER_BLOCK) goto invalid;
        } else if (strcmp(f[0], "MITIGATION") == 0) {
            if (n != 6 || !parse_u64(f[1], &values[0]) || !parse_u64(f[2], &values[1]) ||
                !parse_u64(f[3], &values[2]) || !parse_u64(f[4], &values[3]) ||
                !parse_u64(f[5], &values[4]) || values[0] > 1 || values[1] > 1 || values[2] > 1 ||
                values[3] > 1 || values[4] > 1) goto invalid;
            state->mitigation.enabled = values[0] != 0;
            state->mitigation.protect_51 = values[1] != 0;
            state->mitigation.protect_selfish = values[2] != 0;
            state->mitigation.protect_withholding = values[3] != 0;
            state->mitigation.protect_sybil = values[4] != 0;
        } else if (strcmp(f[0], "NODE") == 0) {
            size_t index = 0; if (!saw_meta || n != 9) goto invalid;
            while (index < state->chain.node_count && state->chain.nodes[index].name[0]) index++;
            if (index >= state->chain.node_count) goto invalid;
            for (i = 0; i < 7; ++i) if (!parse_u64(f[i + 2], &values[i])) goto invalid;
            snprintf(state->chain.nodes[index].name, BC_NAME_SIZE, "%s", f[1]);
            state->chain.nodes[index].hash_power=values[0]; state->chain.nodes[index].stake=values[1];
            state->chain.nodes[index].initial_hash_power=values[2]; state->chain.nodes[index].initial_stake=values[3];
            state->chain.nodes[index].initial_balance=values[4]; state->chain.nodes[index].balance=values[5]; state->chain.nodes[index].rewards=values[6];
        } else if (strcmp(f[0], "BLOCK") == 0) {
            size_t index; if (!saw_meta || n != 11 || !parse_u64(f[1], &values[0]) || values[0] >= state->chain.block_count) goto invalid;
            if (!parse_u64(f[2],&values[1]) || !parse_u64(f[3],&values[2]) || !parse_u64(f[7],&values[3]) ||
                !parse_u64(f[8],&values[4]) || !parse_u64(f[9],&values[5]) || !parse_u64(f[10],&values[6])) goto invalid;
            index=(size_t)values[0]; state->chain.blocks[index].height=(uint32_t)values[1]; state->chain.blocks[index].consensus=(Consensus)values[2];
            snprintf(state->chain.blocks[index].previous_hash,SHA256_HEX_SIZE,"%s",f[4]); snprintf(state->chain.blocks[index].hash,SHA256_HEX_SIZE,"%s",f[5]);
            snprintf(state->chain.blocks[index].producer,BC_NAME_SIZE,"%s",f[6]); state->chain.blocks[index].selection_ticket=values[3];
            state->chain.blocks[index].nonce=values[4]; state->chain.blocks[index].reward=values[5]; state->chain.blocks[index].transaction_count=(size_t)values[6];
            if (state->chain.blocks[index].transaction_count > BC_MAX_TRANSACTIONS_PER_BLOCK) goto invalid;
        } else if (strcmp(f[0], "BTX") == 0 || strcmp(f[0], "BUP") == 0) {
            size_t block, order; if (!saw_meta || n < 5 || !parse_u64(f[1],&values[0]) || !parse_u64(f[2],&values[1])) goto invalid;
            block=(size_t)values[0]; order=(size_t)values[1]; if(block>=state->chain.block_count) goto invalid;
            if (f[0][1]=='T') { if(n!=6 || order>=BC_MAX_TRANSACTIONS_PER_BLOCK || !parse_u64(f[5],&values[2])) goto invalid;
                snprintf(state->chain.blocks[block].transactions[order].sender,BC_NAME_SIZE,"%s",f[3]); snprintf(state->chain.blocks[block].transactions[order].recipient,BC_NAME_SIZE,"%s",f[4]); state->chain.blocks[block].transactions[order].amount=values[2];
            } else { if(n!=6 || order>=BC_MAX_RESOURCE_UPDATES_PER_BLOCK || !parse_u64(f[4],&values[2]) || !parse_u64(f[5],&values[3])) goto invalid;
                snprintf(state->chain.blocks[block].resource_updates[order].node_name,BC_NAME_SIZE,"%s",f[3]); state->chain.blocks[block].resource_updates[order].hash_power=values[2]; state->chain.blocks[block].resource_updates[order].stake=values[3];
                if(order+1>state->chain.blocks[block].resource_update_count) state->chain.blocks[block].resource_update_count=order+1;
            }
        } else if (strcmp(f[0], "PTX") == 0 || strcmp(f[0], "PUP") == 0) {
            if (!saw_meta) goto invalid;
            if (f[0][1]=='T') { if(n!=4 || state->chain.pending_count==BC_MAX_PENDING_TRANSACTIONS || !parse_u64(f[3],&values[0])) goto invalid;
                snprintf(state->chain.pending[state->chain.pending_count].sender,BC_NAME_SIZE,"%s",f[1]); snprintf(state->chain.pending[state->chain.pending_count].recipient,BC_NAME_SIZE,"%s",f[2]); state->chain.pending[state->chain.pending_count++].amount=values[0];
            } else { if(n!=4 || state->chain.pending_resource_update_count==BC_MAX_PENDING_RESOURCE_UPDATES || !parse_u64(f[2],&values[0]) || !parse_u64(f[3],&values[1])) goto invalid;
                snprintf(state->chain.pending_resource_updates[state->chain.pending_resource_update_count].node_name,BC_NAME_SIZE,"%s",f[1]); state->chain.pending_resource_updates[state->chain.pending_resource_update_count].hash_power=values[0]; state->chain.pending_resource_updates[state->chain.pending_resource_update_count++].stake=values[1];
            }
        } else if (strcmp(f[0], "DTX") == 0 || strcmp(f[0], "DUP") == 0) {
            if(n!=2 || !parse_u64(f[1],&values[0])) goto invalid;
            if(f[0][1]=='T') { if(draft_tx_loaded>=state->draft.transaction_count) goto invalid; state->draft.transaction_indices[draft_tx_loaded++]=(size_t)values[0]; }
            else { if(draft_update_loaded>=state->draft.resource_update_count) goto invalid; state->draft.resource_update_indices[draft_update_loaded++]=(size_t)values[0]; }
        } else goto invalid;
    }
    fclose(file);
    if (!saw_meta || draft_tx_loaded != state->draft.transaction_count ||
        draft_update_loaded != state->draft.resource_update_count ||
        (state->chain.block_count > 0 && !bc_validate(&state->chain))) return 0;
    return 1;
invalid:
    fclose(file); return 0;
}

SessionStoreStatus session_store_load(SessionState *state, const char *active_path) {
    FILE *file; char path[SESSION_STORE_PATH_SIZE];
    if (state == NULL || active_path == NULL) return SESSION_STORE_IO_ERROR;
    file=fopen(active_path,"r"); if(file==NULL) return errno==ENOENT?SESSION_STORE_NOT_FOUND:SESSION_STORE_IO_ERROR;
    if(fgets(path,sizeof(path),file)==NULL || fclose(file)!=0) return SESSION_STORE_INVALID;
    path[strcspn(path,"\r\n")]='\0';
    return load_state(state,path)?SESSION_STORE_OK:SESSION_STORE_INVALID;
}

bool session_store_reset(const char *active_path) {
    FILE *file; char path[SESSION_STORE_PATH_SIZE];
    if (active_path == NULL) return false;
    file=fopen(active_path,"r"); if(file!=NULL) { if(fgets(path,sizeof(path),file)!=NULL) { path[strcspn(path,"\r\n")]='\0'; remove(path); } fclose(file); }
    return remove(active_path)==0 || errno==ENOENT;
}
