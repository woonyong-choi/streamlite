/*
 * database_update.c — RETURNING과 UPDATE·DELETE.
 */
#include "database_exec.h"

typedef struct {
    heap_locator_t locator;
    value_t *cells;
} match_t;

typedef struct {
    match_t *items;
    size_t count;
    size_t cap;
    size_t columns;
} matches_t;

/* 탐침으로 후보 행을 찾고 전체 조건을 거른다 */
typedef struct {
    database_t *db;
    heap_t *heap;
    const where_t *where;
    const probe_t *probe;
    value_t *row;
    matches_t *matches;
    bool keep_values;
    database_exec_status_t status;
} secondary_match_t;

typedef struct {
    database_t *db;
    heap_t *heap;
    const where_t *where;
    value_t *row;
    matches_t *matches;
    bool keep_values;
    database_exec_status_t status;
} range_match_t;

typedef struct {
    const value_view_t *view;
} key_ref_t;

/* 기본 키 갱신에서 일치하지 않은 행의 키를 모은다(색인 없는 표) */
typedef struct {
    database_t *db;
    size_t pk;
    value_t *keys;
    size_t count;
    size_t cap;
} other_keys_t;

/* ── RETURNING ── */

database_exec_status_t returning_open(database_t *db, const sqlstmt_returning_t *spec,
                                             const catalog_table_view_t *view, const sqlstmt_name_t *table,
                                             returning_t **out)
{
    *out = NULL;
    if (spec->count == 0) return DATABASE_EXEC_OK;
    returning_t *ret = q_calloc(db, 1, sizeof(*ret));
    if (ret == NULL) return DATABASE_EXEC_NOMEM;
    ret->ctx.db = db;
    ret->source = (source_ref_t){view, table, NULL, 0, false};
    ret->resolver = (resolver_t){&ret->source, 1};
    compile_ctx_t cc = {resolve_column, NULL, &ret->resolver, &ret->resolver};
    database_exec_status_t status = plan_outputs(db, spec->items, spec->count, &cc, &ret->ctx);
    if (status == DATABASE_EXEC_OK) status = select_prepare(&ret->ctx);
    if (status != DATABASE_EXEC_OK) {
        returning_close(db, ret);
        return status;
    }
    *out = ret;
    return DATABASE_EXEC_OK;
}

database_exec_status_t returning_row(returning_t *ret, const value_t *row)
{
    return ret != NULL ? select_emit(&ret->ctx, row) : DATABASE_EXEC_OK;
}

database_exec_status_t returning_finish(database_t *db, returning_t *ret, const char *tag, uint64_t affected,
                                               result_t **success)
{
    if (ret == NULL) return open_command(db, tag, affected, success) == RESULT_OK ? DATABASE_EXEC_OK : DATABASE_EXEC_NOMEM;
    result_status_t finished = result_builder_finish(&ret->ctx.builder, success);
    if (finished == RESULT_NOMEM) return DATABASE_EXEC_NOMEM;
    return finished == RESULT_OK ? DATABASE_EXEC_OK : fail_result(db, RESULT_E_LIMIT, 17, "결과를 만들지 못했습니다");
}

void returning_close(database_t *db, returning_t *ret)
{
    if (ret == NULL) return;
    select_ctx_free(&ret->ctx);
    q_free(db, ret, sizeof(*ret));
}

/* ── UPDATE · DELETE ── */

static void matches_free(database_t *db, matches_t *matches)
{
    for (size_t i = 0; i < matches->count; i++) {
        if (matches->items[i].cells != NULL) {
            owned_clear(db, matches->items[i].cells, matches->columns);
            q_free(db, matches->items[i].cells, matches->columns * sizeof(value_t));
        }
    }
    q_free(db, matches->items, matches->cap * sizeof(match_t));
    memset(matches, 0, sizeof(*matches));
}

