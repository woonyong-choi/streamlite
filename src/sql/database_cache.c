#include "database_exec.h"
#include "../storage/value_internal.h"
#include "../benchmark.h"

#define TEMPLATE_FORMAT 1u

typedef struct {
    uint32_t id;
    size_t offset;
} cached_parameter_t;

struct database_cache_entry {
    database_cache_entry_t *previous;
    database_cache_entry_t *next;
    sqlstmt_t *syntax;
    cached_parameter_t *parameters;
    size_t parameter_occurrences;
    uint32_t parameter_count;
    uint32_t format;
    size_t size;
    size_t bytes;
    size_t parse_peak;
    size_t pins;
    bool is_retired;
    uint8_t sql[];
};

struct database_cache {
    database_cache_entry_t *head;
    database_cache_entry_t *tail;
    database_cache_stats_t stats;
};

static bool is_enabled(const database_t *db)
{
    bool is_selected = db->cache_mode > 0 || (db->cache_mode == 0 && STREAMLITE_SQL_TEMPLATE_CACHE);
    bool has_owner = !mem_ledger_enabled(&db->ledger) || mem_ledger_enabled(&db->resident);
    return is_selected && has_owner;
}

static database_cache_t *get_cache(database_t *db)
{
    if (db->cache != NULL) return db->cache;
    size_t size = sizeof(*db->cache);
    if (!mem_reserve(&db->resident, MEM_SCOPE_RESIDENT, size)) return NULL;
    db->cache = calloc(1, size);
    if (db->cache == NULL) {
        mem_release(&db->resident, MEM_SCOPE_RESIDENT, size);
        return NULL;
    }
    db->cache->stats.bytes = size;
    return db->cache;
}

static void unlink_entry(database_cache_t *cache, database_cache_entry_t *entry)
{
    if (entry->previous != NULL) entry->previous->next = entry->next;
    else cache->head = entry->next;
    if (entry->next != NULL) entry->next->previous = entry->previous;
    else cache->tail = entry->previous;
}

static void push_head(database_cache_t *cache, database_cache_entry_t *entry)
{
    entry->previous = NULL;
    entry->next = cache->head;
    if (cache->head != NULL) cache->head->previous = entry;
    else cache->tail = entry;
    cache->head = entry;
}

static void free_entry(database_t *db, database_cache_entry_t *entry)
{
    assert(entry->pins == 0);
    size_t bytes = entry->bytes;
    unlink_entry(db->cache, entry);
    sqlstmt_free(entry->syntax);
    free(entry->parameters);
    free(entry);
    db->cache->stats.entries--;
    db->cache->stats.bytes -= bytes;
    mem_release(&db->resident, MEM_SCOPE_RESIDENT, bytes);
}

void database_cache_set_enabled(database_t *db, bool is_selected)
{
    if (!is_selected) database_cache_close(db);
    db->cache_mode = is_selected ? 1 : -1;
}

void database_cache_stats(const database_t *db, database_cache_stats_t *out)
{
    *out = db->cache == NULL ? (database_cache_stats_t){0} : db->cache->stats;
}

database_cache_entry_t *database_cache_find(database_t *db, const uint8_t *sql, size_t size)
{
    if (mem_ledger_enabled(&db->ledger) && !mem_ledger_enabled(&db->resident)) database_cache_close(db);
    if (!is_enabled(db)) return NULL;
    if (size > DATABASE_CACHE_MAX_SQL) {
        if (db->cache != NULL) db->cache->stats.bypasses++;
        return NULL;
    }
    database_cache_t *cache = db->cache;
    if (cache == NULL) return NULL;
    for (database_cache_entry_t *entry = cache->head; entry != NULL; entry = entry->next) {
        if (entry->is_retired || entry->format != TEMPLATE_FORMAT || entry->size != size) continue;
        if (size != 0 && memcmp(entry->sql, sql, size) != 0) continue;
        unlink_entry(cache, entry);
        push_head(cache, entry);
        entry->pins++;
        cache->stats.hits++;
        return entry;
    }
    cache->stats.misses++;
    return NULL;
}

uint32_t database_cache_parameter_count(const database_cache_entry_t *entry)
{
    return entry->parameter_count;
}

size_t database_cache_parse_peak(const database_cache_entry_t *entry, const lexer_binding_t *binding)
{
    size_t peak = entry->parse_peak;
    for (size_t i = 0; i < entry->parameter_occurrences; i++) {
        size_t size = binding->values[entry->parameters[i].id - 1].size_bytes;
        if (size > SIZE_MAX - peak) return SIZE_MAX;
        peak += size;
    }
    return peak;
}

sqlstmt_status_t database_cache_instantiate(const database_cache_entry_t *entry,
                                           statement_clone_t *context, sqlstmt_t **out)
{
    for (size_t i = 0; i < entry->parameter_occurrences; i++) {
        const cached_parameter_t *parameter = &entry->parameters[i];
        value_status_t status = value_validate(&context->binding->values[parameter->id - 1]);
        if (status != VALUE_OK) {
            context->error = (sqlstmt_error_t){SQLSTMT_REASON_BAD_BINDING, parameter->offset, status};
            return SQLSTMT_INVALID;
        }
    }
    return statement_clone(entry->syntax, context, out);
}

