#include "sha256.h"

#include <string.h>

typedef struct {
    uint8_t buffer[64];
    uint32_t state[8];
    uint64_t bit_count;
    size_t buffer_length;
} Sha256Context;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(value, amount) (((value) >> (amount)) | ((value) << (32U - (amount))))
#define CHOICE(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJORITY(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BIG0(x) (ROTR((x), 2U) ^ ROTR((x), 13U) ^ ROTR((x), 22U))
#define BIG1(x) (ROTR((x), 6U) ^ ROTR((x), 11U) ^ ROTR((x), 25U))
#define SMALL0(x) (ROTR((x), 7U) ^ ROTR((x), 18U) ^ ((x) >> 3U))
#define SMALL1(x) (ROTR((x), 17U) ^ ROTR((x), 19U) ^ ((x) >> 10U))

static void sha256_transform(Sha256Context *context, const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h, temp1, temp2;
    size_t i;

    for (i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24U) | ((uint32_t)block[i * 4 + 1] << 16U) |
                   ((uint32_t)block[i * 4 + 2] << 8U) | (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; ++i) {
        words[i] = SMALL1(words[i - 2]) + words[i - 7] + SMALL0(words[i - 15]) + words[i - 16];
    }
    a = context->state[0]; b = context->state[1]; c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5]; g = context->state[6]; h = context->state[7];
    for (i = 0; i < 64; ++i) {
        temp1 = h + BIG1(e) + CHOICE(e, f, g) + K[i] + words[i];
        temp2 = BIG0(a) + MAJORITY(a, b, c);
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f; context->state[6] += g; context->state[7] += h;
}

static void sha256_digest(const uint8_t *data, size_t length, uint8_t digest[SHA256_DIGEST_SIZE]) {
    Sha256Context context = {{0}, {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, 0, 0};
    size_t i, position;
    uint64_t message_bits;

    for (i = 0; i < length; ++i) {
        context.buffer[context.buffer_length++] = data[i];
        if (context.buffer_length == 64) {
            sha256_transform(&context, context.buffer);
            context.bit_count += 512;
            context.buffer_length = 0;
        }
    }
    position = context.buffer_length;
    context.buffer[position++] = 0x80;
    if (position > 56) {
        while (position < 64) context.buffer[position++] = 0;
        sha256_transform(&context, context.buffer);
        position = 0;
    }
    while (position < 56) context.buffer[position++] = 0;
    message_bits = context.bit_count + (uint64_t)context.buffer_length * 8U;
    for (i = 0; i < 8; ++i) context.buffer[63 - i] = (uint8_t)(message_bits >> (i * 8U));
    sha256_transform(&context, context.buffer);
    for (i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(context.state[i] >> 24U);
        digest[i * 4 + 1] = (uint8_t)(context.state[i] >> 16U);
        digest[i * 4 + 2] = (uint8_t)(context.state[i] >> 8U);
        digest[i * 4 + 3] = (uint8_t)context.state[i];
    }
}

void sha256_hex(const uint8_t *data, size_t length, char output[SHA256_HEX_SIZE]) {
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[SHA256_DIGEST_SIZE];
    size_t i;

    sha256_digest(data, length, digest);
    for (i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        output[i * 2] = digits[digest[i] >> 4U];
        output[i * 2 + 1] = digits[digest[i] & 0x0fU];
    }
    output[64] = '\0';
}