/* 일치한 행(locator와 값)을 모은다. keep_values가 false면 값은 보관하지 않는다. */
static bool matches_add(database_t *db, matches_t *matches, heap_locator_t locator, const value_t *row,
                        bool keep_values)
{
    if (matches->count == matches->cap) {
        /* 새 배열을 먼저 reserve하고 복사한 뒤 옛 배열을 돌려준다. 순간 최대치는 old+new다 */
        size_t cap = matches->cap == 0 ? 64 : matches->cap * 2;
        if (cap > SIZE_MAX / sizeof(match_t) || !q_reserve(db, cap * sizeof(match_t))) return false;
        match_t *grown = realloc(matches->items, cap * sizeof(*grown));
        if (grown == NULL) {
            q_release(db, cap * sizeof(match_t));
            return false;
        }
        q_release(db, matches->cap * sizeof(match_t));
        matches->items = grown;
        matches->cap = cap;
    }
    match_t *slot = &matches->items[matches->count];
    slot->locator = locator;
    slot->cells = NULL;
    if (keep_values) {
        slot->cells = q_calloc(db, matches->columns, sizeof(*slot->cells));
        if (slot->cells == NULL) return false;
        for (size_t i = 0; i < matches->columns; i++) {
            if (q_clone(db, &row[i].view, &slot->cells[i]) != VALUE_OK) {
                owned_clear(db, slot->cells, matches->columns);
                q_free(db, slot->cells, matches->columns * sizeof(value_t));
                slot->cells = NULL;
                return false;
            }
        }
    }
    matches->count++;
    return true;
}

static bool collect_secondary_match(void *context, heap_locator_t locator)
{
    secondary_match_t *scan = context;
    if (cancel_requested(scan->db)) {
        scan->status = cancelled_result(scan->db);
        return false;
    }
    heap_status_t got = heap_get(scan->heap, locator, scan->row);
    if (got != HEAP_OK) {
        scan->status = heap_failure(scan->db, got, "index lookup");
        return false;
    }
    bool matched = false;
    if (!probe_holds(scan->probe, scan->row)) {
        scan->status = heap_failure(scan->db, HEAP_CORRUPT, "index reference");
    } else {
        scan->status = where_matches(scan->db, scan->where, scan->row, &matched);
        if (scan->status == DATABASE_EXEC_OK && matched &&
            !matches_add(scan->db, scan->matches, locator, scan->row, scan->keep_values)) {
            scan->status = DATABASE_EXEC_NOMEM;
        }
    }
    heap_release_row(scan->heap, scan->row);
    return scan->status == DATABASE_EXEC_OK;
}

static bool collect_range_match(void *context, heap_locator_t locator)
{
    range_match_t *scan = context;
    if (cancel_requested(scan->db)) {
        scan->status = cancelled_result(scan->db);
        return false;
    }
    heap_status_t got = heap_get(scan->heap, locator, scan->row);
    if (got != HEAP_OK) {
        scan->status = heap_failure(scan->db, got, "index range");
        return false;
    }
    bool matched = false;
    scan->status = where_matches(scan->db, scan->where, scan->row, &matched);
    if (scan->status == DATABASE_EXEC_OK && matched &&
        !matches_add(scan->db, scan->matches, locator, scan->row, scan->keep_values)) {
        scan->status = DATABASE_EXEC_NOMEM;
    }
    heap_release_row(scan->heap, scan->row);
    return scan->status == DATABASE_EXEC_OK;
}

/*
 * WHERE에 맞는 행을 모은다. 경로는 scan_path_choose가 고르며 보조 색인 구간은 쓰지 않는다(force_scan이면 전체 스캔).
 * 모은 뒤에 고치므로 갱신이 경로를 다시 지나지 않는다. scan_other가 NULL이 아니면 훑는 동안 맞지 않은 행마다
 * 부른다(기본 키 갱신의 중복 검사용).
 */