void database_cache_unpin(database_t *db, database_cache_entry_t *entry)
{
    assert(entry->pins > 0);
    entry->pins--;
    if (entry->pins == 0 && entry->is_retired) free_entry(db, entry);
}

static bool reserve_admission(database_t *db, size_t bytes)
{
    database_cache_t *cache = db->cache;
    if (bytes > DATABASE_CACHE_MAX_BYTES - sizeof(*cache)) return false;
    while (cache->stats.entries >= DATABASE_CACHE_MAX_ENTRIES ||
           bytes > DATABASE_CACHE_MAX_BYTES - cache->stats.bytes) {
        database_cache_entry_t *victim = cache->tail;
        while (victim != NULL && victim->pins != 0) victim = victim->previous;
        if (victim == NULL) return false;
        free_entry(db, victim);
        cache->stats.evictions++;
    }
    return mem_reserve(&db->resident, MEM_SCOPE_RESIDENT, bytes);
}

static bool is_cacheable(sqlstmt_kind_t kind)
{
    return kind == SQLSTMT_SELECT || kind == SQLSTMT_INSERT || kind == SQLSTMT_UPDATE || kind == SQLSTMT_DELETE;
}

static size_t parameter_occurrences(const lexer_statement_t *tokens)
{
    size_t count = 0;
    for (size_t i = 0; i < tokens->token_count; i++) {
        if (tokens->tokens[i].kind == LEXER_PARAMETER) count++;
    }
    return count;
}

static void copy_parameters(database_cache_entry_t *entry, const lexer_statement_t *tokens)
{
    size_t index = 0;
    for (size_t i = 0; i < tokens->token_count; i++) {
        const lexer_token_t *token = &tokens->tokens[i];
        if (token->kind != LEXER_PARAMETER) continue;
        entry->parameters[index++] = (cached_parameter_t){token->parameter_id, token->offset};
        if (token->parameter_id > entry->parameter_count) entry->parameter_count = token->parameter_id;
    }
}

void database_cache_admit(database_t *db, const uint8_t *sql, size_t size,
                          const lexer_statement_t *tokens, const sqlstmt_t *statement)
{
    if (!is_enabled(db) || size > DATABASE_CACHE_MAX_SQL || !is_cacheable(statement->kind)) return;
    TB_SCOPE(bench_template, TB_PARSE);
    bool is_new = db->cache == NULL;
    database_cache_t *cache = get_cache(db);
    if (cache == NULL) return;
    if (is_new) cache->stats.misses++;
    size_t count = parameter_occurrences(tokens);
    size_t fixed = sizeof(database_cache_entry_t) + size + count * sizeof(cached_parameter_t);
    size_t peak = sqlstmt_parse_peak_bytes(tokens, NULL);
    if (peak > SIZE_MAX - fixed || !reserve_admission(db, fixed + peak)) {
        cache->stats.bypasses++;
        return;
    }
    database_cache_entry_t *entry = calloc(1, sizeof(*entry) + size);
    if (entry != NULL && count > 0) entry->parameters = calloc(count, sizeof(*entry->parameters));
    statement_clone_t clone = {.limit_bytes = peak};
    bool allocated = entry != NULL && (count == 0 || entry->parameters != NULL);
    sqlstmt_status_t status = allocated ? statement_clone(statement, &clone, &entry->syntax) : SQLSTMT_NOMEM;
    if (status != SQLSTMT_OK) {
        if (entry != NULL) free(entry->parameters);
        free(entry);
        mem_release(&db->resident, MEM_SCOPE_RESIDENT, fixed + peak);
        cache->stats.bypasses++;
        return;
    }
    entry->size = size;
    entry->format = TEMPLATE_FORMAT;
    entry->bytes = fixed + clone.used_bytes;
    entry->parse_peak = peak;
    entry->parameter_occurrences = count;
    memcpy(entry->sql, sql, size);
    copy_parameters(entry, tokens);
    push_head(cache, entry);
    cache->stats.entries++;
    cache->stats.bytes += entry->bytes;
    mem_release(&db->resident, MEM_SCOPE_RESIDENT, peak - clone.used_bytes);
}

void database_cache_clear(database_t *db)
{
    if (db->cache == NULL) return;
    db->cache->stats.invalidations++;
    for (database_cache_entry_t *entry = db->cache->head; entry != NULL;) {
        database_cache_entry_t *next = entry->next;
        entry->is_retired = true;
        if (entry->pins == 0) free_entry(db, entry);
        entry = next;
    }
}

void database_cache_close(database_t *db)
{
    if (db->cache == NULL) return;
    database_cache_clear(db);
    assert(db->cache->stats.entries == 0);
    free(db->cache);
    db->cache = NULL;
    mem_release(&db->resident, MEM_SCOPE_RESIDENT, sizeof(database_cache_t));
}

bool database_cache_trim(database_t *db)
{
    if (db->cache == NULL) return false;
    size_t before = db->cache->stats.bytes;
    database_cache_clear(db);
    if (db->cache->stats.entries == 0) {
        database_cache_close(db);
        return true;
    }
    return db->cache->stats.bytes < before;
}
