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
/* 모은 사본을 지금 파일에 쓰고 비운다. 사본 예산이 모자랄 때 쓴다. 실패하면 사본이 남고 거래는 실패로 끝내야 한다 */
page_file_status_t page_file_workspace_spill(page_file_t *file);
void page_file_workspace_end(page_file_t *file);

#endif