static database_exec_status_t collect_matches(database_t *db, const catalog_table_view_t *view, catalog_table_id_t id,
                                              heap_t *heap, const where_t *where, value_t *row, bool keep_values,
                                              bool force_scan, matches_t *matches,
                                              database_exec_status_t (*scan_other)(void *, const value_t *),
                                              void *other_ctx)
{
    size_t n = view->column_count;
    if (where->never && scan_other == NULL) return DATABASE_EXEC_OK;
    scan_path_t path = {SCAN_PATH_FULL, SIZE_MAX, {0}};
    if (!force_scan) path = scan_path_choose(db, view, id, heap, where, false);
    if (path.kind == SCAN_PATH_KEY) {
        bool is_hit;
        heap_locator_t at;
        database_exec_status_t status = locate_key(db, heap, &where->probes[path.at].literal.view, &is_hit, &at);
        if (status != DATABASE_EXEC_OK || !is_hit) return status;
        heap_status_t got = heap_get(heap, at, row);
        if (got != HEAP_OK) return heap_failure(db, got, "lookup");
        bool matched = false;
        status = where_matches(db, where, row, &matched);
        if (status == DATABASE_EXEC_OK && matched && !matches_add(db, matches, at, row, keep_values)) {
            status = DATABASE_EXEC_NOMEM;
        }
        heap_release_row(heap, row);
        return status;
    }
    if (path.kind == SCAN_PATH_SECONDARY) {
        secondary_match_t scan = {db, heap, where, &where->probes[path.at], row, matches, keep_values,
                                  DATABASE_EXEC_OK};
        heap_status_t visited = heap_secondary_visit_equal(heap, path.index, &where->probes[path.at].literal.view,
                                                           collect_secondary_match, &scan);
        if (scan.status != DATABASE_EXEC_OK) return scan.status;
        return visited == HEAP_OK ? DATABASE_EXEC_OK : heap_failure(db, visited, "index lookup");
    }
    if (path.kind == SCAN_PATH_KEY_RANGE) {
        range_match_t scan = {db, heap, where, row, matches, keep_values, DATABASE_EXEC_OK};
        bool stopped = false;
        heap_status_t visited = heap_visit_key_range(heap, where->ranges[path.at].low, where->ranges[path.at].high,
                                                     collect_range_match, &scan, &stopped);
        if (scan.status != DATABASE_EXEC_OK) return scan.status;
        return visited == HEAP_OK ? DATABASE_EXEC_OK : heap_failure(db, visited, "index range");
    }
    if (path.kind == SCAN_PATH_TEXT_RANGE) {
        range_match_t scan = {db, heap, where, row, matches, keep_values, DATABASE_EXEC_OK};
        bool stopped = false;
        heap_status_t visited = visit_text_range(heap, &where->text_ranges[path.at], collect_range_match, &scan, &stopped);
        if (scan.status != DATABASE_EXEC_OK) return scan.status;
        return visited == HEAP_OK ? DATABASE_EXEC_OK : heap_failure(db, visited, "index range");
    }
    heap_cursor_t *cursor = NULL;
    heap_status_t made = heap_cursor_open(heap, &cursor);
    if (made != HEAP_OK) return heap_failure(db, made, "cursor");
    database_exec_status_t status = DATABASE_EXEC_OK;
    for (;;) {
        if (cancel_requested(db)) {
            status = cancelled_result(db);
            break;
        }
        heap_locator_t at;
        heap_status_t next = heap_cursor_next(cursor, row, &at);
        if (next == HEAP_DONE) break;
        if (next != HEAP_OK) {
            status = heap_failure(db, next, "scan");
            break;
        }
        bool matched = false;
        status = where_matches(db, where, row, &matched);
        if (status == DATABASE_EXEC_OK && matched) {
            if (!matches_add(db, matches, at, row, keep_values)) status = DATABASE_EXEC_NOMEM;
        } else if (status == DATABASE_EXEC_OK && scan_other != NULL) {
            status = scan_other(other_ctx, row);
        }
        clear_cells(row, n);
        if (status != DATABASE_EXEC_OK) break;
    }
    heap_cursor_close(cursor);
    return status;
}

static int compare_keys(const key_ref_t *x, const key_ref_t *y)
{
    int order = 0;
    value_compare(x->view, y->view, &order);
    return order;
}

/* 제자리 힙 정렬. libc qsort는 내부에서 임시 메모리를 쓸 수 있어 계상 밖의 할당이 생기므로 쓰지 않는다. */
static void sift_down(key_ref_t *keys, size_t root, size_t count)
{
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= count) return;
        if (child + 1 < count && compare_keys(&keys[child], &keys[child + 1]) < 0) child++;
        if (compare_keys(&keys[root], &keys[child]) >= 0) return;
        key_ref_t swap = keys[root];
        keys[root] = keys[child];
        keys[child] = swap;
        root = child;
    }
}

