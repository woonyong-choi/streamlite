#include <stdlib.h>
#include <string.h>

#include "statement_clone.h"

static void *allocate(statement_clone_t *c, size_t count, size_t size)
{
    if (size != 0 && count > (c->limit_bytes - c->used_bytes) / size) {
        c->status = SQLSTMT_NOMEM;
        return NULL;
    }
    if (count == 0) return NULL;
    void *block = calloc(count, size);
    if (block == NULL) c->status = SQLSTMT_NOMEM;
    else c->used_bytes += count * size;
    return block;
}

static bool clone_name(statement_clone_t *c, const sqlstmt_name_t *from, sqlstmt_name_t *to)
{
    if (from->size == 0) return true;
    to->bytes = allocate(c, from->size, 1);
    if (to->bytes == NULL) return false;
    to->size = from->size;
    memcpy(to->bytes, from->bytes, from->size);
    return true;
}

static bool clone_names(statement_clone_t *c, const sqlstmt_name_t *from, size_t count,
                         sqlstmt_name_t **out, size_t *out_count)
{
    if (count == 0) return true;
    *out = allocate(c, count, sizeof(**out));
    if (*out == NULL) return false;
    *out_count = count;
    for (size_t i = 0; i < count; i++) {
        if (!clone_name(c, &from[i], &(*out)[i])) return false;
    }
    return true;
}

static bool clone_value(statement_clone_t *c, const sqlexpr_t *from, value_t *to)
{
    const value_view_t *view = &from->value.view;
    if (from->parameter_id != 0) {
        if (c->binding == NULL) return true;
        if (lexer_binding_value(c->binding, from->parameter_id, &view) != LEXER_OK) {
            c->status = SQLSTMT_INVALID;
            c->error = (sqlstmt_error_t){SQLSTMT_REASON_BAD_BINDING, from->offset, VALUE_INVALID};
            return false;
        }
    }
    size_t bytes = view->type == VALUE_TEXT || view->type == VALUE_BLOB ? view->size_bytes : 0;
    if (bytes > c->limit_bytes - c->used_bytes) {
        c->status = SQLSTMT_NOMEM;
        return false;
    }
    value_status_t status = value_clone(view, to);
    if (status != VALUE_OK) {
        c->status = status == VALUE_NOMEM ? SQLSTMT_NOMEM : SQLSTMT_INVALID;
        c->error = (sqlstmt_error_t){SQLSTMT_REASON_BAD_BINDING, from->offset, status};
        return false;
    }
    c->used_bytes += bytes;
    return true;
}

static bool clone_expr(statement_clone_t *c, const sqlexpr_t *from, sqlexpr_t **out);
static bool clone_select(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t *to);

static bool clone_query(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t **out)
{
    if (from == NULL) return true;
    *out = allocate(c, 1, sizeof(**out));
    return *out != NULL && clone_select(c, from, *out);
}

static bool clone_exprs(statement_clone_t *c, sqlexpr_t *const *from, size_t count,
                        sqlexpr_t ***out, size_t *out_count)
{
    if (count == 0) return true;
    *out = allocate(c, count, sizeof(**out));
    if (*out == NULL) return false;
    *out_count = count;
    for (size_t i = 0; i < count; i++) {
        if (!clone_expr(c, from[i], &(*out)[i])) return false;
    }
    return true;
}

static bool clone_expr(statement_clone_t *c, const sqlexpr_t *from, sqlexpr_t **out)
{
    if (from == NULL) return true;
    sqlexpr_t *to = allocate(c, 1, sizeof(*to));
    if (to == NULL) return false;
    *out = to;
    to->kind = from->kind;
    to->op = from->op;
    to->negated = from->negated;
    to->has_operand = from->has_operand;
    to->has_else = from->has_else;
    to->star = from->star;
    to->distinct = from->distinct;
    to->cast_type = from->cast_type;
    to->offset = from->offset;
    to->height = from->height;
    to->parameter_id = from->parameter_id;
    if (!clone_value(c, from, &to->value)) return false;
    if (!clone_name(c, &from->qualifier, &to->qualifier)) return false;
    if (!clone_name(c, &from->name, &to->name)) return false;
    if (!clone_exprs(c, from->args, from->arg_count, &to->args, &to->arg_count)) return false;
    return clone_query(c, from->subquery, &to->subquery);
}

