#ifndef DATABASE_WORKSPACE_H
#define DATABASE_WORKSPACE_H

#include "sql/database.h"

#ifndef TOTE_TXN_WORKSPACE
#define TOTE_TXN_WORKSPACE 0
#endif
#define DATABASE_WORKSPACE_BYTES (8u * 1024u * 1024u)

typedef struct {
    size_t bytes;
    size_t peak_bytes;
    size_t buffered_writes;
    size_t physical_writes;
    size_t catalog_clones;
} database_workspace_stats_t;

bool database_workspace_set_enabled(database_t *db, bool enabled);
bool database_workspace_enabled(const database_t *db);
void database_workspace_stats(const database_t *db, database_workspace_stats_t *stats);
bool database_workspace_allows(database_t *db, size_t catalog_bytes, size_t list_bytes, size_t extra);
void database_workspace_begin(database_t *db);
void database_workspace_end(database_t *db);

#endif
