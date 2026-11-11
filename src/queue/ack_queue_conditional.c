#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ack_queue_internal.h"

#define CONDITIONAL_SQL_MAX 2048u
#define ELIGIBLE "((state = 0 AND ready_at <= ?) OR " \
                 "(state = 1 AND lease_until <= ? AND (? = 0 OR attempts < ?)))"
#define CLAIM_LIMITS " AND fence < 9223372036854775807 AND attempts < 9223372036854775807"
#define CLAIM_SET " SET state = 1, lease_until = ?, fence = fence + 1, attempts = attempts + 1 WHERE id = "
#define CLAIM_RETURN " RETURNING id, fence, attempts, payload"
/* due 형식: 새 시도 횟수가 max_attempts에 닿으면 마지막 시도 구간, 아니면 lease_until(ack_queue_internal.h) */
#define CLAIM_SET_DUE " SET state = 1, lease_until = ?, fence = fence + 1, attempts = attempts + 1, " \
                      "due = CASE WHEN ? > 0 AND attempts + 1 >= ? THEN ? + id ELSE ? END WHERE id = "

static ackq_status_t copy_claim(const result_row_t *row, int64_t until, ackq_claim_t *item)
{
    if (row->cell_count != 4) return ACKQ_INTERNAL;
    for (size_t i = 0; i < 3; i++) {
        if (row->cells[i].type != VALUE_INTEGER) return ACKQ_INTERNAL;
    }
    const value_view_t *payload = &row->cells[3];
    if (payload->type != VALUE_BLOB && payload->type != VALUE_TEXT) return ACKQ_INTERNAL;
    if (payload->size_bytes == SIZE_MAX) return ACKQ_LIMIT;
    uint8_t *bytes = NULL;
    if (payload->size_bytes > 0) {
        bytes = malloc(payload->size_bytes + 1);
        if (bytes == NULL) return ACKQ_NOMEM;
        memcpy(bytes, payload->bytes, payload->size_bytes);
        bytes[payload->size_bytes] = 0;
    }
    *item = (ackq_claim_t){(uint64_t)row->cells[0].integer, (uint64_t)row->cells[1].integer,
                          (uint32_t)row->cells[2].integer, (uint64_t)until, bytes, payload->size_bytes};
    return ACKQ_OK;
}

static ackq_status_t take_claim(result_t *result, int64_t until, ackq_claim_t *item, bool *matched)
{
    ackq_status_t status = ACKQ_OK;
    result_row_t row;
    result_next_t next;
    size_t rows = 0;
    while ((next = result_next(result, &row)) == RESULT_NEXT_ROW) {
        rows++;
        if (rows == 1) status = copy_claim(&row, until, item);
        else status = ACKQ_INTERNAL;
    }
    ackq_status_t finished = ackq_finish_rows(next, result);
    if (status == ACKQ_OK) status = finished;
    if (status != ACKQ_OK) ackq_claims_release(item, 1);
    *matched = status == ACKQ_OK && rows == 1;
    return status;
}

static int claim_sql(const ackq_t *queue, bool by_id, char sql[CONDITIONAL_SQL_MAX])
{
    if (queue->due_column) {
        if (by_id) {
            return snprintf(sql, CONDITIONAL_SQL_MAX, "UPDATE %s" CLAIM_SET_DUE "? AND " ELIGIBLE CLAIM_LIMITS CLAIM_RETURN,
                            queue->table);
        }
        return snprintf(sql, CONDITIONAL_SQL_MAX,
                        "UPDATE %s" CLAIM_SET_DUE
                        "(SELECT COALESCE(MIN(id), -1) FROM (SELECT id FROM %s WHERE due >= 0 AND due <= ? "
                        "AND ((state = 0 AND ready_at <= ?) OR (state = 1 AND lease_until <= ?)) "
                        "AND (state = 0 OR ? = 0 OR attempts < ?) ORDER BY due LIMIT 1) candidates) AND "
                        ELIGIBLE CLAIM_LIMITS CLAIM_RETURN,
                        queue->table, queue->table);
    }
    if (by_id) {
        return snprintf(sql, CONDITIONAL_SQL_MAX, "UPDATE %s" CLAIM_SET "? AND " ELIGIBLE CLAIM_LIMITS CLAIM_RETURN,
                        queue->table);
    }
    return snprintf(sql, CONDITIONAL_SQL_MAX,
                    "UPDATE %s" CLAIM_SET
                    "(SELECT COALESCE(MIN(id), -1) FROM ("
                    "SELECT id FROM (SELECT id FROM %s WHERE state = 0 AND ready_at <= ? ORDER BY id LIMIT 1) r "
                    "UNION ALL SELECT id FROM (SELECT id FROM %s WHERE state = 1 AND lease_until <= ? "
                    "AND (? = 0 OR attempts < ?) ORDER BY id LIMIT 1) l) candidates) AND "
                    ELIGIBLE CLAIM_LIMITS CLAIM_RETURN,
                    queue->table, queue->table, queue->table);
}

