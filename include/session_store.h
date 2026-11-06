#ifndef ATELIER2_SESSION_STORE_H
#define ATELIER2_SESSION_STORE_H

#include "blockchain.h"
#include "mitigation.h"

#define SESSION_STORE_PATH_SIZE 512

typedef struct {
    bool active;
    Consensus consensus;
    size_t transaction_indices[BC_MAX_TRANSACTIONS_PER_BLOCK];
    size_t transaction_count;
    size_t resource_update_indices[BC_MAX_RESOURCE_UPDATES_PER_BLOCK];
    size_t resource_update_count;
} SessionDraftState;

typedef struct {
    Blockchain chain;
    RunMetrics metrics;
    SessionDraftState draft;
    MitigationConfig mitigation;
} SessionState;

typedef enum {
    SESSION_STORE_OK,
    SESSION_STORE_NOT_FOUND,
    SESSION_STORE_INVALID,
    SESSION_STORE_IO_ERROR
} SessionStoreStatus;

bool session_store_default_path(const char *accounts_path, char output[SESSION_STORE_PATH_SIZE]);
SessionStoreStatus session_store_load(SessionState *state, const char *active_path);
bool session_store_save(const SessionState *state, const char *active_path);
bool session_store_reset(const char *active_path);

#endif
