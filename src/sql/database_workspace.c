#include "database_exec.h"

bool database_workspace_set_enabled(database_t *db, bool enabled)
{
    if (db == NULL || db->work.active) return false;
    db->workspace_mode = enabled ? 1 : -1;
    return true;
}

bool database_workspace_enabled(const database_t *db)
{
    return db != NULL && (db->workspace_mode > 0 || (db->workspace_mode == 0 && STREAMLITE_TXN_WORKSPACE));
}

static bool fits(database_t *db, size_t catalog_bytes, size_t list_bytes, size_t extra)
{
    size_t used = db->work.pages.bytes;
    const size_t parts[] = {catalog_bytes, list_bytes, extra};
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if (parts[i] > DATABASE_WORKSPACE_BYTES - used) return false;
        used += parts[i];
    }
    if (used > db->workspace_stats.peak_bytes) db->workspace_stats.peak_bytes = used;
    return true;
}

/*
 * 사본 예산(DATABASE_WORKSPACE_BYTES) 안에 들어가는지. 모은 페이지 사본 때문에 모자라면 사본을 파일에 먼저 쓰고(사본
 * 없이 쓰는 경로와 같은 새 페이지 쓰기) 다시 본다. 그래서 큰 거래도 예산 때문에 실패하지 않고 사본 이득만 줄어든다.
 */
bool database_workspace_allows(database_t *db, size_t catalog_bytes, size_t list_bytes, size_t extra)
{
    if (!database_workspace_enabled(db)) return true;
    if (fits(db, catalog_bytes, list_bytes, extra)) return true;
    if (!db->work.active || db->work.pages.bytes == 0 || page_file_workspace_spill(db->file) != PAGE_FILE_OK) return false;
    return fits(db, catalog_bytes, list_bytes, extra);
}

static bool reserve_images(void *ctx, mem_scope_t scope, size_t bytes)
{
    database_t *db = ctx;
    /* 넘으면 NOMEM을 받은 page_file_write가 사본을 비우고 바로 쓴다(여기서 비우지 않는다) */
    return fits(db, db->work.res_cat, db->work.res_lists, bytes) && mem_reserve(&db->resident, scope, bytes);
}

static void release_images(void *ctx, mem_scope_t scope, size_t bytes)
{
    database_t *db = ctx;
    mem_release(&db->resident, scope, bytes);
}

static const mem_ledger_ops_t IMAGE_LEDGER = {reserve_images, release_images, NULL, NULL};

static bool admit_pages(void *ctx, uint32_t count)
{
    database_t *db = ctx;
    size_t bytes = page_file_list_peak_bytes(count);
    if (bytes <= db->res_lists) return true;
    size_t extra = bytes - db->res_lists;
    if (!fits(db, db->work.res_cat, db->work.res_lists, extra)) return false;
    if (!mem_reserve(&db->resident, MEM_SCOPE_RESIDENT, extra)) return false;
    db->res_lists = bytes;
    return true;
}

void database_workspace_begin(database_t *db)
{
    db->workspace_stats = (database_workspace_stats_t){0};
    if (!database_workspace_enabled(db)) return;
    db->work.pages.ledger = (mem_ledger_t){&IMAGE_LEDGER, db};
    db->work.pages.admit_pages = admit_pages;
    page_file_status_t status = page_file_workspace_begin(db->file, &db->work.pages);
    assert(status == PAGE_FILE_OK);
}

void database_workspace_stats(const database_t *db, database_workspace_stats_t *stats)
{
    *stats = db->workspace_stats;
    stats->bytes = db->work.res_cat + db->work.res_lists + db->work.pages.bytes;
    if (db->work.active) {
        stats->buffered_writes = db->work.pages.writes;
        stats->physical_writes = db->work.pages.flushed;
    }
}

void database_workspace_end(database_t *db)
{
    page_file_workspace_end(db->file);
    db->workspace_stats.buffered_writes = db->work.pages.writes;
    db->workspace_stats.physical_writes = db->work.pages.flushed;
    page_workspace_clear(&db->work.pages);
}
