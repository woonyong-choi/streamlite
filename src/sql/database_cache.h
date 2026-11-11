#ifndef DATABASE_CACHE_H
#define DATABASE_CACHE_H

#include "sql/database.h"
#include "statement_clone.h"

#ifndef STREAMLITE_SQL_TEMPLATE_CACHE
#define STREAMLITE_SQL_TEMPLATE_CACHE 0
#endif

#define DATABASE_CACHE_MAX_ENTRIES 64u
#define DATABASE_CACHE_MAX_BYTES ((size_t)1 << 20)
#define DATABASE_CACHE_MAX_SQL ((size_t)16 << 10)

typedef struct database_cache_entry database_cache_entry_t;
typedef struct database_cache database_cache_t;

typedef struct {
    size_t entries;
    size_t bytes;
    size_t hits;
    size_t misses;
    size_t evictions;
    size_t invalidations;
    size_t bypasses;
} database_cache_stats_t;

void database_cache_set_enabled(database_t *db, bool is_enabled);
void database_cache_stats(const database_t *db, database_cache_stats_t *out);
database_cache_entry_t *database_cache_find(database_t *db, const uint8_t *sql, size_t size);
uint32_t database_cache_parameter_count(const database_cache_entry_t *entry);
size_t database_cache_parse_peak(const database_cache_entry_t *entry, const lexer_binding_t *binding);
sqlstmt_status_t database_cache_instantiate(const database_cache_entry_t *entry,
                                           statement_clone_t *context, sqlstmt_t **out);
void database_cache_unpin(database_t *db, database_cache_entry_t *entry);
void database_cache_admit(database_t *db, const uint8_t *sql, size_t size,
                          const lexer_statement_t *tokens, const sqlstmt_t *statement);
void database_cache_clear(database_t *db);
bool database_cache_trim(database_t *db);
void database_cache_close(database_t *db);

#endif
