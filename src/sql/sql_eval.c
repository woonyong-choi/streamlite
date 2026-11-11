#include "sql/sql_eval.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 식 트리 높이만큼 재귀하는 길에 있는 함수는 컴파일러가 합치지 않게 한다(합치면 한 단계의 스택이 커진다) */
#define NOINLINE __attribute__((noinline))

typedef enum {
    FN_NONE,
    FN_ABS,
    FN_LENGTH,
    FN_LOWER,
    FN_UPPER,
    FN_SUBSTR,
    FN_TRIM,
    FN_LTRIM,
    FN_RTRIM,
    FN_REPLACE,
    FN_COALESCE,
    FN_NULLIF
} fn_t;

typedef struct {
    const char *name;
    fn_t fn;
    size_t min_args;
    size_t max_args;
} fn_spec_t;

static const fn_spec_t FUNCTIONS[] = {
    {"ABS", FN_ABS, 1, 1},           {"LENGTH", FN_LENGTH, 1, 1},     {"LOWER", FN_LOWER, 1, 1},
    {"UPPER", FN_UPPER, 1, 1},       {"SUBSTR", FN_SUBSTR, 2, 3},     {"SUBSTRING", FN_SUBSTR, 2, 3},
    {"TRIM", FN_TRIM, 1, 2},         {"LTRIM", FN_LTRIM, 1, 2},       {"RTRIM", FN_RTRIM, 1, 2},
    {"REPLACE", FN_REPLACE, 3, 3},   {"COALESCE", FN_COALESCE, 1, SIZE_MAX},
    {"NULLIF", FN_NULLIF, 2, 2},
};

typedef struct node {
    const sqlexpr_t *src;
    value_type_t type;
    bool matched;           /* match가 바꾼 부분식. row[column]을 읽는다 */
    size_t column;
    fn_t fn;
    const sqleval_subquery_t *sub;  /* SUBQUERY·EXISTS·IN ( query )의 결과 */
    const value_t *outer_cell;      /* 바깥 질의의 열이면 그 값이 놓이는 칸 */
    struct node **args;
} node_t;

struct sqleval_prog {
    node_t *root;
    node_t *nodes;
    node_t **slots;
    size_t node_count;
    sqleval_memory_t memory;  /* 계상 훅의 사본. reserve가 NULL이면 계상하지 않는다 */
    size_t bytes;             /* 이 구조와 두 배열로 reserve한 바이트 */
};

/* value_cast가 숫자를 문자열로 바꿀 때(INTEGER 20바이트, REAL 32바이트 이하)의 여유 */
#define CAST_SLACK 64u

static bool mem_take(const sqleval_memory_t *memory, size_t bytes)
{
    return bytes == 0 || memory->reserve == NULL || memory->reserve(memory->ctx, bytes);
}

static void mem_give(const sqleval_memory_t *memory, size_t bytes)
{
    if (bytes != 0 && memory->reserve != NULL) memory->release(memory->ctx, bytes);
}

/* ── 컴파일 ── */

typedef struct {
    const sqleval_env_t *env;
    node_t *nodes;
    size_t used;
    node_t **slots;
    size_t slots_used;
    sqleval_error_t *error;
} compiler_t;

static sqleval_status_t failed(sqleval_error_t *error, sqleval_status_t status, const sqlexpr_t *at,
                               const sqlstmt_name_t *name)
{
    if (error != NULL) {
        error->status = status;
        error->offset = at != NULL ? at->offset : 0;
        error->name = name;
    }
    return status;
}

static void count_nodes(const sqlexpr_t *expr, size_t *nodes, size_t *slots)
{
    (*nodes)++;
    *slots += expr->arg_count;
    for (size_t i = 0; i < expr->arg_count; i++) count_nodes(expr->args[i], nodes, slots);
}

static bool is_numeric(value_type_t type) { return type == VALUE_NULL || type == VALUE_INTEGER || type == VALUE_REAL; }
static bool is_boolish(value_type_t type) { return type == VALUE_NULL || type == VALUE_INTEGER; }
static bool is_textish(value_type_t type) { return type == VALUE_NULL || type == VALUE_TEXT; }

/* 두 형식을 하나로 합친다(CASE·COALESCE의 결과). NULL은 다른 쪽을 따른다 */
static bool unify(value_type_t a, value_type_t b, value_type_t *out)
{
    if (a == VALUE_NULL) *out = b;
    else if (b == VALUE_NULL || a == b) *out = a;
    else if (is_numeric(a) && is_numeric(b)) *out = VALUE_REAL;
    else return false;
    return true;
}

static bool comparable(value_type_t a, value_type_t b)
{
    if (a == VALUE_NULL || b == VALUE_NULL) return true;
    if (is_numeric(a) && is_numeric(b)) return true;
    return a == b;
}

static bool name_is(const sqlstmt_name_t *name, const char *text)
{
    size_t length = strlen(text);
    return name->size == length && memcmp(name->bytes, text, length) == 0;
}

static sqleval_status_t compile_node(compiler_t *c, const sqlexpr_t *expr, node_t **out);

static const char *const AGGREGATES[] = {"COUNT", "SUM", "AVG", "MIN", "MAX", NULL};

bool sqleval_is_aggregate(const sqlexpr_t *expr)
{
    if (expr == NULL || expr->kind != SQLEXPR_FUNCTION) return false;
    for (size_t i = 0; AGGREGATES[i] != NULL; i++) {
        if (name_is(&expr->name, AGGREGATES[i])) return expr->star ? i == 0 && expr->arg_count == 0 : expr->arg_count == 1;
    }
    return false;
}

