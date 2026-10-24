#ifndef PAGE_WORKSPACE_H
#define PAGE_WORKSPACE_H

#include "sql/mem_ledger.h"
#include "storage/page_file.h"

typedef struct page_image page_image_t;
typedef struct {
    page_image_t *images;
    mem_ledger_t ledger;
    size_t bytes;
    size_t peak_bytes;
    size_t writes;
    size_t flushed;
    bool (*admit_pages)(void *ctx, uint32_t count);
} page_workspace_t;

typedef page_file_status_t (*page_workspace_write_fn)(void *, uint32_t, page_kind_t, uint32_t,
                                                      const uint8_t[PAGE_BODY_BYTES]);

bool page_workspace_read(const page_workspace_t *workspace, uint32_t id, page_kind_t *kind,
                          uint32_t *table, uint8_t body[PAGE_BODY_BYTES]);
page_file_status_t page_workspace_write(page_workspace_t *workspace, uint32_t id, page_kind_t kind,
                                         uint32_t table, const uint8_t body[PAGE_BODY_BYTES]);
void page_workspace_drop(page_workspace_t *workspace, uint32_t id);
void page_workspace_clear(page_workspace_t *workspace);
page_file_status_t page_workspace_flush(page_workspace_t *workspace, page_workspace_write_fn write, void *ctx);
page_file_status_t page_file_workspace_begin(page_file_t *file, page_workspace_t *workspace);
void page_file_workspace_end(page_file_t *file);

#endif