ackq_status_t ackq_conditional_claim(ackq_t *queue, ackq_scope_t *scope, const ackq_claim_request_t *request,
                                      ackq_claim_t *item, bool *matched)
{
    *matched = false;
    *item = (ackq_claim_t){0};
    int64_t id;
    if (!ackq_to_i64(request->id, &id)) return ACKQ_NOT_FOUND;
    char sql[CONDITIONAL_SQL_MAX];
    int length = claim_sql(queue, request->id != 0, sql);
    if (length < 0 || (size_t)length >= sizeof(sql)) return ACKQ_LIMIT;
    value_view_t params[16];
    size_t count = 0;
    params[count++] = ackq_int(request->lease_until);
    if (queue->due_column) {
        params[count++] = ackq_int(queue->max_attempts);
        params[count++] = ackq_int(queue->max_attempts);
        params[count++] = ackq_int(ACKQ_DUE_LAST);
        params[count++] = ackq_int(request->lease_until);
    }
    if (request->id != 0) params[count++] = ackq_int(id);
    else if (queue->due_column) {
        /* due가 상태와 어긋난 행(이전 판 라이브러리의 쓰기)은 후보에서 뺀다. 아니면 그 행이 머리를 막는다 */
        params[count++] = ackq_int(request->now);
        params[count++] = ackq_int(request->now);
        params[count++] = ackq_int(request->now);
        params[count++] = ackq_int(queue->max_attempts);
        params[count++] = ackq_int(queue->max_attempts);
    } else {
        params[count++] = ackq_int(request->now);
        params[count++] = ackq_int(request->now);
        params[count++] = ackq_int(queue->max_attempts);
        params[count++] = ackq_int(queue->max_attempts);
    }
    params[count++] = ackq_int(request->now);
    params[count++] = ackq_int(request->now);
    params[count++] = ackq_int(queue->max_attempts);
    params[count++] = ackq_int(queue->max_attempts);
    bool was_dirty = scope->dirty;
    result_t *result = NULL;
    ackq_status_t status = ackq_run(queue, scope, true, sql, params, count, &result, NULL);
    if (status != ACKQ_OK) return status;
    status = take_claim(result, request->lease_until, item, matched);
    result_release(&result);
    if (status == ACKQ_OK && !*matched) scope->dirty = was_dirty;
    return status;
}

ackq_status_t ackq_conditional_finish(ackq_t *queue, ackq_scope_t *scope, const ackq_finish_request_t *request,
                                       bool *matched)
{
    *matched = false;
    int64_t id;
    if (!ackq_to_i64(request->id, &id)) return ACKQ_NOT_FOUND;
    char sql[ACKQ_SQL_MAX];
    int length = snprintf(sql, sizeof(sql),
                          "UPDATE %s SET state = ?, lease_until = 0, note = ?%s WHERE id = ? AND state = 1 AND fence = ?",
                          queue->table, queue->due_column ? ", due = ? + id" : "");
    if (length < 0 || (size_t)length >= sizeof(sql)) return ACKQ_LIMIT;
    int64_t base = request->state == ACKQ_STATE_ACKED ? ACKQ_DUE_ACKED : ACKQ_DUE_FAILED;
    value_view_t params[5] = {ackq_int(request->state), ackq_text(request->note), ackq_int(base), ackq_int(id),
                              ackq_int((int64_t)request->fence)};
    size_t count = 5;
    if (!queue->due_column) {
        params[2] = params[3];
        params[3] = params[4];
        count = 4;
    }
    bool was_dirty = scope->dirty;
    result_t *result = NULL;
    ackq_status_t status = ackq_run(queue, scope, true, sql, params, count, &result, NULL);
    status = ackq_note_result(scope, request->note, status);
    if (status != ACKQ_OK) return status;
    const result_command_t *command = result_command(result);
    if (command == NULL || command->affected_rows > 1) status = ACKQ_INTERNAL;
    else *matched = command->affected_rows == 1;
    result_release(&result);
    if (status == ACKQ_OK && !*matched) scope->dirty = was_dirty;
    return status;
}