static sqleval_status_t compile_function(compiler_t *c, node_t *node)
{
    const sqlexpr_t *expr = node->src;
    if (sqleval_is_aggregate(expr)) return failed(c->error, SQLEVAL_AGGREGATE, expr, &expr->name);
    const fn_spec_t *spec = NULL;
    for (size_t i = 0; i < sizeof(FUNCTIONS) / sizeof(FUNCTIONS[0]); i++) {
        if (name_is(&expr->name, FUNCTIONS[i].name)) spec = &FUNCTIONS[i];
    }
    if (spec == NULL) {
        /* 집계 이름인데 인자 꼴이 맞지 않으면(SUM(*), MAX(a, b)) 모르는 함수가 아니라 인자 오류다 */
        for (size_t i = 0; AGGREGATES[i] != NULL; i++) {
            if (name_is(&expr->name, AGGREGATES[i])) return failed(c->error, SQLEVAL_ARGUMENT, expr, &expr->name);
        }
        return failed(c->error, SQLEVAL_UNKNOWN_FUNCTION, expr, &expr->name);
    }
    if (expr->star || expr->distinct || expr->arg_count < spec->min_args || expr->arg_count > spec->max_args) {
        return failed(c->error, SQLEVAL_ARGUMENT, expr, &expr->name);
    }
    node->fn = spec->fn;
    value_type_t a = node->args[0]->type;
    switch (spec->fn) {
        case FN_ABS:
            if (!is_numeric(a)) return failed(c->error, SQLEVAL_TYPE, expr, &expr->name);
            node->type = a;
            return SQLEVAL_OK;
        case FN_LENGTH:
            if (a != VALUE_NULL && a != VALUE_TEXT && a != VALUE_BLOB) return failed(c->error, SQLEVAL_TYPE, expr, &expr->name);
            node->type = VALUE_INTEGER;
            return SQLEVAL_OK;
        case FN_SUBSTR:
            if (!is_textish(a) || !is_boolish(node->args[1]->type) ||
                (expr->arg_count == 3 && !is_boolish(node->args[2]->type))) {
                return failed(c->error, SQLEVAL_TYPE, expr, &expr->name);
            }
            node->type = VALUE_TEXT;
            return SQLEVAL_OK;
        case FN_LOWER:
        case FN_UPPER:
        case FN_TRIM:
        case FN_LTRIM:
        case FN_RTRIM:
        case FN_REPLACE:
            for (size_t i = 0; i < expr->arg_count; i++) {
                if (!is_textish(node->args[i]->type)) return failed(c->error, SQLEVAL_TYPE, expr, &expr->name);
            }
            node->type = VALUE_TEXT;
            return SQLEVAL_OK;
        case FN_COALESCE: {
            value_type_t type = VALUE_NULL;
            for (size_t i = 0; i < expr->arg_count; i++) {
                if (!unify(type, node->args[i]->type, &type)) return failed(c->error, SQLEVAL_TYPE, expr, &expr->name);
            }
            node->type = type;
            return SQLEVAL_OK;
        }
        case FN_NULLIF:
            if (!comparable(a, node->args[1]->type)) return failed(c->error, SQLEVAL_TYPE, expr, &expr->name);
            node->type = a;
            return SQLEVAL_OK;
        case FN_NONE:
            break;
    }
    return failed(c->error, SQLEVAL_UNKNOWN_FUNCTION, expr, &expr->name);
}

/* 서브쿼리 결과를 훅에서 받는다. 식 자리 서브쿼리는 열 하나여야 한다(EXISTS는 열 수를 보지 않는다) */
static sqleval_status_t subquery_of(compiler_t *c, node_t *node)
{
    const sqlexpr_t *expr = node->src;
    if (c->env->subquery == NULL) return failed(c->error, SQLEVAL_NOT_CONSTANT, expr, NULL);
    sqleval_status_t status = c->env->subquery(c->env->subquery_ctx, expr, &node->sub);
    if (status != SQLEVAL_OK) return failed(c->error, status, expr, NULL);
    if (expr->kind != SQLEXPR_EXISTS && node->sub->column_count != 1) {
        return failed(c->error, SQLEVAL_SUBQUERY_SHAPE, expr, NULL);
    }
    return SQLEVAL_OK;
}

