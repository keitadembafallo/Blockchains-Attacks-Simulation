#ifndef ATELIER2_SHA256_H
#define ATELIER2_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE 65

/* Calcule SHA-256 puis renvoie les 64 caracteres hexadecimaux termines par '\0'. */
void sha256_hex(const uint8_t *data, size_t length, char output[SHA256_HEX_SIZE]);

#endif