static void sort_keys(key_ref_t *keys, size_t count)
{
    if (count < 2) return;
    for (size_t i = count / 2; i > 0; i--) sift_down(keys, i - 1, count);
    for (size_t end = count - 1; end > 0; end--) {
        key_ref_t swap = keys[0];
        keys[0] = keys[end];
        keys[end] = swap;
        sift_down(keys, 0, end);
    }
}

static bool has_duplicate(key_ref_t *keys, size_t count)
{
    sort_keys(keys, count);
    for (size_t i = 1; i < count; i++) {
        if (values_equal(keys[i - 1].view, keys[i].view)) return true;
    }
    return false;
}

static database_exec_status_t keep_other_key(void *context, const value_t *row)
{
    other_keys_t *other = context;
    if (row[other->pk].view.type == VALUE_NULL) return DATABASE_EXEC_OK;
    if (other->count == other->cap) {
        size_t cap = other->cap == 0 ? 64 : other->cap * 2;
        if (cap > SIZE_MAX / sizeof(value_t) || !q_reserve(other->db, cap * sizeof(value_t))) return DATABASE_EXEC_NOMEM;
        value_t *grown = realloc(other->keys, cap * sizeof(*grown));
        if (grown == NULL) {
            q_release(other->db, cap * sizeof(value_t));
            return DATABASE_EXEC_NOMEM;
        }
        q_release(other->db, other->cap * sizeof(value_t));
        other->keys = grown;
        other->cap = cap;
    }
    memset(&other->keys[other->count], 0, sizeof(value_t));
    if (q_clone(other->db, &row[other->pk].view, &other->keys[other->count]) != VALUE_OK) return DATABASE_EXEC_NOMEM;
    other->count++;
    return DATABASE_EXEC_OK;
}

static int compare_locators(heap_locator_t a, heap_locator_t b)
{
    if (a.directory_index != b.directory_index) return a.directory_index < b.directory_index ? -1 : 1;
    if (a.slot != b.slot) return a.slot < b.slot ? -1 : 1;
    return 0;
}

static void sift_order(size_t *order, size_t root, size_t count, const match_t *items)
{
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= count) return;
        if (child + 1 < count && compare_locators(items[order[child]].locator, items[order[child + 1]].locator) < 0) child++;
        if (compare_locators(items[order[root]].locator, items[order[child]].locator) >= 0) return;
        size_t swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

/* 일치한 행 번호를 locator 순으로 늘어놓는다(제자리 힙 정렬. sort_keys와 같은 이유로 qsort를 쓰지 않는다) */
static void order_by_locator(size_t *order, size_t count, const match_t *items)
{
    for (size_t i = 0; i < count; i++) order[i] = i;
    if (count < 2) return;
    for (size_t i = count / 2; i > 0; i--) sift_order(order, i - 1, count, items);
    for (size_t end = count - 1; end > 0; end--) {
        size_t swap = order[0];
        order[0] = order[end];
        order[end] = swap;
        sift_order(order, 0, end, items);
    }
}

/* locator가 일치한 행의 것이면 그 번호, 아니면 SIZE_MAX. order는 order_by_locator가 만든 순서다 */
static size_t match_at(const size_t *order, size_t count, const match_t *items, heap_locator_t at)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int cmp = compare_locators(items[order[mid]].locator, at);
        if (cmp == 0) return order[mid];
        if (cmp < 0) low = mid + 1;
        else high = mid;
    }
    return SIZE_MAX;
}

