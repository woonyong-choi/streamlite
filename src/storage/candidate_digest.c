#include "storage/candidate_digest.h"

#include <string.h>

static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotate(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t words[64];
    for (unsigned i = 0; i < 16; i++) {
        const uint8_t *p = block + i * 4;
        words[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }
    for (unsigned i = 16; i < 64; i++) {
        uint32_t x = words[i - 15], y = words[i - 2];
        uint32_t small0 = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
        uint32_t small1 = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + small0 + words[i - 7] + small1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t sum1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + sum1 + choose + constants[i] + words[i];
        uint32_t sum0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + sum0 + majority;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

candidate_wal_status_t candidate_digest_sha256(const uint8_t *bytes, size_t size, uint8_t out[32])
{
    if (out == NULL) return CANDIDATE_WAL_INVALID;
    if (size > UINT64_MAX / 8) return CANDIDATE_WAL_LIMIT;
    if (size > 0 && bytes == NULL) return CANDIDATE_WAL_INVALID;
    uint32_t state[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                         0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t offset = 0;
    while (size - offset >= 64) { compress(state, bytes + offset); offset += 64; }
    uint8_t tail[128] = {0};
    size_t remaining = size - offset;
    if (remaining > 0) memcpy(tail, bytes + offset, remaining);
    tail[remaining] = 0x80;
    size_t padded = remaining < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8;
    for (unsigned i = 0; i < 8; i++) tail[padded - 1 - i] = (uint8_t)(bits >> (i * 8));
    compress(state, tail);
    if (padded == 128) compress(state, tail + 64);
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(state[i] >> (24 - j * 8));
    return CANDIDATE_WAL_OK;
}