static bool clone_items(statement_clone_t *c, const sqlstmt_select_item_t *from, size_t count,
                         sqlstmt_select_item_t **out, size_t *out_count)
{
    if (count == 0) return true;
    *out = allocate(c, count, sizeof(**out));
    if (*out == NULL) return false;
    *out_count = count;
    for (size_t i = 0; i < count; i++) {
        sqlstmt_select_item_t *to = &(*out)[i];
        to->star = from[i].star;
        if (!clone_expr(c, from[i].expr, &to->expr) ||
            !clone_name(c, &from[i].qualifier, &to->qualifier) ||
            !clone_name(c, &from[i].alias, &to->alias)) return false;
    }
    return true;
}

static bool clone_assignments(statement_clone_t *c, const sqlstmt_assignment_t *from, size_t count,
                              sqlstmt_assignment_t **out, size_t *out_count)
{
    if (count == 0) return true;
    *out = allocate(c, count, sizeof(**out));
    if (*out == NULL) return false;
    *out_count = count;
    for (size_t i = 0; i < count; i++) {
        if (!clone_name(c, &from[i].column, &(*out)[i].column) ||
            !clone_expr(c, from[i].value, &(*out)[i].value)) return false;
    }
    return true;
}

static bool clone_ctes(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t *to)
{
    if (from->cte_count == 0) return true;
    to->ctes = allocate(c, from->cte_count, sizeof(*to->ctes));
    if (to->ctes == NULL) return false;
    to->cte_count = from->cte_count;
    for (size_t i = 0; i < from->cte_count; i++) {
        const sqlstmt_cte_t *src = &from->ctes[i];
        sqlstmt_cte_t *dst = &to->ctes[i];
        if (!clone_name(c, &src->name, &dst->name) ||
            !clone_names(c, src->columns, src->column_count, &dst->columns, &dst->column_count) ||
            !clone_query(c, src->query, &dst->query)) return false;
    }
    return true;
}

static bool clone_sources(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t *to)
{
    if (from->source_count == 0) return true;
    to->sources = allocate(c, from->source_count, sizeof(*to->sources));
    if (to->sources == NULL) return false;
    to->source_count = from->source_count;
    for (size_t i = 0; i < from->source_count; i++) {
        const sqlstmt_source_t *src = &from->sources[i];
        sqlstmt_source_t *dst = &to->sources[i];
        dst->join = src->join;
        if (!clone_name(c, &src->table, &dst->table) ||
            !clone_name(c, &src->alias, &dst->alias) ||
            !clone_query(c, src->subquery, &dst->subquery) ||
            !clone_expr(c, src->on, &dst->on)) return false;
    }
    return true;
}

static bool clone_order(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t *to)
{
    if (from->order_count == 0) return true;
    to->order = allocate(c, from->order_count, sizeof(*to->order));
    if (to->order == NULL) return false;
    to->order_count = from->order_count;
    for (size_t i = 0; i < from->order_count; i++) {
        to->order[i].desc = from->order[i].desc;
        to->order[i].nulls = from->order[i].nulls;
        if (!clone_expr(c, from->order[i].expr, &to->order[i].expr)) return false;
    }
    return true;
}

