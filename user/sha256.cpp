// SHA-256, used to check passwords without ever storing them.

#include "libc.h"

#define BLOCK_BYTES 64

static const uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotate_right(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void process_block(uint32_t state[8], const uint8_t *block)
{
    uint32_t words[64];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 16; i++) {
        words[i] = ((uint32_t)block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
                   (block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }

    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
                      rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
                      rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);

        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + choice + round_constants[i] + words[i];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256(const void *data, size_t length, uint8_t digest[32])
{
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    const uint8_t *input = (const uint8_t *)data;
    uint8_t last[BLOCK_BYTES * 2];
    size_t remaining = length;
    size_t last_size;
    uint32_t bit_length = (uint32_t)length * 8;

    while (remaining >= BLOCK_BYTES) {
        process_block(state, input);
        input += BLOCK_BYTES;
        remaining -= BLOCK_BYTES;
    }

    // The end of the message: a 1 bit, zeros, then the length in bits.
    memset(last, 0, sizeof(last));
    memcpy(last, input, remaining);
    last[remaining] = 0x80;
    last_size = (remaining < 56) ? BLOCK_BYTES : BLOCK_BYTES * 2;

    last[last_size - 4] = bit_length >> 24;
    last[last_size - 3] = bit_length >> 16;
    last[last_size - 2] = bit_length >> 8;
    last[last_size - 1] = bit_length;

    for (size_t offset = 0; offset < last_size; offset += BLOCK_BYTES)
        process_block(state, last + offset);

    for (int i = 0; i < 8; i++) {
        digest[i * 4] = state[i] >> 24;
        digest[i * 4 + 1] = state[i] >> 16;
        digest[i * 4 + 2] = state[i] >> 8;
        digest[i * 4 + 3] = state[i];
    }
}
