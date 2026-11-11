#ifndef STATEMENT_CLONE_H
#define STATEMENT_CLONE_H

#include "sql/sql_statement.h"

typedef struct {
    const lexer_binding_t *binding;
    size_t limit_bytes;
    size_t used_bytes;
    sqlstmt_error_t error;
    sqlstmt_status_t status;
} statement_clone_t;

sqlstmt_status_t statement_clone(const sqlstmt_t *source, statement_clone_t *context, sqlstmt_t **out);

#endif
