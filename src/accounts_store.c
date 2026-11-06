#include "accounts_store.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static int parse_u64(const char *text, uint64_t *result) {
    char *end;
    unsigned long long value;
    if (text == NULL || text[0] == '\0' || text[0] == '-') return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || *end != '\0') return 0;
    *result = (uint64_t)value;
    return 1;
}

static int make_parent_directory(const char *path) {
    char directory[ACCOUNT_STORE_PATH_SIZE];
    char *separator;
    size_t length;

    if (path == NULL || strlen(path) >= sizeof(directory)) return 0;
    snprintf(directory, sizeof(directory), "%s", path);
    separator = strrchr(directory, '/');
#ifdef _WIN32
    {
        char *backslash = strrchr(directory, '\\');
        if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
    }
#endif
    if (separator == NULL) return 1;
    *separator = '\0';
    length = strlen(directory);
    if (length == 0) return 1;
#ifdef _WIN32
    if (CreateDirectoryA(directory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
#else
    if (mkdir(directory, 0777) == 0 || errno == EEXIST) return 1;
#endif
    return 0;
}

bool account_store_default_path(char output[ACCOUNT_STORE_PATH_SIZE]) {
    const char *override_path = getenv("ATELIER2_ACCOUNTS_FILE");
    if (override_path != NULL && override_path[0] != '\0') {
        if (strlen(override_path) >= ACCOUNT_STORE_PATH_SIZE) return false;
        snprintf(output, ACCOUNT_STORE_PATH_SIZE, "%s", override_path);
        return true;
    }
#ifdef _WIN32
    {
        char executable[ACCOUNT_STORE_PATH_SIZE];
        char *separator;
        DWORD length = GetModuleFileNameA(NULL, executable, (DWORD)sizeof(executable));
        if (length == 0 || length >= sizeof(executable)) return false;
        separator = strrchr(executable, '\\');
        if (separator == NULL) return false;
        *separator = '\0';
        if (snprintf(output, ACCOUNT_STORE_PATH_SIZE, "%s\\data\\comptes.csv", executable) >=
            ACCOUNT_STORE_PATH_SIZE) return false;
        return true;
    }
#else
    snprintf(output, ACCOUNT_STORE_PATH_SIZE, "data/comptes.csv");
    return true;
#endif
}

AccountStoreStatus account_store_load(Blockchain *chain, const char *path,
                                      AccountStoreReport *report) {
    FILE *file;
    char line[256];

    if (report != NULL) memset(report, 0, sizeof(*report));
    if (chain == NULL || path == NULL) return ACCOUNT_STORE_IO_ERROR;
    file = fopen(path, "r");
    if (file == NULL) return errno == ENOENT ? ACCOUNT_STORE_NOT_FOUND : ACCOUNT_STORE_IO_ERROR;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[5] = {NULL};
        char *cursor;
        size_t field_count = 0;
        uint64_t hash_power, stake, available_balance, total_balance;
        size_t length = strcspn(line, "\r\n");

        line[length] = '\0';
        if (line[0] == '\0' ||
            strcmp(line, "name;hash_power;stake;balance") == 0 ||
            strcmp(line, "name;hash_power;locked_stake;available_balance") == 0) continue;
        cursor = line;
        while (field_count < 5) {
            fields[field_count++] = cursor;
            cursor = strchr(cursor, ';');
            if (cursor == NULL) break;
            *cursor++ = '\0';
        }
        if (field_count != 4 || strchr(fields[3], ';') != NULL ||
            !parse_u64(fields[1], &hash_power) || !parse_u64(fields[2], &stake) ||
            !parse_u64(fields[3], &available_balance) ||
            UINT64_MAX - available_balance < stake) {
            if (report != NULL) report->skipped++;
            continue;
        }
        total_balance = available_balance + stake;
        if (!bc_add_node(chain, fields[0], hash_power, stake, total_balance)) {
            if (report != NULL) report->skipped++;
            continue;
        }
        if (report != NULL) report->loaded++;
    }
    {
        int read_error = ferror(file);
        if (fclose(file) != 0) read_error = 1;
        if (read_error) return ACCOUNT_STORE_IO_ERROR;
    }
    return ACCOUNT_STORE_OK;
}

bool account_store_save(const Blockchain *chain, const char *path) {
    char temporary_path[ACCOUNT_STORE_PATH_SIZE + 8];
    FILE *file;
    size_t i;

    if (chain == NULL || path == NULL || !make_parent_directory(path) ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) >= (int)sizeof(temporary_path)) {
        return false;
    }
    file = fopen(temporary_path, "w");
    if (file == NULL) return false;
    if (fprintf(file, "name;hash_power;locked_stake;available_balance\n") < 0) {
        fclose(file);
        remove(temporary_path);
        return false;
    }
    for (i = 0; i < chain->node_count; ++i) {
        const Node *node = &chain->nodes[i];
        if (fprintf(file, "%s;%" PRIu64 ";%" PRIu64 ";%" PRIu64 "\n", node->name,
                    node->hash_power, node->stake, node->balance) < 0) {
            fclose(file);
            remove(temporary_path);
            return false;
        }
    }
    if (fflush(file) != 0 || fclose(file) != 0) {
        remove(temporary_path);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(temporary_path);
        return false;
    }
#else
    if (rename(temporary_path, path) != 0) {
        remove(temporary_path);
        return false;
    }
#endif
    return true;
}