static bool clone_select_fields(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t *to)
{
    to->distinct = from->distinct;
    to->next_op = from->next_op;
    if (!clone_ctes(c, from, to)) return false;
    if (!clone_items(c, from->items, from->item_count, &to->items, &to->item_count)) return false;
    if (!clone_sources(c, from, to)) return false;
    if (!clone_expr(c, from->where, &to->where)) return false;
    if (!clone_exprs(c, from->group_by, from->group_count, &to->group_by, &to->group_count)) return false;
    if (!clone_expr(c, from->having, &to->having)) return false;
    if (!clone_order(c, from, to)) return false;
    if (!clone_expr(c, from->limit, &to->limit)) return false;
    return clone_expr(c, from->offset, &to->offset);
}

static bool clone_select(statement_clone_t *c, const sqlstmt_select_t *from, sqlstmt_select_t *to)
{
    for (;;) {
        if (!clone_select_fields(c, from, to)) return false;
        from = from->next;
        if (from == NULL) return true;
        to->next = allocate(c, 1, sizeof(*to->next));
        if (to->next == NULL) return false;
        to = to->next;
    }
}

static bool clone_returning(statement_clone_t *c, const sqlstmt_returning_t *from, sqlstmt_returning_t *to)
{
    return clone_items(c, from->items, from->count, &to->items, &to->count);
}

static bool clone_conflict(statement_clone_t *c, const sqlstmt_conflict_t *from, sqlstmt_conflict_t *to)
{
    to->present = from->present;
    to->has_target = from->has_target;
    to->do_update = from->do_update;
    return clone_name(c, &from->target, &to->target) &&
           clone_assignments(c, from->assignments, from->assignment_count, &to->assignments, &to->assignment_count) &&
           clone_expr(c, from->where, &to->where);
}

static bool clone_insert(statement_clone_t *c, const sqlstmt_insert_t *from, sqlstmt_insert_t *to)
{
    to->has_columns = from->has_columns;
    if (!clone_name(c, &from->table, &to->table) ||
        !clone_names(c, from->columns, from->column_count, &to->columns, &to->column_count)) return false;
    size_t count = 0;
    bool ok = clone_exprs(c, from->values, from->row_count * from->value_count, &to->values, &count);
    if (!ok) {
        to->row_count = 1;
        to->value_count = count;
        return false;
    }
    to->row_count = from->row_count;
    to->value_count = from->value_count;
    return ok && clone_conflict(c, &from->conflict, &to->conflict) &&
           clone_returning(c, &from->returning, &to->returning);
}

static bool clone_update(statement_clone_t *c, const sqlstmt_update_t *from, sqlstmt_update_t *to)
{
    return clone_name(c, &from->table, &to->table) &&
           clone_assignments(c, from->assignments, from->assignment_count, &to->assignments, &to->assignment_count) &&
           clone_expr(c, from->where, &to->where) &&
           clone_returning(c, &from->returning, &to->returning);
}

static bool clone_delete(statement_clone_t *c, const sqlstmt_delete_t *from, sqlstmt_delete_t *to)
{
    return clone_name(c, &from->table, &to->table) &&
           clone_expr(c, from->where, &to->where) &&
           clone_returning(c, &from->returning, &to->returning);
}

sqlstmt_status_t statement_clone(const sqlstmt_t *source, statement_clone_t *c, sqlstmt_t **out)
{
    c->used_bytes = 0;
    c->status = SQLSTMT_OK;
    c->error = (sqlstmt_error_t){0};
    sqlstmt_t *built = allocate(c, 1, sizeof(*built));
    if (built == NULL) return c->status;
    built->kind = source->kind;
    bool ok = false;
    switch (source->kind) {
        case SQLSTMT_INSERT: ok = clone_insert(c, &source->insert, &built->insert); break;
        case SQLSTMT_SELECT: ok = clone_select(c, &source->select, &built->select); break;
        case SQLSTMT_UPDATE: ok = clone_update(c, &source->update, &built->update); break;
        case SQLSTMT_DELETE: ok = clone_delete(c, &source->del, &built->del); break;
        default: c->status = SQLSTMT_UNSUPPORTED; break;
    }
    if (ok) *out = built;
    else sqlstmt_free(built);
    return c->status;
}