static sqleval_status_t type_node(compiler_t *c, node_t *node)
{
    const sqlexpr_t *expr = node->src;
    node_t **args = node->args;
    switch (expr->kind) {
        case SQLEXPR_LITERAL:
            node->type = expr->value.view.type;
            return SQLEVAL_OK;
        case SQLEXPR_COLUMN: {
            if (c->env->resolve == NULL) return failed(c->error, SQLEVAL_NOT_CONSTANT, expr, &expr->name);
            value_type_t type = VALUE_NULL;
            size_t column = c->env->resolve(c->env->ctx, &expr->qualifier, &expr->name, &type);
            if (column == SQLEVAL_NO_COLUMN && c->env->outer != NULL &&
                c->env->outer(c->env->subquery_ctx, &expr->qualifier, &expr->name, &type, &node->outer_cell)) {
                node->type = type;
                return SQLEVAL_OK;
            }
            if (column == SQLEVAL_NO_COLUMN) return failed(c->error, SQLEVAL_UNKNOWN_COLUMN, expr, &expr->name);
            if (column == SQLEVAL_AMBIGUOUS) return failed(c->error, SQLEVAL_AMBIGUOUS_COLUMN, expr, &expr->name);
            if (column == SQLEVAL_UNGROUPED) return failed(c->error, SQLEVAL_NOT_GROUPED, expr, &expr->name);
            node->column = column;
            node->type = type;
            return SQLEVAL_OK;
        }
        case SQLEXPR_UNARY:
            if (expr->op == SQLEXPR_OP_NOT) {
                if (!is_boolish(args[0]->type)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                node->type = VALUE_INTEGER;
            } else {
                if (!is_numeric(args[0]->type)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                node->type = args[0]->type;
            }
            return SQLEVAL_OK;
        case SQLEXPR_BINARY: {
            value_type_t a = args[0]->type, b = args[1]->type;
            switch (expr->op) {
                case SQLEXPR_OP_ADD:
                case SQLEXPR_OP_SUB:
                case SQLEXPR_OP_MUL:
                case SQLEXPR_OP_DIV:
                    if (!is_numeric(a) || !is_numeric(b)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                    if (!unify(a, b, &node->type)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                    if (node->type == VALUE_NULL) node->type = VALUE_INTEGER;
                    return SQLEVAL_OK;
                case SQLEXPR_OP_MOD:
                    if (!is_boolish(a) || !is_boolish(b)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                    node->type = VALUE_INTEGER;
                    return SQLEVAL_OK;
                case SQLEXPR_OP_CONCAT:
                    if ((!is_textish(a) && a != VALUE_INTEGER) || (!is_textish(b) && b != VALUE_INTEGER)) {
                        return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                    }
                    node->type = VALUE_TEXT;
                    return SQLEVAL_OK;
                case SQLEXPR_OP_AND:
                case SQLEXPR_OP_OR:
                    if (!is_boolish(a) || !is_boolish(b)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                    node->type = VALUE_INTEGER;
                    return SQLEVAL_OK;
                default:
                    if (!comparable(a, b)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                    node->type = VALUE_INTEGER;
                    return SQLEVAL_OK;
            }
        }
        case SQLEXPR_IS_NULL:
            node->type = VALUE_INTEGER;
            return SQLEVAL_OK;
        case SQLEXPR_SUBQUERY:
        case SQLEXPR_EXISTS: {
            sqleval_status_t status = subquery_of(c, node);
            if (status != SQLEVAL_OK) return status;
            if (expr->kind == SQLEXPR_SUBQUERY && node->sub->refresh == NULL && node->sub->row_count > 1) {
                return failed(c->error, SQLEVAL_SUBQUERY_SHAPE, expr, NULL);
            }
            node->type = expr->kind == SQLEXPR_EXISTS ? VALUE_INTEGER : node->sub->type;
            return SQLEVAL_OK;
        }
        case SQLEXPR_BETWEEN:
        case SQLEXPR_IN:
            if (expr->subquery != NULL) {
                sqleval_status_t status = subquery_of(c, node);
                if (status != SQLEVAL_OK) return status;
                if (!comparable(args[0]->type, node->sub->type)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
                node->type = VALUE_INTEGER;
                return SQLEVAL_OK;
            }
            for (size_t i = 1; i < expr->arg_count; i++) {
                if (!comparable(args[0]->type, args[i]->type)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
            }
            node->type = VALUE_INTEGER;
            return SQLEVAL_OK;
        case SQLEXPR_LIKE:
            for (size_t i = 0; i < expr->arg_count; i++) {
                if (!is_textish(args[i]->type)) return failed(c->error, SQLEVAL_TYPE, expr, NULL);
            }
            node->type = VALUE_INTEGER;
            return SQLEVAL_OK;
        case SQLEXPR_CASE: {
            size_t at = expr->has_operand ? 1 : 0;
            size_t pairs_end = expr->arg_count - (expr->has_else ? 1 : 0);
            value_type_t type = VALUE_NULL;
            for (; at < pairs_end; at += 2) {
                value_type_t when = args[at]->type;
                if (expr->has_operand ? !comparable(args[0]->type, when) : !is_boolish(when)) {
                    return failed(c->error, SQLEVAL_TYPE, args[at]->src, NULL);
                }
                if (!unify(type, args[at + 1]->type, &type)) return failed(c->error, SQLEVAL_TYPE, args[at + 1]->src, NULL);
            }
            if (expr->has_else && !unify(type, args[expr->arg_count - 1]->type, &type)) {
                return failed(c->error, SQLEVAL_TYPE, args[expr->arg_count - 1]->src, NULL);
            }
            node->type = type;
            return SQLEVAL_OK;
        }
        case SQLEXPR_CAST:
            node->type = expr->cast_type;
            return SQLEVAL_OK;
        case SQLEXPR_FUNCTION:
            return compile_function(c, node);
    }
    return failed(c->error, SQLEVAL_TYPE, expr, NULL);
}

static sqleval_status_t compile_node(compiler_t *c, const sqlexpr_t *expr, node_t **out)
{
    node_t *node = &c->nodes[c->used++];
    node->src = expr;
    node->args = &c->slots[c->slots_used];
    c->slots_used += expr->arg_count;
    if (c->env->match != NULL) {
        value_type_t type = VALUE_NULL;
        size_t index = c->env->match(c->env->ctx, expr, &type);
        if (index != SQLEVAL_NO_COLUMN) {
            node->matched = true;
            node->column = index;
            node->type = type;
            *out = node;
            return SQLEVAL_OK;
        }
    }
    for (size_t i = 0; i < expr->arg_count; i++) {
        sqleval_status_t status = compile_node(c, expr->args[i], &node->args[i]);
        if (status != SQLEVAL_OK) return status;
    }
    *out = node;
    return type_node(c, node);
}

sqleval_status_t sqleval_compile(const sqlexpr_t *expr, const sqleval_env_t *env, sqleval_prog_t **out,
                                 sqleval_error_t *error)
{
    if (out == NULL || env == NULL) return failed(error, SQLEVAL_ARGUMENT, NULL, NULL);
    *out = NULL;
    if (expr == NULL) return failed(error, SQLEVAL_ARGUMENT, NULL, NULL);
    size_t nodes = 0, slots = 0;
    count_nodes(expr, &nodes, &slots);
    if (slots == 0) slots = 1;
    /* 노드와 인자 자리는 문장의 식 노드 수만큼이다. 할당하기 전에 계상한다 */
    if (nodes > (SIZE_MAX - sizeof(sqleval_prog_t)) / (sizeof(node_t) + sizeof(node_t *)) || slots > nodes + 1) {
        return failed(error, SQLEVAL_NOMEM, expr, NULL);
    }
    size_t bytes = sizeof(sqleval_prog_t) + nodes * sizeof(node_t) + slots * sizeof(node_t *);
    if (!mem_take(&env->memory, bytes)) return failed(error, SQLEVAL_NOMEM, expr, NULL);
    sqleval_prog_t *prog = calloc(1, sizeof(*prog));
    node_t *array = calloc(nodes, sizeof(*array));
    node_t **slot_array = calloc(slots, sizeof(*slot_array));
    if (prog == NULL || array == NULL || slot_array == NULL) {
        free(prog);
        free(array);
        free(slot_array);
        mem_give(&env->memory, bytes);
        return failed(error, SQLEVAL_NOMEM, expr, NULL);
    }
    prog->nodes = array;
    prog->slots = slot_array;
    prog->node_count = nodes;
    prog->memory = env->memory;
    prog->bytes = bytes;
    compiler_t c = {env, array, 0, slot_array, 0, error};
    sqleval_status_t status = compile_node(&c, expr, &prog->root);
    if (status != SQLEVAL_OK) {
        sqleval_free(prog);
        return status;
    }
    *out = prog;
    return SQLEVAL_OK;
}

void sqleval_free(sqleval_prog_t *prog)
{
    if (prog == NULL) return;
    sqleval_memory_t memory = prog->memory;
    size_t bytes = prog->bytes;
    free(prog->nodes);
    free(prog->slots);
    free(prog);
    mem_give(&memory, bytes);
}

value_type_t sqleval_type(const sqleval_prog_t *prog) { return prog != NULL ? prog->root->type : VALUE_NULL; }

void sqleval_mark_columns(const sqleval_prog_t *prog, bool *needed, size_t column_count)
{
    if (prog == NULL) return;
    for (size_t i = 0; i < prog->node_count; i++) {
        const node_t *node = &prog->nodes[i];
        if (node->outer_cell == NULL && (node->matched || node->src->kind == SQLEXPR_COLUMN) && node->column < column_count) {
            needed[node->column] = true;
        }
        for (size_t k = 0; node->sub != NULL && k < node->sub->outer_count; k++) {
            if (node->sub->outer_columns[k] < column_count) needed[node->sub->outer_columns[k]] = true;
        }
    }
}

/* ── 실행 ── */

typedef struct {
    const value_t *row;
    sqleval_error_t *error;
    const sqleval_memory_t *memory;
} runner_t;

/*
 * node를 계산해 *view에 둔다. 새 값이 필요하면 *tmp에 만들고 view가 그것을 가리킨다(호출자가 tmp_clear로 비운다).
 * 열과 리터럴은 복사하지 않고 빌려 준다.
 *
 * 계상(#272): tmp가 쥔 TEXT·BLOB 바이트는 만들기 전에 reserve하고 tmp_clear가 돌려준다. 값을 만들 때 쓰는 작업
 * buffer도 같다. 그래서 중간값이 아무리 커도 예산을 넘겨 할당하지 않는다.
 */
static sqleval_status_t eval(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view);

static size_t owned_bytes(const value_t *value) { return value->owns_bytes ? value->view.size_bytes : 0; }

static void tmp_clear(const runner_t *r, value_t *tmp)
{
    size_t bytes = owned_bytes(tmp);
    value_clear(tmp);
    mem_give(r->memory, bytes);
}

static sqleval_status_t from_value(const runner_t *r, value_status_t status, const node_t *node)
{
    switch (status) {
        case VALUE_OK: return SQLEVAL_OK;
        case VALUE_NOMEM: return failed(r->error, SQLEVAL_NOMEM, node->src, NULL);
        case VALUE_OVERFLOW: return failed(r->error, SQLEVAL_OVERFLOW, node->src, NULL);
        case VALUE_LIMIT: return failed(r->error, SQLEVAL_LIMIT, node->src, NULL);
        default: return failed(r->error, SQLEVAL_TYPE, node->src, NULL);
    }
}

/*
 * tmp에 새 값을 쓴 value 함수의 결과를 마무리한다. reserved는 그 값을 위해 미리 reserve한 바이트, old는 쓰기 전에
 * tmp가 쥐고 있던 바이트다. 실패하면 tmp는 그대로이므로 reserved만 돌려준다. 성공하면 옛 값이 해제됐으므로 old를,
 * 그리고 실제로 쥔 것보다 더 잡은 몫을 돌려준다.
 */
static sqleval_status_t settle(const runner_t *r, const node_t *node, value_status_t status, size_t old, size_t reserved,
                               value_t *tmp, value_view_t *view)
{
    if (status != VALUE_OK) {
        mem_give(r->memory, reserved);
        return from_value(r, status, node);
    }
    size_t kept = owned_bytes(tmp);
    mem_give(r->memory, old);
    if (reserved > kept) mem_give(r->memory, reserved - kept);
    *view = tmp->view;
    return SQLEVAL_OK;
}

static sqleval_status_t set_int(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view, int64_t v)
{
    size_t old = owned_bytes(tmp);
    return settle(r, node, value_set_integer(tmp, v), old, 0, tmp, view);
}

static sqleval_status_t set_real(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view, double v)
{
    size_t old = owned_bytes(tmp);
    return settle(r, node, value_set_real(tmp, v), old, 0, tmp, view);
}

static sqleval_status_t set_null(const runner_t *r, value_t *tmp, value_view_t *view)
{
    tmp_clear(r, tmp);
    *view = tmp->view;
    return SQLEVAL_OK;
}

/* 조건 값: 1 참, 0 거짓, -1 NULL */
static sqleval_status_t truth_of(const runner_t *r, const node_t *node, const value_view_t *v, int *truth)
{
    if (v->type == VALUE_NULL) *truth = -1;
    else if (v->type == VALUE_INTEGER) *truth = v->integer != 0;
    else return failed(r->error, SQLEVAL_TYPE, node->src, NULL);
    return SQLEVAL_OK;
}

static sqleval_status_t truth(const runner_t *r, const node_t *node, int *out)
{
    value_t tmp = {0};
    value_view_t view;
    sqleval_status_t status = eval(r, node, &tmp, &view);
    if (status == SQLEVAL_OK) status = truth_of(r, node, &view, out);
    tmp_clear(r, &tmp);
    return status;
}

static sqleval_status_t set_truth(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view, int t)
{
    return t < 0 ? set_null(r, tmp, view) : set_int(r, node, tmp, view, t);
}

/* NULL이 아닌 두 값의 비교. 형식은 컴파일 때 맞췄다 */
static int compare_values(const value_view_t *a, const value_view_t *b)
{
    int order = 0;
    value_compare(a, b, &order);
    return order;
}

static double as_real(const value_view_t *v) { return v->type == VALUE_REAL ? v->real : (double)v->integer; }

static sqleval_status_t arithmetic(const runner_t *r, const node_t *node, const value_view_t *a,
                                   const value_view_t *b, value_t *tmp, value_view_t *view)
{
    sqlexpr_op_t op = node->src->op;
    if (a->type == VALUE_INTEGER && b->type == VALUE_INTEGER && node->type != VALUE_REAL) {
        int64_t x = a->integer, y = b->integer, z = 0;
        bool overflow = false;
        switch (op) {
            case SQLEXPR_OP_ADD: overflow = __builtin_add_overflow(x, y, &z); break;
            case SQLEXPR_OP_SUB: overflow = __builtin_sub_overflow(x, y, &z); break;
            case SQLEXPR_OP_MUL: overflow = __builtin_mul_overflow(x, y, &z); break;
            case SQLEXPR_OP_DIV:
            case SQLEXPR_OP_MOD:
                if (y == 0) return failed(r->error, SQLEVAL_DIVIDE_BY_ZERO, node->src, NULL);
                if (x == INT64_MIN && y == -1) {
                    if (op == SQLEXPR_OP_DIV) overflow = true;
                    else z = 0;
                } else {
                    z = op == SQLEXPR_OP_DIV ? x / y : x % y;
                }
                break;
            default: return failed(r->error, SQLEVAL_TYPE, node->src, NULL);
        }
        if (overflow) return failed(r->error, SQLEVAL_OVERFLOW, node->src, NULL);
        return set_int(r, node, tmp, view, z);
    }
    if (op == SQLEXPR_OP_MOD) return failed(r->error, SQLEVAL_TYPE, node->src, NULL);
    double x = as_real(a), y = as_real(b), z;
    switch (op) {
        case SQLEXPR_OP_ADD: z = x + y; break;
        case SQLEXPR_OP_SUB: z = x - y; break;
        case SQLEXPR_OP_MUL: z = x * y; break;
        case SQLEXPR_OP_DIV:
            if (y == 0.0) return failed(r->error, SQLEVAL_DIVIDE_BY_ZERO, node->src, NULL);
            z = x / y;
            break;
        default: return failed(r->error, SQLEVAL_TYPE, node->src, NULL);
    }
    return set_real(r, node, tmp, view, z);
}

/* INTEGER면 10진 표기, TEXT면 그대로. buffer는 INTEGER 표기용 */
static void text_of(const value_view_t *v, char buffer[32], const uint8_t **bytes, size_t *size)
{
    if (v->type == VALUE_INTEGER) {
        size_t needed = 0;
        value_format(v, buffer, 32, &needed);
        *bytes = (const uint8_t *)buffer;
        *size = needed;
    } else {
        *bytes = v->bytes;
        *size = v->size_bytes;
    }
}

static sqleval_status_t set_text_bytes(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view,
                                       const uint8_t *bytes, size_t size)
{
    size_t old = owned_bytes(tmp);
    if (!mem_take(r->memory, size)) return failed(r->error, SQLEVAL_NOMEM, node->src, NULL);
    return settle(r, node, value_set_text(tmp, bytes, size), old, size, tmp, view);
}

/* 값을 만드는 동안만 쓰는 작업 buffer. 계상한 뒤 할당하고 scratch_free가 돌려준다. 거절되면 NULL이다 */
static uint8_t *scratch_alloc(const runner_t *r, size_t bytes)
{
    if (!mem_take(r->memory, bytes)) return NULL;
    uint8_t *block = malloc(bytes);
    if (block == NULL) mem_give(r->memory, bytes);
    return block;
}

static void scratch_free(const runner_t *r, uint8_t *block, size_t bytes)
{
    free(block);
    mem_give(r->memory, bytes);
}

static sqleval_status_t concat(const runner_t *r, const node_t *node, const value_view_t *a, const value_view_t *b,
                               value_t *tmp, value_view_t *view)
{
    char left_buffer[32], right_buffer[32];
    const uint8_t *left, *right;
    size_t left_size, right_size;
    text_of(a, left_buffer, &left, &left_size);
    text_of(b, right_buffer, &right, &right_size);
    if (left_size > SIZE_MAX - right_size || left_size + right_size > VALUE_MAX_BYTES) {
        return failed(r->error, SQLEVAL_LIMIT, node->src, NULL);
    }
    uint8_t *joined = scratch_alloc(r, left_size + right_size + 1);
    if (joined == NULL) return failed(r->error, SQLEVAL_NOMEM, node->src, NULL);
    if (left_size > 0) memcpy(joined, left, left_size);
    if (right_size > 0) memcpy(joined + left_size, right, right_size);
    sqleval_status_t status = set_text_bytes(r, node, tmp, view, joined, left_size + right_size);
    scratch_free(r, joined, left_size + right_size + 1);
    return status;
}

/* UTF-8 한 글자의 바이트 수(유효한 TEXT라고 가정한다) */
static size_t char_len(uint8_t lead)
{
    if (lead < 0x80) return 1;
    if (lead < 0xE0) return 2;
    if (lead < 0xF0) return 3;
    return 4;
}

static bool same_char(const uint8_t *a, const uint8_t *b, size_t len) { return memcmp(a, b, len) == 0; }

/* LIKE: %는 0개 이상, _는 한 글자, escape 뒤의 글자는 그대로. 대소문자를 구분한다 */
static bool like_match(const uint8_t *text, size_t text_size, const uint8_t *pattern, size_t pattern_size,
                       const uint8_t *escape, size_t escape_size)
{
    size_t t = 0, p = 0, star_p = SIZE_MAX, star_t = 0;
    while (t < text_size) {
        if (p < pattern_size) {
            size_t plen = char_len(pattern[p]);
            bool escaped = escape_size > 0 && plen == escape_size && same_char(pattern + p, escape, plen) &&
                           p + plen < pattern_size;
            if (!escaped && pattern[p] == '%') {
                star_p = ++p;
                star_t = t;
                continue;
            }
            size_t at = escaped ? p + plen : p;
            size_t clen = char_len(pattern[at]);
            size_t tlen = char_len(text[t]);
            if ((!escaped && pattern[at] == '_') || (clen == tlen && same_char(pattern + at, text + t, clen))) {
                p = at + clen;
                t += tlen;
                continue;
            }
        }
        if (star_p == SIZE_MAX) return false;
        star_t += char_len(text[star_t]);
        t = star_t;
        p = star_p;
    }
    while (p < pattern_size && pattern[p] == '%') p++;
    return p == pattern_size;
}

static NOINLINE sqleval_status_t eval_like(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    value_t parts_tmp[3];
    memset(parts_tmp, 0, sizeof(parts_tmp));
    value_view_t parts[3];
    memset(parts, 0, sizeof(parts));
    size_t count = node->src->arg_count;
    sqleval_status_t status = SQLEVAL_OK;
    bool has_null = false;
    for (size_t i = 0; i < count && status == SQLEVAL_OK; i++) {
        status = eval(r, node->args[i], &parts_tmp[i], &parts[i]);
        if (status == SQLEVAL_OK && parts[i].type == VALUE_NULL) has_null = true;
    }
    if (status == SQLEVAL_OK) {
        if (has_null) {
            status = set_null(r, tmp, view);
        } else if (count == 3 && (parts[2].size_bytes == 0 || char_len(parts[2].bytes[0]) != parts[2].size_bytes)) {
            status = failed(r->error, SQLEVAL_ARGUMENT, node->src, NULL);
        } else {
            bool matched = like_match(parts[0].bytes, parts[0].size_bytes, parts[1].bytes, parts[1].size_bytes,
                                      count == 3 ? parts[2].bytes : NULL, count == 3 ? parts[2].size_bytes : 0);
            status = set_int(r, node, tmp, view, matched != node->src->negated);
        }
    }
    for (size_t i = 0; i < 3; i++) tmp_clear(r, &parts_tmp[i]);
    return status;
}

/* 같은지(NULL이면 unknown -1). 형식은 맞춰져 있다 */
static int equals(const value_view_t *a, const value_view_t *b)
{
    if (a->type == VALUE_NULL || b->type == VALUE_NULL) return -1;
    return compare_values(a, b) == 0;
}

/* 바깥 열을 보는 서브쿼리는 이 행에 맞게 다시 계산한다 */
static sqleval_status_t refresh(const runner_t *r, const node_t *node)
{
    if (node->sub->refresh == NULL) return SQLEVAL_OK;
    sqleval_status_t status = node->sub->refresh(node->sub->refresh_ctx, r->row);
    return status == SQLEVAL_OK ? SQLEVAL_OK : failed(r->error, status, node->src, NULL);
}

/* x IN ( query ): 정렬된 집합에서 이분 탐색한다 */
static int in_subquery(const sqleval_subquery_t *sub, const value_view_t *x)
{
    if (sub->set_count == 0 && !sub->set_has_null) return 0;
    if (x->type == VALUE_NULL) return -1;
    size_t lo = 0, hi = sub->set_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int order = compare_values(&sub->set[mid], x);
        if (order == 0) return 1;
        if (order < 0) lo = mid + 1;
        else hi = mid;
    }
    return sub->set_has_null ? -1 : 0;
}

static NOINLINE sqleval_status_t eval_in(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    value_t left_tmp = {0};
    value_view_t left;
    sqleval_status_t status = eval(r, node->args[0], &left_tmp, &left);
    int result = 0;
    if (status == SQLEVAL_OK && node->sub != NULL) status = refresh(r, node);
    if (status == SQLEVAL_OK && node->sub != NULL) result = in_subquery(node->sub, &left);
    for (size_t i = 1; i < node->src->arg_count && status == SQLEVAL_OK && result != 1; i++) {
        value_t item_tmp = {0};
        value_view_t item;
        status = eval(r, node->args[i], &item_tmp, &item);
        if (status == SQLEVAL_OK) {
            int eq = equals(&left, &item);
            if (eq == 1) result = 1;
            else if (eq < 0) result = -1;
        }
        tmp_clear(r, &item_tmp);
    }
    tmp_clear(r, &left_tmp);
    if (status != SQLEVAL_OK) return status;
    if (node->src->negated && result >= 0) result = !result;
    return set_truth(r, node, tmp, view, result);
}

static NOINLINE sqleval_status_t eval_between(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    value_t parts_tmp[3];
    memset(parts_tmp, 0, sizeof(parts_tmp));
    value_view_t parts[3];
    memset(parts, 0, sizeof(parts));
    sqleval_status_t status = SQLEVAL_OK;
    for (size_t i = 0; i < 3 && status == SQLEVAL_OK; i++) status = eval(r, node->args[i], &parts_tmp[i], &parts[i]);
    if (status == SQLEVAL_OK) {
        /* x >= lo AND x <= hi 의 3치 논리 */
        int low = parts[0].type == VALUE_NULL || parts[1].type == VALUE_NULL ? -1 : compare_values(&parts[0], &parts[1]) >= 0;
        int high = parts[0].type == VALUE_NULL || parts[2].type == VALUE_NULL ? -1 : compare_values(&parts[0], &parts[2]) <= 0;
        int both = low == 0 || high == 0 ? 0 : (low < 0 || high < 0 ? -1 : 1);
        if (node->src->negated && both >= 0) both = !both;
        status = set_truth(r, node, tmp, view, both);
    }
    for (size_t i = 0; i < 3; i++) tmp_clear(r, &parts_tmp[i]);
    return status;
}

static NOINLINE sqleval_status_t eval_case(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    const sqlexpr_t *expr = node->src;
    value_t operand_tmp = {0};
    value_view_t operand = {VALUE_NULL, 0, 0.0, NULL, 0};
    sqleval_status_t status = SQLEVAL_OK;
    size_t at = 0;
    if (expr->has_operand) {
        status = eval(r, node->args[0], &operand_tmp, &operand);
        at = 1;
    }
    size_t pairs_end = expr->arg_count - (expr->has_else ? 1 : 0);
    const node_t *chosen = expr->has_else ? node->args[expr->arg_count - 1] : NULL;
    for (; status == SQLEVAL_OK && at < pairs_end; at += 2) {
        int hit = 0;
        if (expr->has_operand) {
            value_t when_tmp = {0};
            value_view_t when;
            status = eval(r, node->args[at], &when_tmp, &when);
            hit = status == SQLEVAL_OK ? equals(&operand, &when) == 1 : 0;
            tmp_clear(r, &when_tmp);
        } else {
            status = truth(r, node->args[at], &hit);
            hit = hit == 1;
        }
        if (status == SQLEVAL_OK && hit) {
            chosen = node->args[at + 1];
            break;
        }
    }
    tmp_clear(r, &operand_tmp);
    if (status != SQLEVAL_OK) return status;
    if (chosen == NULL) return set_null(r, tmp, view);
    status = eval(r, chosen, tmp, view);
    if (status == SQLEVAL_OK && node->type == VALUE_REAL && view->type == VALUE_INTEGER) {
        return set_real(r, node, tmp, view, (double)view->integer);
    }
    return status;
}

/* SQLite가 길이 없는 SUBSTR에 쓰는 길이(문자열 길이 한도 기본값). 시작이 문자열 앞으로 벗어난 만큼 여기서 뺀다 */
#define SUBSTR_REST 1000000000

/*
 * 글자 단위 SUBSTR. SQLite와 같다: start는 1부터 세고 음수면 끝에서 센다(-1이 마지막 글자). 0은 첫 글자 바로 앞
 * 자리라 길이 하나를 그 자리에 쓴다. 음수 count는 start 앞의 글자들이다. 범위를 벗어난 부분은 잘라 낸다.
 */
static sqleval_status_t substr(const runner_t *r, const node_t *node, const value_view_t *text, int64_t start,
                               bool has_count, int64_t count, value_t *tmp, value_view_t *view)
{
    size_t chars = 0;
    for (size_t at = 0; at < text->size_bytes; at += char_len(text->bytes[at])) chars++;
    /* INT64 끝 값끼리 더해도 넘치지 않게 넓은 정수로 계산한다 */
    __int128 length = (__int128)chars, skip = start, take = length > SUBSTR_REST ? length : SUBSTR_REST;
    bool before = false;
    if (has_count) {
        take = count;
        if (take < 0) {
            take = -take;
            before = true;
        }
    }
    if (skip < 0) {
        skip += length;
        if (skip < 0) {
            take += skip;
            if (take < 0) take = 0;
            skip = 0;
        }
    } else if (skip > 0) {
        skip--;
    } else if (take > 0) {
        take--;
    }
    if (before) {
        skip -= take;
        if (skip < 0) {
            take += skip;
            skip = 0;
        }
    }
    size_t from = 0, to;
    for (; skip > 0 && from < text->size_bytes; skip--) from += char_len(text->bytes[from]);
    for (to = from; take > 0 && to < text->size_bytes; take--) to += char_len(text->bytes[to]);
    return set_text_bytes(r, node, tmp, view, text->bytes + from, to - from);
}

/* C 로캘 isspace와 같은 여섯 글자. 로캘에 따라 결과가 바뀌지 않게 직접 본다 */
static bool is_ascii_space(uint8_t c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static bool in_set(const uint8_t *c, size_t len, const value_view_t *set)
{
    for (size_t at = 0; at < set->size_bytes; at += char_len(set->bytes[at])) {
        size_t slen = char_len(set->bytes[at]);
        if (slen == len && same_char(set->bytes + at, c, len)) return true;
    }
    return false;
}

static sqleval_status_t trim(const runner_t *r, const node_t *node, const value_view_t *text, const value_view_t *set,
                             bool left, bool right, value_t *tmp, value_view_t *view)
{
    static const value_view_t SPACE = {VALUE_TEXT, 0, 0.0, (const uint8_t *)" ", 1};
    if (set == NULL) set = &SPACE;
    size_t from = 0, to = text->size_bytes;
    if (left) {
        while (from < to && in_set(text->bytes + from, char_len(text->bytes[from]), set)) from += char_len(text->bytes[from]);
    }
    if (right) {
        while (to > from) {
            size_t start = to - 1;
            while (start > from && (text->bytes[start] & 0xC0) == 0x80) start--;
            if (!in_set(text->bytes + start, to - start, set)) break;
            to = start;
        }
    }
    return set_text_bytes(r, node, tmp, view, text->bytes + from, to - from);
}

static sqleval_status_t replace(const runner_t *r, const node_t *node, const value_view_t *text,
                                const value_view_t *from, const value_view_t *to, value_t *tmp, value_view_t *view)
{
    if (from->size_bytes == 0) return set_text_bytes(r, node, tmp, view, text->bytes, text->size_bytes);
    size_t hits = 0;
    for (size_t at = 0; at + from->size_bytes <= text->size_bytes;) {
        if (memcmp(text->bytes + at, from->bytes, from->size_bytes) == 0) {
            hits++;
            at += from->size_bytes;
        } else at++;
    }
    size_t size = text->size_bytes;
    if (to->size_bytes > from->size_bytes) {
        size_t grow = to->size_bytes - from->size_bytes;
        if (hits > 0 && grow > (VALUE_MAX_BYTES - size) / hits) return failed(r->error, SQLEVAL_LIMIT, node->src, NULL);
        size += grow * hits;
    } else {
        size -= (from->size_bytes - to->size_bytes) * hits;
    }
    uint8_t *out = scratch_alloc(r, size + 1);
    if (out == NULL) return failed(r->error, SQLEVAL_NOMEM, node->src, NULL);
    size_t w = 0;
    for (size_t at = 0; at < text->size_bytes;) {
        if (at + from->size_bytes <= text->size_bytes && memcmp(text->bytes + at, from->bytes, from->size_bytes) == 0) {
            if (to->size_bytes > 0) memcpy(out + w, to->bytes, to->size_bytes);
            w += to->size_bytes;
            at += from->size_bytes;
        } else out[w++] = text->bytes[at++];
    }
    sqleval_status_t status = set_text_bytes(r, node, tmp, view, out, w);
    scratch_free(r, out, size + 1);
    return status;
}

static NOINLINE sqleval_status_t eval_function(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    size_t count = node->src->arg_count;
    if (node->fn == FN_COALESCE) {
        for (size_t i = 0; i < count; i++) {
            sqleval_status_t status = eval(r, node->args[i], tmp, view);
            if (status != SQLEVAL_OK) return status;
            if (view->type != VALUE_NULL) {
                if (node->type == VALUE_REAL && view->type == VALUE_INTEGER) {
                    return set_real(r, node, tmp, view, (double)view->integer);
                }
                return SQLEVAL_OK;
            }
        }
        return set_null(r, tmp, view);
    }
    value_t args_tmp[3];
    memset(args_tmp, 0, sizeof(args_tmp));
    value_view_t args[3];
    memset(args, 0, sizeof(args));
    sqleval_status_t status = SQLEVAL_OK;
    bool has_null = false;
    for (size_t i = 0; i < count && status == SQLEVAL_OK; i++) {
        status = eval(r, node->args[i], &args_tmp[i], &args[i]);
        if (status == SQLEVAL_OK && args[i].type == VALUE_NULL) has_null = true;
    }
    if (status == SQLEVAL_OK && node->fn == FN_NULLIF) {
        if (args[0].type != VALUE_NULL && equals(&args[0], &args[1]) == 1) {
            status = set_null(r, tmp, view);
        } else {
            size_t old = owned_bytes(tmp);
            size_t bytes = args[0].type == VALUE_TEXT || args[0].type == VALUE_BLOB ? args[0].size_bytes : 0;
            if (!mem_take(r->memory, bytes)) status = failed(r->error, SQLEVAL_NOMEM, node->src, NULL);
            else status = settle(r, node, value_clone(&args[0], tmp), old, bytes, tmp, view);
        }
    } else if (status == SQLEVAL_OK && has_null) {
        status = set_null(r, tmp, view);
    } else if (status == SQLEVAL_OK) {
        const value_view_t *a = &args[0];
        switch (node->fn) {
            case FN_ABS:
                if (a->type == VALUE_INTEGER) {
                    if (a->integer == INT64_MIN) status = failed(r->error, SQLEVAL_OVERFLOW, node->src, NULL);
                    else status = set_int(r, node, tmp, view, a->integer < 0 ? -a->integer : a->integer);
                } else {
                    status = set_real(r, node, tmp, view, a->real < 0 ? -a->real : a->real);
                }
                break;
            case FN_LENGTH:
                if (a->type == VALUE_BLOB) {
                    status = set_int(r, node, tmp, view, (int64_t)a->size_bytes);
                } else {
                    size_t chars = 0;
                    status = from_value(r, value_text_chars(a, &chars), node);
                    if (status == SQLEVAL_OK) status = set_int(r, node, tmp, view, (int64_t)chars);
                }
                break;
            case FN_LOWER:
            case FN_UPPER: {
                uint8_t *out = scratch_alloc(r, a->size_bytes + 1);
                if (out == NULL) {
                    status = failed(r->error, SQLEVAL_NOMEM, node->src, NULL);
                    break;
                }
                for (size_t i = 0; i < a->size_bytes; i++) {
                    uint8_t c = a->bytes[i];
                    if (node->fn == FN_LOWER && c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
                    if (node->fn == FN_UPPER && c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
                    out[i] = c;
                }
                status = set_text_bytes(r, node, tmp, view, out, a->size_bytes);
                scratch_free(r, out, a->size_bytes + 1);
                break;
            }
            case FN_SUBSTR:
                status = substr(r, node, a, args[1].integer, count == 3, count == 3 ? args[2].integer : 0, tmp, view);
                break;
            case FN_TRIM: status = trim(r, node, a, count == 2 ? &args[1] : NULL, true, true, tmp, view); break;
            case FN_LTRIM: status = trim(r, node, a, count == 2 ? &args[1] : NULL, true, false, tmp, view); break;
            case FN_RTRIM: status = trim(r, node, a, count == 2 ? &args[1] : NULL, false, true, tmp, view); break;
            case FN_REPLACE: status = replace(r, node, a, &args[1], &args[2], tmp, view); break;
            default: status = failed(r->error, SQLEVAL_UNKNOWN_FUNCTION, node->src, &node->src->name); break;
        }
    }
    for (size_t i = 0; i < 3; i++) tmp_clear(r, &args_tmp[i]);
    return status;
}

static NOINLINE sqleval_status_t eval_unary(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    const sqlexpr_t *expr = node->src;
    if (expr->op == SQLEXPR_OP_NOT) {
        int t;
        sqleval_status_t status = truth(r, node->args[0], &t);
        if (status != SQLEVAL_OK) return status;
        return set_truth(r, node, tmp, view, t < 0 ? -1 : !t);
    }
    sqleval_status_t status = eval(r, node->args[0], tmp, view);
    if (status != SQLEVAL_OK || expr->op == SQLEXPR_OP_PLUS || view->type == VALUE_NULL) return status;
    if (view->type == VALUE_INTEGER) {
        if (view->integer == INT64_MIN) return failed(r->error, SQLEVAL_OVERFLOW, expr, NULL);
        return set_int(r, node, tmp, view, -view->integer);
    }
    if (view->type != VALUE_REAL) return failed(r->error, SQLEVAL_TYPE, expr, NULL);
    return set_real(r, node, tmp, view, -view->real);
}

/* AND·OR. 왼쪽으로 결과가 정해지면 오른쪽은 계산하지 않는다 */
static NOINLINE sqleval_status_t eval_logic(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    int a, b;
    sqleval_status_t status = truth(r, node->args[0], &a);
    if (status != SQLEVAL_OK) return status;
    bool and = node->src->op == SQLEXPR_OP_AND;
    if (and && a == 0) return set_int(r, node, tmp, view, 0);
    if (!and && a == 1) return set_int(r, node, tmp, view, 1);
    status = truth(r, node->args[1], &b);
    if (status != SQLEVAL_OK) return status;
    int result = and ? (b == 0 ? 0 : (a < 0 || b < 0 ? -1 : 1)) : (b == 1 ? 1 : (a < 0 || b < 0 ? -1 : 0));
    return set_truth(r, node, tmp, view, result);
}

/* NULL이 아닌 두 값의 산술·연결·비교 */
static NOINLINE sqleval_status_t apply_binary(const runner_t *r, const node_t *node, const value_view_t *left,
                                              const value_view_t *right, value_t *tmp, value_view_t *view)
{
    switch (node->src->op) {
        case SQLEXPR_OP_ADD:
        case SQLEXPR_OP_SUB:
        case SQLEXPR_OP_MUL:
        case SQLEXPR_OP_DIV:
        case SQLEXPR_OP_MOD: return arithmetic(r, node, left, right, tmp, view);
        case SQLEXPR_OP_CONCAT: return concat(r, node, left, right, tmp, view);
        default: break;
    }
    int order = compare_values(left, right);
    bool result = false;
    switch (node->src->op) {
        case SQLEXPR_OP_EQ: result = order == 0; break;
        case SQLEXPR_OP_NE: result = order != 0; break;
        case SQLEXPR_OP_LT: result = order < 0; break;
        case SQLEXPR_OP_LE: result = order <= 0; break;
        case SQLEXPR_OP_GT: result = order > 0; break;
        case SQLEXPR_OP_GE: result = order >= 0; break;
        default: break;
    }
    return set_int(r, node, tmp, view, result);
}

static NOINLINE sqleval_status_t eval_binary(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    value_t left_tmp = {0}, right_tmp = {0};
    value_view_t left, right;
    sqleval_status_t status = eval(r, node->args[0], &left_tmp, &left);
    if (status == SQLEVAL_OK) status = eval(r, node->args[1], &right_tmp, &right);
    if (status == SQLEVAL_OK) {
        if (left.type == VALUE_NULL || right.type == VALUE_NULL) status = set_null(r, tmp, view);
        else status = apply_binary(r, node, &left, &right, tmp, view);
    }
    tmp_clear(r, &left_tmp);
    tmp_clear(r, &right_tmp);
    return status;
}

static NOINLINE sqleval_status_t eval_is_null(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    value_t inner = {0};
    value_view_t v = {VALUE_NULL, 0, 0.0, NULL, 0};
    sqleval_status_t status = eval(r, node->args[0], &inner, &v);
    bool is_null = v.type == VALUE_NULL;
    tmp_clear(r, &inner);
    if (status != SQLEVAL_OK) return status;
    return set_int(r, node, tmp, view, is_null != node->src->negated);
}

static NOINLINE sqleval_status_t eval_cast(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    const sqlexpr_t *expr = node->src;
    value_t inner = {0};
    value_view_t v;
    sqleval_status_t status = eval(r, node->args[0], &inner, &v);
    if (status == SQLEVAL_OK) {
        /* 문자열을 숫자로 바꿀 때는 SQLite·PostgreSQL처럼 앞뒤 ASCII 공백을 무시한다 */
        if (v.type == VALUE_TEXT && (expr->cast_type == VALUE_INTEGER || expr->cast_type == VALUE_REAL)) {
            while (v.size_bytes > 0 && is_ascii_space(v.bytes[0])) {
                v.bytes++;
                v.size_bytes--;
            }
            while (v.size_bytes > 0 && is_ascii_space(v.bytes[v.size_bytes - 1])) v.size_bytes--;
        }
        /* 정수를 실수로 바꾸는 CAST는 산술의 암묵 변환처럼 가장 가까운 double이다(SQLite·PostgreSQL과 같음) */
        size_t old = owned_bytes(tmp);
        size_t reserved = expr->cast_type == VALUE_TEXT || expr->cast_type == VALUE_BLOB ? v.size_bytes + CAST_SLACK : 0;
        if (!mem_take(r->memory, reserved)) {
            status = failed(r->error, SQLEVAL_NOMEM, expr, NULL);
        } else {
            value_status_t cast = v.type == VALUE_INTEGER && expr->cast_type == VALUE_REAL
                                      ? value_set_real(tmp, (double)v.integer)
                                      : value_cast(&v, expr->cast_type, tmp);
            status = settle(r, node, cast, old, reserved, tmp, view);
        }
    }
    tmp_clear(r, &inner);
    return status;
}

static NOINLINE sqleval_status_t eval_subquery(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    sqleval_status_t status = refresh(r, node);
    if (status != SQLEVAL_OK) return status;
    if (node->src->kind == SQLEXPR_EXISTS) return set_int(r, node, tmp, view, node->sub->row_count > 0);
    if (node->sub->row_count > 1) return failed(r->error, SQLEVAL_SUBQUERY_SHAPE, node->src, NULL);
    if (node->sub->row_count == 0) return set_null(r, tmp, view);
    *view = *node->sub->first;
    return SQLEVAL_OK;
}

/*
 * 종류별 계산은 따로 둔 함수가 한다. 이 함수는 식 트리 높이만큼 재귀하므로 지역 변수를 두지 않는다(#272:
 * 한 함수에 모두 두면 높이 한 단계의 스택이 가장 큰 경우의 합이 된다).
 */
static sqleval_status_t eval(const runner_t *r, const node_t *node, value_t *tmp, value_view_t *view)
{
    if (node->matched) {
        *view = r->row[node->column].view;
        return SQLEVAL_OK;
    }
    switch (node->src->kind) {
        case SQLEXPR_LITERAL:
            *view = node->src->value.view;
            return SQLEVAL_OK;
        case SQLEXPR_COLUMN:
            *view = node->outer_cell != NULL ? node->outer_cell->view : r->row[node->column].view;
            return SQLEVAL_OK;
        case SQLEXPR_UNARY: return eval_unary(r, node, tmp, view);
        case SQLEXPR_BINARY:
            if (node->src->op == SQLEXPR_OP_AND || node->src->op == SQLEXPR_OP_OR) return eval_logic(r, node, tmp, view);
            return eval_binary(r, node, tmp, view);
        case SQLEXPR_IS_NULL: return eval_is_null(r, node, tmp, view);
        case SQLEXPR_BETWEEN: return eval_between(r, node, tmp, view);
        case SQLEXPR_IN: return eval_in(r, node, tmp, view);
        case SQLEXPR_LIKE: return eval_like(r, node, tmp, view);
        case SQLEXPR_CASE: return eval_case(r, node, tmp, view);
        case SQLEXPR_CAST: return eval_cast(r, node, tmp, view);
        case SQLEXPR_FUNCTION: return eval_function(r, node, tmp, view);
        case SQLEXPR_SUBQUERY:
        case SQLEXPR_EXISTS: return eval_subquery(r, node, tmp, view);
    }
    return failed(r->error, SQLEVAL_TYPE, node->src, NULL);
}

sqleval_status_t sqleval_run(const sqleval_prog_t *prog, const value_t *row, value_t *out, sqleval_error_t *error)
{
    if (prog == NULL || out == NULL) return failed(error, SQLEVAL_ARGUMENT, NULL, NULL);
    runner_t r = {row, error, &prog->memory};
    value_t tmp = {0};
    value_view_t view;
    sqleval_status_t status = eval(&r, prog->root, &tmp, &view);
    if (status != SQLEVAL_OK) {
        tmp_clear(&r, &tmp);
        return status;
    }
    /* 결과 형식을 정적 형식에 맞춘다(INTEGER가 REAL 자리에 온 경우) */
    if (prog->root->type == VALUE_REAL && view.type == VALUE_INTEGER) {
        value_status_t set = value_set_real(out, (double)view.integer);
        tmp_clear(&r, &tmp);
        return set == VALUE_OK ? SQLEVAL_OK : from_value(&r, set, prog->root);
    }
    /* 결과는 계상하지 않은 채 넘긴다(호출자가 자기 몫으로 계상한다). tmp를 그대로 넘기면 그 몫을 돌려준다 */
    if (view.bytes != NULL && view.bytes == tmp.view.bytes) {
        value_clear(out);
        mem_give(&prog->memory, owned_bytes(&tmp));
        *out = tmp;
        return SQLEVAL_OK;
    }
    /* 빌린 값(열·리터럴)의 복제도 예산이 받을 때만 만든다 */
    size_t bytes = view.type == VALUE_TEXT || view.type == VALUE_BLOB ? view.size_bytes : 0;
    if (!mem_take(&prog->memory, bytes)) {
        tmp_clear(&r, &tmp);
        return failed(error, SQLEVAL_NOMEM, prog->root->src, NULL);
    }
    value_status_t cloned = value_clone(&view, out);
    mem_give(&prog->memory, bytes);
    tmp_clear(&r, &tmp);
    return cloned == VALUE_OK ? SQLEVAL_OK : from_value(&r, cloned, prog->root);
}

sqleval_status_t sqleval_test(const sqleval_prog_t *prog, const value_t *row, bool *truth_out, sqleval_error_t *error)
{
    if (prog == NULL || truth_out == NULL) return failed(error, SQLEVAL_ARGUMENT, NULL, NULL);
    runner_t r = {row, error, &prog->memory};
    int t = 0;
    sqleval_status_t status = truth(&r, prog->root, &t);
    *truth_out = status == SQLEVAL_OK && t == 1;
    return status;
}

int sqleval_compare(const value_view_t *a, const value_view_t *b)
{
    if (a->type == VALUE_NULL || b->type == VALUE_NULL) return (a->type != VALUE_NULL) - (b->type != VALUE_NULL);
    return compare_values(a, b);
}
