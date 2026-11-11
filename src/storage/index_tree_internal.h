#ifndef STORAGE_INDEX_TREE_INTERNAL_H
#define STORAGE_INDEX_TREE_INTERNAL_H

#include "storage/pk_tree.h"

/*
 * pk_tree와 text_tree가 함께 쓰는 노드 페이지 COW 쓰기. old_page가 이 batch가 만든 페이지면 제자리에, 아니면 새 페이지를
 * 할당해 쓰고 옛 페이지를 놓는다. old_page가 PAGE_FILE_NO_PAGE면 새 노드다. 성공하면 *page가 쓴 페이지 번호다.
 * 노드 header와 본문 채우기는 트리마다 다르므로 호출자가 body를 다 채운 뒤 부른다.
 */
static inline pk_status_t index_tree_write_node(const pk_tree_io_t *io, const uint8_t *body, uint32_t old_page,
                                                uint32_t *page)
{
    uint32_t target = old_page;
    bool in_place = old_page != PAGE_FILE_NO_PAGE && io->is_fresh(io->context, old_page);
    if (!in_place) {
        pk_status_t allocated = io->allocate(io->context, &target);
        if (allocated != PK_OK) return allocated;
    }
    pk_status_t written = io->write(io->context, target, body);
    if (written != PK_OK) return written;
    if (!in_place && old_page != PAGE_FILE_NO_PAGE) {
        pk_status_t released = io->release(io->context, old_page);
        if (released != PK_OK) return released;
    }
    *page = target;
    return PK_OK;
}

#endif
