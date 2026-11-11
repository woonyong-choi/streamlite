#include <stdlib.h>
#include <string.h>

#include "page_workspace.h"

struct page_image {
    page_image_t *next;
    uint32_t id;
    page_kind_t kind;
    uint32_t table;
    uint8_t body[PAGE_BODY_BYTES];
};

static page_image_t *find_image(const page_workspace_t *workspace, uint32_t id)
{
    for (page_image_t *image = workspace->images; image != NULL; image = image->next) {
        if (image->id == id) return image;
    }
    return NULL;
}

bool page_workspace_read(const page_workspace_t *workspace, uint32_t id, page_kind_t *kind,
                          uint32_t *table, uint8_t body[PAGE_BODY_BYTES])
{
    page_image_t *image = find_image(workspace, id);
    if (image == NULL) return false;
    *kind = image->kind;
    *table = image->table;
    memcpy(body, image->body, PAGE_BODY_BYTES);
    return true;
}

page_file_status_t page_workspace_write(page_workspace_t *workspace, uint32_t id, page_kind_t kind,
                                         uint32_t table, const uint8_t body[PAGE_BODY_BYTES])
{
    page_image_t *image = find_image(workspace, id);
    if (image == NULL) {
        if (!mem_reserve(&workspace->ledger, MEM_SCOPE_RESIDENT, sizeof(*image))) return PAGE_FILE_NOMEM;
        image = malloc(sizeof(*image));
        if (image == NULL) {
            mem_release(&workspace->ledger, MEM_SCOPE_RESIDENT, sizeof(*image));
            return PAGE_FILE_NOMEM;
        }
        image->next = workspace->images;
        image->id = id;
        workspace->images = image;
        workspace->bytes += sizeof(*image);
        if (workspace->bytes > workspace->peak_bytes) workspace->peak_bytes = workspace->bytes;
    }
    image->kind = kind;
    image->table = table;
    memcpy(image->body, body, PAGE_BODY_BYTES);
    workspace->writes++;
    return PAGE_FILE_OK;
}

void page_workspace_drop(page_workspace_t *workspace, uint32_t id)
{
    page_image_t **link = &workspace->images;
    while (*link != NULL && (*link)->id != id) link = &(*link)->next;
    if (*link == NULL) return;
    page_image_t *image = *link;
    *link = image->next;
    free(image);
    workspace->bytes -= sizeof(*image);
    mem_release(&workspace->ledger, MEM_SCOPE_RESIDENT, sizeof(*image));
}

void page_workspace_clear(page_workspace_t *workspace)
{
    while (workspace->images != NULL) page_workspace_drop(workspace, workspace->images->id);
}

page_file_status_t page_workspace_flush(page_workspace_t *workspace, page_workspace_write_fn write, void *ctx)
{
    for (const page_image_t *image = workspace->images; image != NULL; image = image->next) {
        page_file_status_t status = write(ctx, image->id, image->kind, image->table, image->body);
        if (status != PAGE_FILE_OK) return status;
        workspace->flushed++;
    }
    return PAGE_FILE_OK;
}
