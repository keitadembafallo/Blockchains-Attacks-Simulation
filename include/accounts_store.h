#ifndef ATELIER2_ACCOUNTS_STORE_H
#define ATELIER2_ACCOUNTS_STORE_H

#include <stddef.h>

#include "blockchain.h"

#define ACCOUNT_STORE_PATH_SIZE 512

typedef enum {
    ACCOUNT_STORE_OK,
    ACCOUNT_STORE_NOT_FOUND,
    ACCOUNT_STORE_IO_ERROR
} AccountStoreStatus;

typedef struct {
    size_t loaded;
    size_t skipped;
} AccountStoreReport;

/* Determine le chemin par defaut data/comptes.csv, a cote de l'executable. */
bool account_store_default_path(char output[ACCOUNT_STORE_PATH_SIZE]);
AccountStoreStatus account_store_load(Blockchain *chain, const char *path,
                                      AccountStoreReport *report);
bool account_store_save(const Blockchain *chain, const char *path);

#endif
