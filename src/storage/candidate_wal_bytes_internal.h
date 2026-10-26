#ifndef STORAGE_CANDIDATE_WAL_BYTES_INTERNAL_H
#define STORAGE_CANDIDATE_WAL_BYTES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t read_le(const uint8_t *p, unsigned size)
{
    uint64_t value = 0; for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8 * i); return value;
}

static void write_le(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool is_zero(const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++) if (p[i] != 0) return false; return true;
}

#endif
