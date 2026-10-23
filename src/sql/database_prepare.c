#include "database_exec.h"
#include "../benchmark.h"

static const lexer_limits_t LEXER_LIMITS = {
    (size_t)16 << 20, (size_t)16 << 20, (size_t)1 << 20, LEXER_PARAMETER_CEILING
};

static database_exec_status_t parsed_status(database_t *db, sqlstmt_status_t status,
                                            const sqlstmt_error_t *error)
{
    switch (status) {
        case SQLSTMT_OK: return DATABASE_EXEC_OK;
        case SQLSTMT_NOMEM: return DATABASE_EXEC_NOMEM;
        case SQLSTMT_SYNTAX:
            return fail_result(db, RESULT_E_SYNTAX, (int)error->reason,
                               "문법 오류 (바이트 %zu)", error->offset);
        case SQLSTMT_LIMIT:
            return fail_result(db, RESULT_E_LIMIT, (int)error->reason,
                               "한도를 넘습니다 (바이트 %zu)", error->offset);
        default:
            return fail_result(db, RESULT_E_SEMANTIC, (int)error->reason,
                               "지원하지 않거나 올바르지 않은 문장입니다 (바이트 %zu)", error->offset);
    }
}

static database_exec_status_t binding_error(database_t *db, lexer_status_t status, uint32_t count)
{
    return fail_result(db, RESULT_E_SYNTAX, (int)status,
                       "매개변수 개수가 맞지 않습니다 (SQL %u개)", (unsigned)count);
}

static bool reserve_parse(database_t *db, prepared_statement_t *p, size_t peak)
{
    if (!q_reserve(db, peak)) return false;
    p->parse_peak = peak;
    return true;
}

static database_exec_status_t prepare_cached(database_t *db, database_cache_entry_t *entry,
                                              const lexer_binding_t *binding, prepared_statement_t *p)
{
    uint32_t count = database_cache_parameter_count(entry);
    lexer_status_t checked = LEXER_OK;
    if (binding->count != count) checked = LEXER_PARAMETER_COUNT;
    else if (count > 0 && binding->values == NULL) checked = LEXER_INVALID;
    database_exec_status_t result;
    if (checked != LEXER_OK) result = binding_error(db, checked, count);
    else if (!reserve_parse(db, p, database_cache_parse_peak(entry, binding))) result = DATABASE_EXEC_NOMEM;
    else {
        statement_clone_t clone = {.binding = binding, .limit_bytes = p->parse_peak};
        TB_BEGIN(bench_parser, TB_PARSE);
        sqlstmt_status_t status = database_cache_instantiate(entry, &clone, &p->statement);
        TB_END(bench_parser);
        result = parsed_status(db, status, &clone.error);
    }
    database_cache_unpin(db, entry);
    return result;
}

static database_exec_status_t scan(database_t *db, const uint8_t *sql, size_t size,
                                   const lexer_limits_t *limits, prepared_statement_t *p)
{
    size_t peak = lexer_scan_peak_bytes(size, limits);
    if (!q_reserve(db, peak)) return DATABASE_EXEC_NOMEM;
    p->lex_peak = peak;
    lexer_error_t error;
    TB_BEGIN(bench_lexer, TB_PARSE);
    lexer_status_t status = lexer_scan(sql, size, limits, &p->lexed, &error);
    TB_END(bench_lexer);
    switch (status) {
        case LEXER_OK: return DATABASE_EXEC_OK;
        case LEXER_NOMEM: return DATABASE_EXEC_NOMEM;
        case LEXER_LIMIT: return fail_result(db, RESULT_E_LIMIT, (int)error.reason, "입력이 한도를 넘습니다");
        default: return fail_result(db, RESULT_E_SYNTAX, (int)error.reason, "어휘 오류 (바이트 %zu)", error.offset);
    }
}

static database_exec_status_t prepare_uncached(database_t *db, const lexer_binding_t *binding,
                                                prepared_statement_t *p, lexer_statement_t *tokens)
{
    if (lexed_statement_count(p->lexed) != 1) {
        return fail_result(db, RESULT_E_SYNTAX, 20, "한 번에 문장 하나만 실행할 수 있습니다");
    }
    lexer_status_t checked = lexer_check_binding(p->lexed, binding);
    if (checked != LEXER_OK) return binding_error(db, checked, lexed_parameter_count(p->lexed));
    if (lexed_statement_at(p->lexed, 0, tokens) != LEXER_OK) {
        return fail_result(db, RESULT_E_INTERNAL, 21, "문장을 읽지 못했습니다");
    }
    if (!reserve_parse(db, p, sqlstmt_parse_peak_bytes(tokens, binding))) return DATABASE_EXEC_NOMEM;
    sqlstmt_error_t error;
    TB_BEGIN(bench_parser, TB_PARSE);
    sqlstmt_status_t status = sqlstmt_parse(tokens, binding, &p->statement, &error);
    TB_END(bench_parser);
    return parsed_status(db, status, &error);
}

database_exec_status_t database_prepare(database_t *db, const database_exec_options_t *opts,
                                        const uint8_t *sql, size_t size, const lexer_binding_t *binding,
                                        prepared_statement_t *p)
{
    lexer_limits_t limits = LEXER_LIMITS;
    if (mem_ledger_enabled(&db->ledger)) {
        size_t max_sql = opts != NULL && opts->max_sql_bytes > 0 ? opts->max_sql_bytes : DATABASE_BOUNDED_SQL_BYTES;
        if (max_sql < limits.max_input_bytes) limits.max_input_bytes = max_sql;
    }
    if (size > limits.max_input_bytes) return fail_result(db, RESULT_E_LIMIT, 25, "입력이 한도를 넘습니다");
    const uint8_t *text = sql != NULL ? sql : (const uint8_t *)"";
    lexer_binding_t empty = {NULL, 0};
    const lexer_binding_t *bound = binding != NULL ? binding : &empty;
    database_cache_entry_t *entry = database_cache_find(db, text, size);
    if (entry != NULL) {
        database_exec_status_t cached = prepare_cached(db, entry, bound, p);
        if (cached != DATABASE_EXEC_NOMEM || p->parse_peak != 0) return cached;
        database_cache_trim(db);
    }
    database_exec_status_t status = scan(db, text, size, &limits, p);
    if (status != DATABASE_EXEC_OK) return status;
    lexer_statement_t tokens;
    status = prepare_uncached(db, bound, p, &tokens);
    return status;
}

void database_prepared_cache(database_t *db, const uint8_t *sql, size_t size, const prepared_statement_t *p)
{
    if (p->lexed == NULL || p->statement == NULL) return;
    lexer_statement_t tokens;
    if (lexed_statement_at(p->lexed, 0, &tokens) == LEXER_OK) {
        database_cache_admit(db, sql, size, &tokens, p->statement);
    }
}

void database_prepared_clear(database_t *db, prepared_statement_t *p)
{
    sqlstmt_free(p->statement);
    q_release(db, p->parse_peak);
    lexer_free(p->lexed);
    q_release(db, p->lex_peak);
}