database_exec_status_t exec_update(database_t *db, const sqlstmt_update_t *update, result_t **out)
{
    database_exec_status_t status = DATABASE_EXEC_OK;
    catalog_table_id_t id;
    catalog_table_view_t view;
    if (!lookup_table(db, &update->table, &id, &view, &status)) return status;
    size_t n = view.column_count;
    source_ref_t source = {&view, &update->table, NULL, 0, false};
    resolver_t resolver = {&source, 1};
    returning_t *ret = NULL;
    status = returning_open(db, &update->returning, &view, &update->table, &ret);
    if (status != DATABASE_EXEC_OK) return status;
    size_t assign_count = update->assignment_count;
    size_t columns_bytes = (assign_count > 0 ? assign_count : 1) * sizeof(size_t);
    size_t progs_bytes = (assign_count > 0 ? assign_count : 1) * sizeof(sqleval_prog_t *);
    size_t row_bytes = n * sizeof(value_t), views_bytes = n * sizeof(value_view_t);
    size_t *targets = q_malloc(db, columns_bytes);
    sqleval_prog_t **progs = q_calloc(db, assign_count > 0 ? assign_count : 1, sizeof(*progs));
    value_t *row = q_calloc(db, n, sizeof(*row));
    value_view_t *views = q_malloc(db, views_bytes);
    value_t *fresh = NULL;      /* 일치한 행마다 assign_count개의 새 값 */
    size_t fresh_bytes = 0;
    key_ref_t *keys = NULL;
    size_t keys_bytes = 0;
    size_t *by_locator = NULL;  /* 일치한 행 번호를 locator 순으로(색인 있는 표의 기본 키 갱신) */
    bool *vacates = NULL;       /* 일치한 다른 행이 이 행의 옛 키를 새 키로 가져간다 */
    size_t by_locator_bytes = 0, vacates_bytes = 0;
    other_keys_t other = {db, view.primary_key_column, NULL, 0, 0};
    heap_t *heap = NULL;
    heap_batch_t *batch = NULL;
    work_t work = {NULL, 0};
    result_t *success = NULL;
    matches_t matches = {NULL, 0, 0, n};
    where_t where;
    memset(&where, 0, sizeof(where));
    if (targets == NULL || progs == NULL || row == NULL || views == NULL) {
        status = DATABASE_EXEC_NOMEM;
        goto done;
    }
    size_t pk = view.primary_key_column;
    bool pk_assigned = false;
    for (size_t i = 0; i < assign_count; i++) {
        size_t column = find_column(&view, &update->assignments[i].column);
        if (column == SIZE_MAX) {
            status = unknown_column(db, &update->assignments[i].column);
            goto done;
        }
        for (size_t j = 0; j < i; j++) {
            if (targets[j] == column) {
                status = fail_result(db, RESULT_E_SEMANTIC, 9, "컬럼이 두 번 나옵니다: %.*s",
                                     name_len(update->assignments[i].column.size),
                                     (const char *)update->assignments[i].column.bytes);
                goto done;
            }
        }
        targets[i] = column;
        if (!compile_expr(db, update->assignments[i].value, &resolver, &progs[i], &status)) goto done;
        if (column == pk) pk_assigned = true;
    }
    if (!compile_where(db, &resolver, update->where, &where, &status)) goto done;

    heap_status_t opened = open_heap(db, &view, id, &heap);
    if (opened != HEAP_OK) {
        status = heap_failure(db, opened, "open");
        goto done;
    }
    bool is_indexed = heap_is_indexed(heap);
    /* 색인 없는 표의 기본 키 갱신은 맞지 않은 행의 키도 봐야 해서 전체를 훑는다 */
    bool scan_all = pk_assigned && !is_indexed;
    status = collect_matches(db, &view, id, heap, &where, row, true, scan_all, &matches,
                             scan_all ? keep_other_key : NULL, &other);
    if (status != DATABASE_EXEC_OK) goto done;
    if (matches.count == 0) {
        status = returning_finish(db, ret, "UPDATE", 0, &success);
        if (status != DATABASE_EXEC_OK) goto done;
        *out = success;
        success = NULL;
        goto done;
    }

    /* 새 값: 일치한 행마다 SET 식을 그 행의 옛 값으로 계산하고 컬럼 형식으로 정확히 바꾼다 */
    if (assign_count > 0 && matches.count > SIZE_MAX / assign_count / sizeof(value_t)) {
        status = DATABASE_EXEC_NOMEM;
        goto done;
    }
    fresh_bytes = matches.count * (assign_count > 0 ? assign_count : 1) * sizeof(value_t);
    fresh = q_calloc(db, matches.count * (assign_count > 0 ? assign_count : 1), sizeof(value_t));
    if (fresh == NULL) {
        status = DATABASE_EXEC_NOMEM;
        goto done;
    }
    for (size_t m = 0; m < matches.count; m++) {
        for (size_t i = 0; i < assign_count; i++) {
            value_t computed = {0};
            const sqlexpr_t *expr = update->assignments[i].value;
            size_t literal_bytes = expr->kind == SQLEXPR_LITERAL ? value_bytes(&expr->value) : 0;
            if (!q_reserve(db, literal_bytes)) {
                status = DATABASE_EXEC_NOMEM;
                goto done;
            }
            sqleval_error_t error = {SQLEVAL_OK, 0, NULL};
            if (sqleval_run(progs[i], matches.items[m].cells, &computed, &error) != SQLEVAL_OK) {
                q_release(db, literal_bytes);
                status = eval_failure(db, &error);
                goto done;
            }
            bool cast = cast_for_column(db, &computed.view, &view.columns[targets[i]], &fresh[m * assign_count + i],
                                        &status);
            value_clear(&computed);
            q_release(db, literal_bytes);
            if (!cast) goto done;
            /* 기본 키는 NOT NULL을 적지 않아도(AUTOINCREMENT) NULL로 바꿀 수 없다. NULL이 번호를 받는 것은 INSERT뿐이다 */
            if ((view.columns[targets[i]].not_null || targets[i] == pk) &&
                fresh[m * assign_count + i].view.type == VALUE_NULL) {
                status = not_null_violation(db, &view.columns[targets[i]]);
                goto done;
            }
        }
    }

    /*
     * 기본 키를 바꾸는 갱신은 적용하기 전에 판정한다. 끝난 결과에서 키가 유일하면 허용이다: 새 키가 일치하지 않은
     * 행의 키와 같거나 새 키끼리 같으면 여기서 거절하고, 일치한 다른 행의 옛 키로 옮기는 것(id = id + 1, 맞바꾸기)은
     * 허용한다. batch는 같은 키가 잠시 두 행에 겹치는 갱신을 받지 않으므로(heap_store.h) 키를 내주는 행을 표시해
     * 두고 적용할 때 먼저 지운 뒤 새 키로 다시 넣는다. 그래서 허용한 것은 그대로 적용된다.
     */
    if (pk_assigned) {
        size_t pk_slot = 0;
        while (targets[pk_slot] != pk) pk_slot++;
        size_t total = matches.count + other.count;
        keys_bytes = total * sizeof(*keys);
        keys = q_malloc(db, keys_bytes);
        if (keys == NULL) {
            status = DATABASE_EXEC_NOMEM;
            goto done;
        }
        if (is_indexed) {
            by_locator_bytes = matches.count * sizeof(*by_locator);
            vacates_bytes = matches.count * sizeof(*vacates);
            by_locator = q_malloc(db, by_locator_bytes);
            vacates = q_calloc(db, matches.count, sizeof(*vacates));
            if (by_locator == NULL || vacates == NULL) {
                status = DATABASE_EXEC_NOMEM;
                goto done;
            }
            order_by_locator(by_locator, matches.count, matches.items);
        }
        size_t key_count = 0;
        for (size_t m = 0; m < matches.count; m++) {
            const value_view_t *key = &fresh[m * assign_count + pk_slot].view;
            keys[key_count++].view = key;
            if (!is_indexed) continue;
            /* 색인에서 같은 키를 가진 행이 있으면 그 행도 이번에 키가 바뀌어야 한다 */
            heap_locator_t at;
            heap_status_t found = key->type == VALUE_TEXT ? heap_find_text_key(heap, key->bytes, key->size_bytes, &at)
                                                          : heap_find_key(heap, key->integer, &at);
            if (found != HEAP_OK && found != HEAP_NOT_FOUND) {
                status = heap_failure(db, found, "lookup");
                goto done;
            }
            if (found == HEAP_OK) {
                size_t holder = match_at(by_locator, matches.count, matches.items, at);
                if (holder == SIZE_MAX) {
                    status = duplicate_key(db, &view);
                    goto done;
                }
                if (holder != m) vacates[holder] = true;
            }
        }
        for (size_t i = 0; i < other.count; i++) keys[key_count++].view = &other.keys[i].view;
        if (has_duplicate(keys, key_count)) {
            status = duplicate_key(db, &view);
            goto done;
        }
    }

    if (!work_clone(db, &work)) {
        status = DATABASE_EXEC_NOMEM;
        goto done;
    }
    heap_status_t began = heap_batch_begin(heap, &batch);
    if (began == HEAP_OK) began = configure_secondary(batch, work.cat, id);
    if (began != HEAP_OK) {
        status = heap_failure(db, began, "batch");
        goto done;
    }
    for (size_t m = 0; m < matches.count; m++) {
        for (size_t i = 0; i < n; i++) views[i] = matches.items[m].cells[i].view;
        heap_status_t removed = heap_batch_secondary_remove(batch, views, n, matches.items[m].locator);
        if (removed != HEAP_OK) {
            status = heap_failure(db, removed, "index");
            goto done;
        }
    }
    /* 키를 내주는 행을 먼저 지운다. 그 키를 받는 행이 어느 순서로 와도 색인에서 겹치지 않는다 */
    for (size_t m = 0; vacates != NULL && m < matches.count; m++) {
        if (!vacates[m]) continue;
        heap_status_t removed = heap_batch_delete(batch, matches.items[m].locator);
        if (removed != HEAP_OK) {
            status = heap_failure(db, removed, "update");
            goto done;
        }
    }
    for (size_t m = 0; m < matches.count; m++) {
        if (cancel_requested(db)) {
            status = cancelled_result(db);
            goto done;
        }
        for (size_t i = 0; i < n; i++) views[i] = matches.items[m].cells[i].view;
        for (size_t i = 0; i < assign_count; i++) views[targets[i]] = fresh[m * assign_count + i].view;
        if (!row_fits_limit(db, views, n, &status)) goto done;
        heap_locator_t placed;
        heap_status_t changed = vacates != NULL && vacates[m]
                                    ? heap_batch_insert(batch, views, n, &placed)
                                    : heap_batch_update(batch, matches.items[m].locator, views, n, &placed);
        if (changed != HEAP_OK) {
            status = heap_failure(db, changed, "update");
            goto done;
        }
        changed = heap_batch_secondary_insert(batch, views, n, placed);
        if (changed != HEAP_OK) {
            status = index_failure(db, changed, "update");
            goto done;
        }
        if (ret != NULL) {
            /* 바뀐 행: 옛 행 값과 새 값을 빌려 쓴다 */
            for (size_t i = 0; i < n; i++) row[i] = (value_t){views[i], false};
            status = returning_row(ret, row);
            memset(row, 0, n * sizeof(*row));
            if (status != DATABASE_EXEC_OK) goto done;
        }
    }
    status = returning_finish(db, ret, "UPDATE", matches.count, &success);
    if (status != DATABASE_EXEC_OK) goto done;
    status = publish(db, &work, id, batch, NULL, 0, success, out);
    success = NULL;
    batch = NULL;
done:
    if (batch != NULL) heap_batch_abort(batch);
    heap_batch_free(batch);
    heap_close(heap);
    work_close(db, &work);
    result_release(&success);
    if (fresh != NULL) owned_clear(db, fresh, matches.count * assign_count);
    q_free(db, fresh, fresh_bytes);
    matches_free(db, &matches);
    if (other.keys != NULL) owned_clear(db, other.keys, other.count);
    q_free(db, other.keys, other.cap * sizeof(value_t));
    q_free(db, keys, keys_bytes);
    q_free(db, by_locator, by_locator_bytes);
    q_free(db, vacates, vacates_bytes);
    if (row != NULL) clear_cells(row, n);
    q_free(db, row, row_bytes);
    q_free(db, views, views_bytes);
    for (size_t i = 0; progs != NULL && i < assign_count; i++) sqleval_free(progs[i]);
    q_free(db, progs, progs_bytes);
    q_free(db, targets, columns_bytes);
    where_clear(db, &where);
    returning_close(db, ret);
    return status;
}

database_exec_status_t exec_delete(database_t *db, const sqlstmt_delete_t *del, result_t **out)
{
    database_exec_status_t status = DATABASE_EXEC_OK;
    catalog_table_id_t id;
    catalog_table_view_t view;
    if (!lookup_table(db, &del->table, &id, &view, &status)) return status;
    size_t n = view.column_count;
    source_ref_t source = {&view, &del->table, NULL, 0, false};
    resolver_t resolver = {&source, 1};
    returning_t *ret = NULL;
    status = returning_open(db, &del->returning, &view, &del->table, &ret);
    if (status != DATABASE_EXEC_OK) return status;
    size_t row_bytes = n * sizeof(value_t);
    value_t *row = q_calloc(db, n, sizeof(*row));
    /* 보조 색인을 지우거나 RETURNING으로 돌려줄 때는 지울 행의 값을 쥔다 */
    bool keep_values = catalog_index_count(base_catalog(db), id) > 0 || ret != NULL;
    size_t views_bytes = keep_values ? n * sizeof(value_view_t) : 0;
    value_view_t *views = keep_values ? q_malloc(db, views_bytes) : NULL;
    heap_t *heap = NULL;
    heap_batch_t *batch = NULL;
    work_t work = {NULL, 0};
    result_t *success = NULL;
    matches_t matches = {NULL, 0, 0, n};
    where_t where;
    memset(&where, 0, sizeof(where));
    if (row == NULL || (keep_values && views == NULL)) {
        status = DATABASE_EXEC_NOMEM;
        goto done;
    }
    if (!compile_where(db, &resolver, del->where, &where, &status)) goto done;
    heap_status_t opened = open_heap(db, &view, id, &heap);
    if (opened != HEAP_OK) {
        status = heap_failure(db, opened, "open");
        goto done;
    }
    status = collect_matches(db, &view, id, heap, &where, row, keep_values, false, &matches, NULL, NULL);
    if (status != DATABASE_EXEC_OK) goto done;
    if (matches.count == 0) {
        status = returning_finish(db, ret, "DELETE", 0, &success);
        if (status != DATABASE_EXEC_OK) goto done;
        *out = success;
        success = NULL;
        goto done;
    }
    if (!work_clone(db, &work)) {
        status = DATABASE_EXEC_NOMEM;
        goto done;
    }
    heap_status_t began = heap_batch_begin(heap, &batch);
    if (began == HEAP_OK) began = configure_secondary(batch, work.cat, id);
    if (began != HEAP_OK) {
        status = heap_failure(db, began, "batch");
        goto done;
    }
    for (size_t m = 0; m < matches.count; m++) {
        if (cancel_requested(db)) {
            status = cancelled_result(db);
            goto done;
        }
        if (keep_values) {
            for (size_t i = 0; i < n; i++) views[i] = matches.items[m].cells[i].view;
            heap_status_t unindexed = heap_batch_secondary_remove(batch, views, n, matches.items[m].locator);
            if (unindexed != HEAP_OK) {
                status = heap_failure(db, unindexed, "index");
                goto done;
            }
        }
        heap_status_t removed = heap_batch_delete(batch, matches.items[m].locator);
        if (removed != HEAP_OK) {
            status = heap_failure(db, removed, "delete");
            goto done;
        }
        status = returning_row(ret, matches.items[m].cells);
        if (status != DATABASE_EXEC_OK) goto done;
    }
    if (catalog_set_row_count(work.cat, id, view.row_count - matches.count) != CATALOG_OK) {
        status = fail_result(db, RESULT_E_INTERNAL, 18, "행 수를 갱신하지 못했습니다");
        goto done;
    }
    status = returning_finish(db, ret, "DELETE", matches.count, &success);
    if (status != DATABASE_EXEC_OK) goto done;
    status = publish(db, &work, id, batch, NULL, 0, success, out);
    success = NULL;
    batch = NULL;
done:
    if (batch != NULL) heap_batch_abort(batch);
    heap_batch_free(batch);
    heap_close(heap);
    work_close(db, &work);
    result_release(&success);
    matches_free(db, &matches);
    if (row != NULL) clear_cells(row, n);
    q_free(db, row, row_bytes);
    q_free(db, views, views_bytes);
    where_clear(db, &where);
    returning_close(db, ret);
    return status;
}
